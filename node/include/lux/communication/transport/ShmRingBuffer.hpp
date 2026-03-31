#pragma once
#include <atomic>
#include <cstdint>
#include <cstddef>

namespace lux::communication::transport
{
    static constexpr uint32_t kRingMagic = 0x4C555852; // "LUXR"
    static constexpr uint32_t kRingVersion = 1;

    // ──── Cache-line-aligned header sections ────

    /// Writer-owned cache line (cache line 0).
    struct alignas(64) RingWriterLine
    {
        uint32_t magic = 0;
        uint32_t version = 0;
        uint32_t slot_count = 0;            // must be power of 2
        uint32_t slot_size = 0;             // bytes per slot (including SlotHeader)
        std::atomic<uint64_t> write_seq{0}; // next sequence to write
        uint32_t writer_pid = 0;
        uint32_t pad_[3] = {};
    };
    static_assert(sizeof(RingWriterLine) == 64);

    /// Reader-owned cache line (cache line 1).
    struct alignas(64) RingReaderLine
    {
        std::atomic<uint64_t> read_seq{0}; // next sequence to read
        uint32_t reader_pid = 0;
        uint32_t pad_[13] = {};
    };
    static_assert(sizeof(RingReaderLine) == 64);

    /// Notification cache line (cache line 2).
    struct alignas(64) NotifyBlock
    {
        std::atomic<uint32_t> futex_word{0}; // Linux: futex; Windows: ignored
        char event_name[60] = {};            // Windows: Named Event name
    };
    static_assert(sizeof(NotifyBlock) == 64);

    /// The full ring header occupying the first 192 bytes of SHM.
    struct RingHeader
    {
        RingWriterLine writer; // offset 0x00
        RingReaderLine reader; // offset 0x40
        NotifyBlock notify;    // offset 0x80
    };
    static_assert(sizeof(RingHeader) == 192);
    static_assert(alignof(RingHeader) == 64);

    // ──── Per-slot header ────

    enum class SlotState : uint32_t
    {
        Free = 0,
        Writing = 1,
        Ready = 2,
        Reading = 3,
    };

    struct SlotHeader
    {
        std::atomic<uint32_t> state{0}; // SlotState
        uint32_t payload_size = 0;      // FrameHeader + serialized data
        uint64_t reserved = 0;
    };
    static_assert(sizeof(SlotHeader) == 16);

    // ──── Helper functions ────

    /// Total SHM bytes needed for a ring with the given parameters.
    inline size_t ringTotalSize(uint32_t slot_count, uint32_t slot_size)
    {
        return sizeof(RingHeader) + static_cast<size_t>(slot_count) * static_cast<size_t>(slot_size);
    }

    /// Pointer to slot[index] (SlotHeader + payload area).
    inline void *slotAt(void *base, uint32_t index, uint32_t slot_size)
    {
        return static_cast<char *>(base) + sizeof(RingHeader) + static_cast<size_t>(index) * slot_size;
    }
    inline const void *slotAt(const void *base, uint32_t index, uint32_t slot_size)
    {
        return static_cast<const char *>(base) + sizeof(RingHeader) + static_cast<size_t>(index) * slot_size;
    }

    /// Pointer to the payload region inside a slot (skip SlotHeader).
    inline void *slotPayload(void *slot)
    {
        return static_cast<char *>(slot) + sizeof(SlotHeader);
    }
    inline const void *slotPayload(const void *slot)
    {
        return static_cast<const char *>(slot) + sizeof(SlotHeader);
    }

    /// Maximum payload bytes that fit in one slot.
    inline uint32_t maxSlotPayload(uint32_t slot_size)
    {
        return slot_size - static_cast<uint32_t>(sizeof(SlotHeader));
    }

    /// Check that slot_count is a power of two.
    inline bool isPowerOf2(uint32_t n)
    {
        return n != 0 && (n & (n - 1)) == 0;
    }

} // namespace lux::communication::transport
