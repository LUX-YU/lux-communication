#include <lux/communication/transport/ShmDataPool.hpp>
#include <lux/communication/platform/PlatformDefs.hpp>

#include <cassert>
#include <cstring>
#include <stdexcept>
#include <new>

namespace lux::communication::transport
{
    ShmDataPool::ShmDataPool(const std::string &shm_name, uint64_t capacity,
                             uint32_t min_block, bool use_huge)
        : shm_name_(shm_name), is_creator_(true)
    {
        if (capacity < sizeof(BlockHeader) + min_block)
            throw std::invalid_argument("ShmDataPool: capacity too small");

        const size_t total = sizeof(PoolHeader) + capacity;
        const std::string platform_name = platform::shmPlatformName(shm_name);
        const auto huge_opt = use_huge ? platform::HugePageOption::TryHuge
                                       : platform::HugePageOption::None;

        shm_ = platform::SharedMemorySegment::open(platform_name, total, /*create=*/true, huge_opt);
        if (!shm_)
            throw std::runtime_error("ShmDataPool: failed to create SHM segment: " + shm_name);

        base_ = static_cast<char *>(shm_->data());
        header_ = reinterpret_cast<PoolHeader *>(base_);

        header_->magic = kPoolMagic;
        header_->version = kPoolVersion;
        header_->capacity = capacity;
        header_->min_block_size = min_block;
        header_->pad0_ = 0;
        header_->allocated_bytes.store(0, std::memory_order_relaxed);

        initFreeList();
    }

    std::unique_ptr<ShmDataPool> ShmDataPool::openExisting(const std::string &shm_name)
    {
        const std::string platform_name = platform::shmPlatformName(shm_name);

        // First open just to read the header and discover the real size.
        auto *shm_probe = platform::SharedMemorySegment::open(
            platform_name, sizeof(PoolHeader), /*create=*/false);
        if (!shm_probe)
            throw std::runtime_error("ShmDataPool::openExisting: cannot open SHM: " + shm_name);

        auto *probe_hdr = static_cast<const PoolHeader *>(shm_probe->data());
        if (probe_hdr->magic != kPoolMagic || probe_hdr->version != kPoolVersion)
        {
            delete shm_probe;
            throw std::runtime_error("ShmDataPool::openExisting: invalid pool header");
        }
        const size_t total = sizeof(PoolHeader) + probe_hdr->capacity;
        delete shm_probe;

        // Re-open with full size.
        auto *shm_full = platform::SharedMemorySegment::open(platform_name, total, /*create=*/false);
        if (!shm_full)
            throw std::runtime_error("ShmDataPool::openExisting: failed to remap full pool");

        auto pool = std::unique_ptr<ShmDataPool>(new ShmDataPool());
        pool->shm_ = shm_full;
        pool->base_ = static_cast<char *>(shm_full->data());
        pool->header_ = reinterpret_cast<PoolHeader *>(pool->base_);
        pool->shm_name_ = shm_name;
        pool->is_creator_ = false;
        return pool;
    }

    ShmDataPool::~ShmDataPool()
    {
        if (shm_)
        {
            if (is_creator_)
                shm_->unlink();
            delete shm_;
            shm_ = nullptr;
        }
    }

    void ShmDataPool::initFreeList()
    {
        // The entire capacity region starts as one big free block.
        const uint64_t block_offset = sizeof(PoolHeader);
        auto *block = reinterpret_cast<BlockHeader *>(base_ + block_offset);

        block->state.store(static_cast<uint32_t>(BlockState::Free),
                           std::memory_order_relaxed);
        block->ref_count.store(0, std::memory_order_relaxed);
        block->data_size = 0;
        block->total_size = static_cast<uint32_t>(header_->capacity);
        block->next_free = 0; // 0 = end of list
        block->pad_ = 0;

        header_->free_head.store(block_offset, std::memory_order_release);
    }

    ShmDataPool::AllocResult ShmDataPool::allocate(uint32_t size, uint32_t ref_count)
    {
        const uint32_t needed = alignUp(
            static_cast<uint32_t>(sizeof(BlockHeader)) + size, 16);
        const uint32_t min_split = static_cast<uint32_t>(sizeof(BlockHeader)) + header_->min_block_size;

        // Walk free-list (single-writer, no CAS needed for traversal).
        uint64_t prev_offset = 0; // 0 means head
        uint64_t cur_offset = header_->free_head.load(std::memory_order_acquire);

        while (cur_offset != 0)
        {
            auto *block = reinterpret_cast<BlockHeader *>(base_ + cur_offset);

            if (block->total_size >= needed)
            {
                // ── Found a fit ──

                // Check if we can split.
                const uint32_t remainder = block->total_size - needed;
                if (remainder >= min_split)
                {
                    // Split: create a new free block after the allocated portion.
                    uint64_t new_free_offset = cur_offset + needed;
                    auto *new_free = reinterpret_cast<BlockHeader *>(base_ + new_free_offset);
                    new_free->state.store(static_cast<uint32_t>(BlockState::Free),
                                          std::memory_order_relaxed);
                    new_free->ref_count.store(0, std::memory_order_relaxed);
                    new_free->data_size = 0;
                    new_free->total_size = remainder;
                    new_free->next_free = block->next_free;
                    new_free->pad_ = 0;

                    block->total_size = needed;

                    // Update linked list to replace current block with new_free.
                    if (prev_offset == 0)
                    {
                        header_->free_head.store(new_free_offset, std::memory_order_release);
                    }
                    else
                    {
                        auto *prev = reinterpret_cast<BlockHeader *>(base_ + prev_offset);
                        prev->next_free = new_free_offset;
                    }
                }
                else
                {
                    // No split — remove entire block from free-list.
                    if (prev_offset == 0)
                    {
                        header_->free_head.store(block->next_free, std::memory_order_release);
                    }
                    else
                    {
                        auto *prev = reinterpret_cast<BlockHeader *>(base_ + prev_offset);
                        prev->next_free = block->next_free;
                    }
                }

                // Initialise block as Allocated.
                block->state.store(static_cast<uint32_t>(BlockState::Allocated),
                                   std::memory_order_relaxed);
                block->ref_count.store(ref_count, std::memory_order_release);
                block->data_size = size;
                block->next_free = 0;

                header_->allocated_bytes.fetch_add(block->total_size, std::memory_order_relaxed);

                // Payload starts right after BlockHeader.
                char *payload = base_ + cur_offset + sizeof(BlockHeader);
                const uint64_t payload_offset = cur_offset + sizeof(BlockHeader);
                const uint64_t rc_offset = cur_offset + offsetof(BlockHeader, ref_count);

                return AllocResult{payload, payload_offset, rc_offset};
            }

            prev_offset = cur_offset;
            cur_offset = block->next_free;
        }

        // Pool is full.
        return AllocResult{nullptr, 0, 0};
    }

    const void *ShmDataPool::read(uint64_t pool_offset) const
    {
        if (pool_offset == 0 || pool_offset >= sizeof(PoolHeader) + header_->capacity)
            return nullptr;
        return base_ + pool_offset;
    }

    void ShmDataPool::release(uint64_t ref_count_offset)
    {
        auto *rc = reinterpret_cast<std::atomic<uint32_t> *>(base_ + ref_count_offset);
        const uint32_t old = rc->fetch_sub(1, std::memory_order_acq_rel);
        if (old == 1)
        {
            // Last reader — return block to free-list.
            // BlockHeader starts at (ref_count_offset - offsetof(BlockHeader, ref_count)).
            const uint64_t block_offset = ref_count_offset - offsetof(BlockHeader, ref_count);
            auto *block = reinterpret_cast<BlockHeader *>(base_ + block_offset);

            header_->allocated_bytes.fetch_sub(block->total_size, std::memory_order_relaxed);

            returnToFreeList(block_offset);
        }
    }

    void ShmDataPool::returnToFreeList(uint64_t block_offset)
    {
        auto *block = reinterpret_cast<BlockHeader *>(base_ + block_offset);
        block->state.store(static_cast<uint32_t>(BlockState::Free),
                           std::memory_order_relaxed);
        block->data_size = 0;

        // CAS head-insert (multi-process safe).
        uint64_t old_head = header_->free_head.load(std::memory_order_acquire);
        do
        {
            block->next_free = old_head;
        } while (!header_->free_head.compare_exchange_weak(
            old_head, block_offset,
            std::memory_order_release, std::memory_order_acquire));
    }

    uint64_t ShmDataPool::capacity() const
    {
        return header_ ? header_->capacity : 0;
    }

    uint64_t ShmDataPool::allocatedBytes() const
    {
        return header_ ? header_->allocated_bytes.load(std::memory_order_relaxed) : 0;
    }

} // namespace lux::communication::transport
