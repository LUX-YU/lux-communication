#pragma once

#include <cassert>
#include <cstddef>
#include <vector>
#include <lux/communication/ExecEntry.hpp>

namespace lux::communication {

    /**
     * @brief Diagnostic statistics for ReorderBuffer performance monitoring.
     */
    struct ReorderBufferStats {
        uint64_t ring_put_ok = 0;           // Successful ring insertions
        uint64_t max_window = 0;            // Max observed (seq - next_seq)
        uint64_t discarded_old = 0;         // Entries discarded (seq < next_seq)
    };

    /**
     * @brief Ring buffer for reordering execution entries by sequence number.
     *        O(1) average insert/lookup when out-of-order window is bounded.
     */
    class ReorderRing {
    public:
        explicit ReorderRing(size_t cap_pow2 = 1 << 16)  // 65536 default
            : cap_(cap_pow2), mask_(cap_pow2 - 1), slots_(cap_pow2), pending_count_(0)
        {
            // cap_pow2 must be power of 2
        }

        /**
         * @brief Try to put an entry into the ring buffer.
         *        Combined can_accept + put for better cache locality.
         * @param e The entry to insert (will be moved if successful)
         * @param next_seq The current expected next sequence number
         * @return true if successfully inserted or discarded (too old), 
         *         false if needs fallback (too far ahead or collision)
         */
        bool try_put(ExecEntry&& e, uint64_t next_seq) {
            const uint64_t seq = e.seq;

            // Too old: discard silently (shouldn't happen normally)
            if (seq < next_seq) {
                return true;
            }

            // Too far ahead: exceeds window, ring can't hold it
            if (seq - next_seq >= cap_) {
                return false;
            }

            auto& slot = slots_[static_cast<size_t>(seq) & mask_];
            if (!slot.occupied) {
                slot.occupied = true;
                slot.seq = seq;
                slot.entry = std::move(e);
                ++pending_count_;
                return true;
            }

            // Same seq (duplicate) - overwrite
            if (slot.seq == seq) {
                slot.entry = std::move(e);
                return true;
            }

            // Collision: cap not enough or out-of-order span too large
            return false;
        }

        /**
         * @brief Get entry at specific sequence number.
         * @return Pointer to entry if exists, nullptr otherwise
         */
        ExecEntry* get(uint64_t seq) {
            auto& slot = slots_[static_cast<size_t>(seq) & mask_];
            if (slot.occupied && slot.seq == seq) {
                return &slot.entry;
            }
            return nullptr;
        }

        /**
         * @brief Erase entry at specific sequence number.
         */
        void erase(uint64_t seq) {
            auto& slot = slots_[static_cast<size_t>(seq) & mask_];
            if (slot.occupied && slot.seq == seq) {
                slot.occupied = false;
                slot.entry = ExecEntry{};  // Release resources
                --pending_count_;
            }
        }

        size_t pending_count() const { return pending_count_; }
        size_t capacity() const { return cap_; }

    private:
        struct Slot {
            bool occupied = false;
            uint64_t seq = 0;
            ExecEntry entry;
        };

        size_t cap_;
        size_t mask_;
        std::vector<Slot> slots_;
        size_t pending_count_;
    };

    /**
     * @brief Ring-only reorder buffer.
     *        Adaptive drain throttling in SeqOrderedExecutor guarantees the
     *        out-of-order window stays within ring capacity, so no fallback
     *        hashmap is needed.  O(1) insert and pop.
     */
    class ReorderBuffer {
    public:
        explicit ReorderBuffer(size_t ring_cap_pow2 = 1 << 16)  // 65536
            : ring_(ring_cap_pow2), next_seq_(1)
        {
        }

        /**
         * @brief Insert an entry into the reorder buffer.
         *        Must fit within ring window (guaranteed by adaptive throttle).
         */
        void put(ExecEntry&& e) {
            uint64_t seq = e.seq;

            if (seq < next_seq_) {
                ++stats_.discarded_old;
                return;
            }

            // Track max window for diagnostics
            uint64_t window = seq - next_seq_;
            if (window > stats_.max_window)
                stats_.max_window = window;

            bool ok = ring_.try_put(std::move(e), next_seq_);
            assert(ok && "ReorderBuffer: entry outside ring window — throttle bug?");
            (void)ok;  // suppress unused-variable warning in release
            ++stats_.ring_put_ok;
        }

        /**
         * @brief Try to pop the next expected entry.
         * @param out Output entry
         * @return true if found and popped, false if next_seq not available
         */
        bool try_pop_next(ExecEntry& out) {
            if (auto* p = ring_.get(next_seq_)) {
                out = std::move(*p);
                ring_.erase(next_seq_);
                ++next_seq_;
                return true;
            }
            return false;
        }

        /**
         * @brief Get the next expected sequence number.
         */
        uint64_t next_seq() const { return next_seq_; }

        /**
         * @brief Set the next expected sequence number (for initialization).
         */
        void set_next_seq(uint64_t seq) { next_seq_ = seq; }

        size_t pending_size() const {
            return ring_.pending_count();
        }

        size_t ring_capacity() const {
            return ring_.capacity();
        }

        /**
         * @brief Get diagnostic statistics.
         */
        const ReorderBufferStats& stats() const { return stats_; }

        /**
         * @brief Reset diagnostic statistics.
         */
        void reset_stats() { stats_ = ReorderBufferStats{}; }

    private:
        ReorderRing ring_;
        uint64_t next_seq_;
        ReorderBufferStats stats_;
    };

} // namespace lux::communication
