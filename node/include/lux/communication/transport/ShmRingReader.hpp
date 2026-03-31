#pragma once
#include <lux/communication/platform/SharedMemory.hpp>
#include <lux/communication/transport/ShmRingBuffer.hpp>
#include <lux/communication/transport/ShmNotify.hpp>
#include <lux/communication/visibility.h>
#include <chrono>
#include <memory>
#include <string>

namespace lux::communication::transport
{
    /// SPSC ring-buffer reader — the Subscriber side.
    ///
    /// Opens an existing shared-memory ring (created by ShmRingWriter) and
    /// provides the SPSC read protocol with a zero-copy ReadView API.
    class LUX_COMMUNICATION_PUBLIC ShmRingReader
    {
    public:
        /// Open an existing SHM ring.
        /// @param shm_name  Logical name (must match the writer's name).
        explicit ShmRingReader(const std::string &shm_name);

        ~ShmRingReader();

        ShmRingReader(const ShmRingReader &) = delete;
        ShmRingReader &operator=(const ShmRingReader &) = delete;
        ShmRingReader(ShmRingReader &&other) noexcept;
        ShmRingReader &operator=(ShmRingReader &&other) noexcept;

        /// Zero-copy view into the SHM slot payload.
        struct ReadView
        {
            const void *data = nullptr; // FrameHeader + serialised bytes
            uint32_t size = 0;
        };

        /// Try to acquire the next readable slot.
        /// @param timeout  Maximum adaptive-wait time (0 = non-blocking poll).
        /// @return A view with data == nullptr if nothing is available.
        ReadView acquireReadView(
            std::chrono::microseconds timeout = std::chrono::microseconds{0});

        /// Release the current ReadView: mark slot FREE and advance read_seq.
        void releaseReadView();

        /// Convenience: copy-read the next payload into @p buffer.
        /// @return Bytes copied, or 0 on timeout / no data.
        uint32_t read(void *buffer, uint32_t max_size,
                      std::chrono::microseconds timeout = std::chrono::microseconds{0});

        /// Is there unread data in the ring?
        bool hasData() const;

        /// Logical SHM name.
        const std::string &shmName() const { return shm_name_; }

    private:
        platform::SharedMemorySegment *shm_ = nullptr;
        RingHeader *header_ = nullptr;
        uint32_t slot_count_ = 0;
        uint32_t slot_size_ = 0;
        uint64_t cached_write_seq_ = 0; // reduce cross-process cache bounce
        bool view_held_ = false;
        std::string shm_name_;

        std::unique_ptr<ShmWaiter> waiter_;
    };

} // namespace lux::communication::transport
