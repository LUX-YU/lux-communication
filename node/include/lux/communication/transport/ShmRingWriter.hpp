#pragma once
#include <lux/communication/platform/SharedMemory.hpp>
#include <lux/communication/transport/ShmRingBuffer.hpp>
#include <lux/communication/transport/ShmNotify.hpp>
#include <lux/communication/visibility.h>
#include <memory>
#include <string>

namespace lux::communication::transport
{
    /// SPSC ring-buffer writer — the Publisher side.
    ///
    /// Creates a new shared-memory segment, initialises the RingHeader, and
    /// provides the SPSC write protocol (acquire → memcpy → commit → notify).
    class LUX_COMMUNICATION_PUBLIC ShmRingWriter
    {
    public:
        /// Create a new SHM ring.
        /// @param shm_name    Logical name (will be platform-adjusted internally).
        /// @param slot_count  Number of slots (must be a power of 2).
        /// @param slot_size   Bytes per slot including SlotHeader (>= 4 KB recommended).
        ShmRingWriter(const std::string &shm_name,
                      uint32_t slot_count = 16,
                      uint32_t slot_size = 1024 * 1024);

        ~ShmRingWriter();

        ShmRingWriter(const ShmRingWriter &) = delete;
        ShmRingWriter &operator=(const ShmRingWriter &) = delete;
        ShmRingWriter(ShmRingWriter &&other) noexcept;
        ShmRingWriter &operator=(ShmRingWriter &&other) noexcept;

        /// Acquire the next writable slot's **payload** area.
        /// @return Pointer past the SlotHeader, or nullptr if ring is full.
        void *acquireSlot();

        /// Commit the current slot: mark it READY, advance write_seq, wake reader.
        /// @param payload_size  Actual bytes written into the payload area.
        void commitSlot(uint32_t payload_size);

        /// Cancel an acquired-but-not-committed slot.
        /// Resets the slot state to Free without advancing write_seq.
        /// Must be called exactly once per acquireSlot() that won't be committed.
        void cancelSlot();

        /// Convenience: copy @p data into the next slot and commit.
        /// @return true on success, false if ring is full.
        bool write(const void *data, uint32_t size);

        /// Is the ring full?
        bool isFull() const;

        /// Logical SHM name (as passed to the constructor).
        const std::string &shmName() const { return shm_name_; }

        /// Maximum payload that fits in one slot (slot_size - sizeof(SlotHeader)).
        uint32_t maxPayloadSize() const;

    private:
        platform::SharedMemorySegment *shm_ = nullptr;
        RingHeader *header_ = nullptr;
        uint32_t slot_count_ = 0;
        uint32_t slot_size_ = 0;
        uint64_t cached_read_seq_ = 0; // reduce cross-process cache bounce
        std::string shm_name_;

        std::unique_ptr<ShmNotifier> notifier_;
    };

} // namespace lux::communication::transport
