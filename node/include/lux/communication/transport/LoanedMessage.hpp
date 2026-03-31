#pragma once

/// LoanedMessage — RAII guard for zero-copy message construction in SHM.
///
/// Allows the user to construct a message directly in a ring-buffer slot,
/// eliminating the serialize + memcpy path for TriviallyCopyableMsg types.
///
/// Usage:
///   auto loan = publisher.loan();
///   if (loan) {
///       loan->x = 42;
///       loan->y = 3.14;
///       publisher.publish(std::move(loan));
///   }
///
/// If the loan goes out of scope without being published, the slot is
/// cancelled (marked Free, write_seq not advanced).

#include <lux/communication/transport/FrameHeader.hpp>
#include <lux/communication/transport/ShmRingWriter.hpp>
#include <lux/communication/serialization/Serializer.hpp>
#include <cstring>
#include <type_traits>
#include <utility>

namespace lux::communication
{
    template <typename T>
    class Publisher;
}

namespace lux::communication::transport
{
    /// RAII guard for a loaned SHM ring slot.
    ///
    /// Requires T to satisfy TriviallyCopyableMsg — non-trivial types (Protobuf,
    /// Custom) must use the conventional publish(const T&) path.
    template <typename T>
    class LoanedMessage
    {
        static_assert(serialization::TriviallyCopyableMsg<T>,
                      "LoanedMessage requires a TriviallyCopyableMsg type. "
                      "Non-trivial types should use publish(const T&) or the data pool path.");

    public:
        LoanedMessage() = default;

        /// Acquire a slot and placement-new the FrameHeader + default T.
        LoanedMessage(ShmRingWriter *writer, const FrameHeader &header)
            : writer_(writer)
        {
            slot_base_ = writer_->acquireSlot();
            if (!slot_base_)
            {
                writer_ = nullptr;
                return;
            }

            // Place FrameHeader at slot payload start.
            std::memcpy(slot_base_, &header, sizeof(FrameHeader));

            // Placement-new T after FrameHeader.
            void *msg_addr = static_cast<char *>(slot_base_) + sizeof(FrameHeader);
            msg_ = new (msg_addr) T{};
        }

        ~LoanedMessage()
        {
            if (msg_ && !committed_)
                cancel();
        }

        // Move-only.
        LoanedMessage(LoanedMessage &&o) noexcept
            : writer_(o.writer_), msg_(o.msg_),
              slot_base_(o.slot_base_), committed_(o.committed_)
        {
            o.writer_ = nullptr;
            o.msg_ = nullptr;
            o.slot_base_ = nullptr;
            o.committed_ = false;
        }

        LoanedMessage &operator=(LoanedMessage &&o) noexcept
        {
            if (this != &o)
            {
                if (msg_ && !committed_)
                    cancel();
                writer_ = o.writer_;
                msg_ = o.msg_;
                slot_base_ = o.slot_base_;
                committed_ = o.committed_;
                o.writer_ = nullptr;
                o.msg_ = nullptr;
                o.slot_base_ = nullptr;
                o.committed_ = false;
            }
            return *this;
        }

        LoanedMessage(const LoanedMessage &) = delete;
        LoanedMessage &operator=(const LoanedMessage &) = delete;

        /// Access the message being constructed in SHM.
        T *get() noexcept { return msg_; }
        const T *get() const noexcept { return msg_; }
        T &operator*() noexcept { return *msg_; }
        T *operator->() noexcept { return msg_; }
        const T &operator*() const noexcept { return *msg_; }
        const T *operator->() const noexcept { return msg_; }

        /// Is this loan valid (holds an acquired slot)?
        explicit operator bool() const noexcept { return msg_ != nullptr; }
        bool valid() const noexcept { return msg_ != nullptr; }

        /// Cancel the loan: destroy T (if non-trivial), mark slot Free.
        void cancel() noexcept
        {
            if (!msg_)
                return;
            if constexpr (!std::is_trivially_destructible_v<T>)
            {
                msg_->~T();
            }
            writer_->cancelSlot();
            msg_ = nullptr;
            slot_base_ = nullptr;
            writer_ = nullptr;
        }

        /// Access the raw slot base (FrameHeader + T).
        /// Used internally by Publisher for multi-ring replication.
        const void *slotBase() const noexcept { return slot_base_; }

        /// Commit the loan: mark slot as READY, advance write_seq, wake reader.
        /// After this call the loan is no longer valid (committed_ = true).
        /// @param total_size  Total payload bytes (typically sizeof(FrameHeader) + sizeof(T)).
        void commit(uint32_t total_size)
        {
            if (!msg_ || committed_)
                return;
            writer_->commitSlot(total_size);
            committed_ = true;
        }

    private:
        template <typename U>
        friend class ::lux::communication::Publisher;

        /// Mark as committed (prevent destructor cancel) without calling commitSlot.
        /// Used by Publisher when it handles the commit itself.
        void markCommitted() noexcept { committed_ = true; }

        ShmRingWriter *writer_ = nullptr;
        T *msg_ = nullptr;
        void *slot_base_ = nullptr;
        bool committed_ = false;
    };

} // namespace lux::communication::transport
