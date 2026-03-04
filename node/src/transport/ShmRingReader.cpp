#include <lux/communication/transport/ShmRingReader.hpp>
#include <lux/communication/platform/PlatformDefs.hpp>
#include <lux/communication/transport/FrameHeader.hpp>

#include <cassert>
#include <cstring>
#include <stdexcept>

namespace lux::communication::transport {

ShmRingReader::ShmRingReader(const std::string& shm_name)
    : shm_name_(shm_name)
{
    const std::string platform_name = platform::shmPlatformName(shm_name);

    // Try to open an existing segment — we don't know the size yet,
    // so open with a minimal size first to read the header.
    shm_ = platform::SharedMemorySegment::open(
        platform_name, sizeof(RingHeader), /*create=*/false);
    if (!shm_)
        throw std::runtime_error("ShmRingReader: SHM segment not found: " + shm_name);

    // Read ring parameters from the header.
    auto* hdr = static_cast<const RingHeader*>(shm_->data());
    if (hdr->writer.magic != kRingMagic || hdr->writer.version != kRingVersion) {
        delete shm_;
        shm_ = nullptr;
        throw std::runtime_error("ShmRingReader: invalid ring magic/version in: " + shm_name);
    }

    slot_count_ = hdr->writer.slot_count;
    slot_size_  = hdr->writer.slot_size;

    // Re-open with full size if the initial mapping was too small.
    const size_t full_size = ringTotalSize(slot_count_, slot_size_);
    if (shm_->size() < full_size) {
        delete shm_;
        shm_ = platform::SharedMemorySegment::open(
            platform_name, full_size, /*create=*/false);
        if (!shm_)
            throw std::runtime_error("ShmRingReader: failed to re-map full ring: " + shm_name);
    }

    header_ = static_cast<RingHeader*>(shm_->data());

    // Record this reader's PID.
    header_->reader.reader_pid = platform::currentPid();

    waiter_ = std::make_unique<ShmWaiter>(&header_->notify);
}

ShmRingReader::~ShmRingReader() {
    waiter_.reset();
    if (shm_) {
        delete shm_;
        shm_ = nullptr;
    }
}

ShmRingReader::ShmRingReader(ShmRingReader&& other) noexcept
    : shm_(other.shm_)
    , header_(other.header_)
    , slot_count_(other.slot_count_)
    , slot_size_(other.slot_size_)
    , cached_write_seq_(other.cached_write_seq_)
    , view_held_(other.view_held_)
    , shm_name_(std::move(other.shm_name_))
    , waiter_(std::move(other.waiter_))
{
    other.shm_    = nullptr;
    other.header_ = nullptr;
    other.view_held_ = false;
}

ShmRingReader& ShmRingReader::operator=(ShmRingReader&& other) noexcept {
    if (this != &other) {
        waiter_.reset();
        if (shm_) delete shm_;

        shm_              = other.shm_;
        header_           = other.header_;
        slot_count_       = other.slot_count_;
        slot_size_        = other.slot_size_;
        cached_write_seq_ = other.cached_write_seq_;
        view_held_        = other.view_held_;
        shm_name_         = std::move(other.shm_name_);
        waiter_           = std::move(other.waiter_);

        other.shm_       = nullptr;
        other.header_    = nullptr;
        other.view_held_ = false;
    }
    return *this;
}

ShmRingReader::ReadView
ShmRingReader::acquireReadView(std::chrono::microseconds timeout) {
    if (view_held_)
        return {};   // must release previous view first

    const uint64_t rseq = header_->reader.read_seq.load(std::memory_order_relaxed);

    // Fast path: check cached write_seq.
    auto dataReady = [&]() -> bool {
        cached_write_seq_ = header_->writer.write_seq.load(std::memory_order_acquire);
        return cached_write_seq_ > rseq;
    };

    if (!dataReady()) {
        if (timeout.count() <= 0)
            return {};   // non-blocking

        // Adaptive wait.
        if (!waiter_->wait(dataReady, timeout))
            return {};
    }

    // Slot is (at least) Ready; wait for the state bit to confirm.
    const uint32_t idx = static_cast<uint32_t>(rseq & (slot_count_ - 1));
    auto* slot = static_cast<SlotHeader*>(
        const_cast<void*>(slotAt(header_, idx, slot_size_)));

    // Spin until the writer marks the slot as Ready (should be near-instant).
    while (slot->state.load(std::memory_order_acquire)
            != static_cast<uint32_t>(SlotState::Ready)) {
        _mm_pause();
    }

    slot->state.store(static_cast<uint32_t>(SlotState::Reading),
                      std::memory_order_relaxed);
    view_held_ = true;

    return ReadView{slotPayload(slot), slot->payload_size};
}

void ShmRingReader::releaseReadView() {
    if (!view_held_)
        return;

    const uint64_t rseq = header_->reader.read_seq.load(std::memory_order_relaxed);
    const uint32_t idx  = static_cast<uint32_t>(rseq & (slot_count_ - 1));
    auto* slot = static_cast<SlotHeader*>(
        const_cast<void*>(slotAt(header_, idx, slot_size_)));

    // Release: the writer may now reuse this slot.
    slot->state.store(static_cast<uint32_t>(SlotState::Free),
                      std::memory_order_release);
    header_->reader.read_seq.store(rseq + 1, std::memory_order_release);

    view_held_ = false;
}

uint32_t ShmRingReader::read(void* buffer, uint32_t max_size,
                             std::chrono::microseconds timeout) {
    auto view = acquireReadView(timeout);
    if (!view.data)
        return 0;

    const uint32_t copy_size = (view.size <= max_size) ? view.size : max_size;
    std::memcpy(buffer, view.data, copy_size);
    releaseReadView();
    return copy_size;
}

bool ShmRingReader::hasData() const {
    const uint64_t rseq = header_->reader.read_seq.load(std::memory_order_relaxed);
    const uint64_t wseq = header_->writer.write_seq.load(std::memory_order_acquire);
    return wseq > rseq;
}

} // namespace lux::communication::transport
