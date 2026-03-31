#include <lux/communication/transport/ShmRingWriter.hpp>
#include <lux/communication/platform/PlatformDefs.hpp>
#include <lux/communication/transport/FrameHeader.hpp>

#include <cassert>
#include <cstring>
#include <stdexcept>

namespace lux::communication::transport
{
    ShmRingWriter::ShmRingWriter(const std::string &shm_name, uint32_t slot_count, uint32_t slot_size)
        : slot_count_(slot_count), slot_size_(slot_size), shm_name_(shm_name)
    {
        if (!isPowerOf2(slot_count))
            throw std::invalid_argument("ShmRingWriter: slot_count must be a power of 2");
        if (slot_size <= sizeof(SlotHeader))
            throw std::invalid_argument("ShmRingWriter: slot_size must be > sizeof(SlotHeader)");

        const size_t total = ringTotalSize(slot_count, slot_size);
        const std::string platform_name = platform::shmPlatformName(shm_name);

        shm_ = platform::SharedMemorySegment::open(platform_name, total, /*create=*/true);
        if (!shm_)
            throw std::runtime_error("ShmRingWriter: failed to create SHM segment: " + shm_name);

        header_ = static_cast<RingHeader *>(shm_->data());

        // Initialise writer cacheline
        auto &w = header_->writer;
        w.magic = kRingMagic;
        w.version = kRingVersion;
        w.slot_count = slot_count;
        w.slot_size = slot_size;
        w.write_seq.store(0, std::memory_order_relaxed);
        w.writer_pid = platform::currentPid();

        // Initialise reader cacheline
        auto &r = header_->reader;
        r.read_seq.store(0, std::memory_order_relaxed);
        r.reader_pid = 0;

        // Initialise notify block
        auto &n = header_->notify;
        n.futex_word.store(0, std::memory_order_relaxed);
#ifdef _WIN32
        // Store the Event name that the reader will open.
        std::string event_name = "Local\\lux_evt_" + shm_name;
        if (event_name.size() >= sizeof(n.event_name))
            event_name.resize(sizeof(n.event_name) - 1);
        std::memset(n.event_name, 0, sizeof(n.event_name));
        std::memcpy(n.event_name, event_name.c_str(), event_name.size());
#endif

        // Zero all slots
        for (uint32_t i = 0; i < slot_count; ++i)
        {
            auto *slot = static_cast<SlotHeader *>(slotAt(header_, i, slot_size));
            slot->state.store(static_cast<uint32_t>(SlotState::Free),
                              std::memory_order_relaxed);
            slot->payload_size = 0;
            slot->reserved = 0;
        }

        notifier_ = std::make_unique<ShmNotifier>(&header_->notify);
    }

    ShmRingWriter::~ShmRingWriter()
    {
        notifier_.reset();
        if (shm_)
        {
            shm_->unlink();
            delete shm_;
            shm_ = nullptr;
        }
    }

    ShmRingWriter::ShmRingWriter(ShmRingWriter &&other) noexcept
        : shm_(other.shm_), header_(other.header_), slot_count_(other.slot_count_), slot_size_(other.slot_size_), cached_read_seq_(other.cached_read_seq_), shm_name_(std::move(other.shm_name_)), notifier_(std::move(other.notifier_))
    {
        other.shm_ = nullptr;
        other.header_ = nullptr;
    }

    ShmRingWriter &ShmRingWriter::operator=(ShmRingWriter &&other) noexcept
    {
        if (this != &other)
        {
            notifier_.reset();
            if (shm_)
            {
                shm_->unlink();
                delete shm_;
            }

            shm_ = other.shm_;
            header_ = other.header_;
            slot_count_ = other.slot_count_;
            slot_size_ = other.slot_size_;
            cached_read_seq_ = other.cached_read_seq_;
            shm_name_ = std::move(other.shm_name_);
            notifier_ = std::move(other.notifier_);

            other.shm_ = nullptr;
            other.header_ = nullptr;
        }
        return *this;
    }

    void *ShmRingWriter::acquireSlot()
    {
        const uint64_t wseq = header_->writer.write_seq.load(std::memory_order_relaxed);

        // Check if ring is full (using cached read_seq first to avoid cross-process load).
        if (wseq - cached_read_seq_ >= slot_count_)
        {
            // Refresh from shared memory.
            cached_read_seq_ = header_->reader.read_seq.load(std::memory_order_acquire);
            if (wseq - cached_read_seq_ >= slot_count_)
                return nullptr; // ring genuinely full
        }

        const uint32_t idx = static_cast<uint32_t>(wseq & (slot_count_ - 1));
        auto *slot = static_cast<SlotHeader *>(slotAt(header_, idx, slot_size_));

        // Mark as Writing (informational; the SPSC invariant already protects us).
        slot->state.store(static_cast<uint32_t>(SlotState::Writing),
                          std::memory_order_relaxed);

        return slotPayload(slot);
    }

    void ShmRingWriter::commitSlot(uint32_t payload_size)
    {
        const uint64_t wseq = header_->writer.write_seq.load(std::memory_order_relaxed);
        const uint32_t idx = static_cast<uint32_t>(wseq & (slot_count_ - 1));
        auto *slot = static_cast<SlotHeader *>(slotAt(header_, idx, slot_size_));

        slot->payload_size = payload_size;
        // Release: ensure payload bytes are visible before state becomes Ready.
        slot->state.store(static_cast<uint32_t>(SlotState::Ready),
                          std::memory_order_release);

        // Advance write_seq (release so reader sees it after slot state).
        header_->writer.write_seq.store(wseq + 1, std::memory_order_release);

        // Bump futex word (so waiters can detect change) and wake reader.
        header_->notify.futex_word.fetch_add(1, std::memory_order_release);
        notifier_->wake();
    }

    void ShmRingWriter::cancelSlot()
    {
        const uint64_t wseq = header_->writer.write_seq.load(std::memory_order_relaxed);
        const uint32_t idx = static_cast<uint32_t>(wseq & (slot_count_ - 1));
        auto *slot = static_cast<SlotHeader *>(slotAt(header_, idx, slot_size_));

        // Reset slot to Free WITHOUT advancing write_seq.
        slot->state.store(static_cast<uint32_t>(SlotState::Free),
                          std::memory_order_release);
    }

    bool ShmRingWriter::write(const void *data, uint32_t size)
    {
        void *payload = acquireSlot();
        if (!payload)
            return false;
        if (size > maxPayloadSize())
        {
            // Can't fit; roll back by not committing. The slot will stay in
            // Writing state and be reclaimed on next acquireSlot for the same
            // index — but since SPSC, the writer never re-visits a slot that
            // wasn't committed unless the ring wraps.  Mark it Free explicitly.
            const uint64_t wseq = header_->writer.write_seq.load(std::memory_order_relaxed);
            const uint32_t idx = static_cast<uint32_t>(wseq & (slot_count_ - 1));
            auto *slot = static_cast<SlotHeader *>(slotAt(header_, idx, slot_size_));
            slot->state.store(static_cast<uint32_t>(SlotState::Free),
                              std::memory_order_relaxed);
            return false;
        }
        std::memcpy(payload, data, size);
        commitSlot(size);
        return true;
    }

    bool ShmRingWriter::isFull() const
    {
        const uint64_t wseq = header_->writer.write_seq.load(std::memory_order_relaxed);
        const uint64_t rseq = header_->reader.read_seq.load(std::memory_order_acquire);
        return (wseq - rseq) >= slot_count_;
    }

    uint32_t ShmRingWriter::maxPayloadSize() const
    {
        return transport::maxSlotPayload(slot_size_);
    }

} // namespace lux::communication::transport
