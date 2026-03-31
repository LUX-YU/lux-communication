#pragma once

/// ShmDataPool — shared-memory data pool for large message 1:N transmission.
///
/// The pool is a single SHM segment with a variable-size free-list allocator.
/// Publisher allocates a block, serialises data once, sets ref_count = N,
/// and writes PoolDescriptors into each subscriber's ring.
/// Each subscriber reads from the pool and decrements ref_count;
/// the last reader returns the block to the free-list.

#include <lux/communication/platform/SharedMemory.hpp>
#include <lux/communication/visibility.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace lux::communication::transport
{
    // ──── Constants ────

    static constexpr uint32_t kPoolMagic = 0x4C555850; // "LUXP"
    static constexpr uint32_t kPoolVersion = 1;

    /// Messages larger than this threshold (serialised size) use the data pool path
    /// when there are multiple subscribers.
    static constexpr uint32_t kPoolThreshold = 64 * 1024; // 64 KB

    // ──── Block states ────

    enum class BlockState : uint32_t
    {
        Free = 0,
        Allocated = 1,
    };

    // ──── Structures ────

    struct alignas(64) PoolHeader
    {
        uint32_t magic;
        uint32_t version;
        uint64_t capacity; // usable bytes (excluding PoolHeader)
        uint32_t min_block_size;
        uint32_t pad0_;
        std::atomic<uint64_t> free_head; // offset to first free block (0 = end)
        std::atomic<uint64_t> allocated_bytes;
        char pad_[24];
    };
    static_assert(sizeof(PoolHeader) == 64);

    struct alignas(16) BlockHeader
    {
        std::atomic<uint32_t> state; // BlockState
        std::atomic<uint32_t> ref_count;
        uint32_t data_size;  // actual payload bytes
        uint32_t total_size; // sizeof(BlockHeader) + payload_capacity (aligned)
        uint64_t next_free;  // offset of next free block (when Free)
        uint64_t pad_;
    };
    static_assert(sizeof(BlockHeader) == 32);

    /// Descriptor stored in a ring slot when the actual data resides in ShmDataPool.
    struct PoolDescriptor
    {
        uint64_t pool_offset;      ///< Byte offset of payload from pool SHM base
        uint32_t data_size;        ///< Serialised payload size
        uint32_t ref_count_offset; ///< Byte offset of BlockHeader::ref_count from pool SHM base
    };
    static_assert(sizeof(PoolDescriptor) == 16);

    // ──── ShmDataPool class ────

    class LUX_COMMUNICATION_PUBLIC ShmDataPool
    {
    public:
        /// Create a new pool (Publisher side).
        /// @param shm_name   Logical SHM name (platform-adjusted internally).
        /// @param capacity   Total pool capacity in bytes (excluding PoolHeader).
        /// @param min_block  Minimum block size (alignment granularity).
        /// @param use_huge   Request huge pages (graceful fallback if unavailable).
        ShmDataPool(const std::string &shm_name, uint64_t capacity,
                    uint32_t min_block = 4096, bool use_huge = false);

        /// Open an existing pool (Subscriber side).
        /// @param shm_name  Logical SHM name (must match publisher's).
        static std::unique_ptr<ShmDataPool> openExisting(const std::string &shm_name);

        ~ShmDataPool();

        ShmDataPool(const ShmDataPool &) = delete;
        ShmDataPool &operator=(const ShmDataPool &) = delete;

        // ──── Publisher API ────

        struct AllocResult
        {
            void *payload;             ///< Writable pointer to block payload area
            uint64_t pool_offset;      ///< Offset of payload from SHM base
            uint64_t ref_count_offset; ///< Offset of ref_count from SHM base
        };

        /// Allocate a block of at least @p size bytes. ref_count is set to @p ref_count.
        /// @return result with payload==nullptr if pool is full.
        AllocResult allocate(uint32_t size, uint32_t ref_count);

        // ──── Subscriber API ────

        /// Get a read-only pointer to the payload at @p pool_offset.
        const void *read(uint64_t pool_offset) const;

        /// Decrement the reference count at @p ref_count_offset.
        /// If it reaches 0, the block is returned to the free-list.
        void release(uint64_t ref_count_offset);

        // ──── Info ────

        uint64_t capacity() const;
        uint64_t allocatedBytes() const;
        const std::string &shmName() const { return shm_name_; }

    private:
        /// Constructor for openExisting() path.
        ShmDataPool() = default;

        void initFreeList();
        void returnToFreeList(uint64_t block_offset);

        /// Align size up to 16-byte boundary.
        static uint32_t alignUp(uint32_t size, uint32_t alignment)
        {
            return (size + alignment - 1) & ~(alignment - 1);
        }

        platform::SharedMemorySegment *shm_ = nullptr;
        PoolHeader *header_ = nullptr;
        char *base_ = nullptr; // == shm_->data()
        std::string shm_name_;
        bool is_creator_ = false;
    };

} // namespace lux::communication::transport
