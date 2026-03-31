#pragma once
#include <cstdint>

namespace lux::communication::transport
{
    /// Magic number for fragment headers — "LUXG".
    static constexpr uint32_t kFragmentMagic = 0x4C555847;

    /// Header prepended to each UDP fragment.
    ///
    /// Layout (24 bytes):
    ///  ┌───────────────┬─────────────┬────────────────┐
    ///  │ frag_magic 4B │ group_id  4B│ seq_in_group 2B│
    ///  │ total_frags 2B│ total_sz  4B│                │
    ///  │ topic_hash 8B                               │
    ///  └───────────────┴─────────────┴────────────────┘
    struct FragmentHeader
    {
        uint32_t frag_magic;      ///< kFragmentMagic — distinguishes from FrameHeader
        uint32_t group_id;        ///< Unique per fragmented message (monotonic per writer)
        uint16_t seq_in_group;    ///< 0-based fragment index
        uint16_t total_fragments; ///< Total number of fragments in this group
        uint32_t total_msg_size;  ///< Total (FrameHeader + payload) size in bytes
        uint64_t topic_hash;      ///< Topic hash for routing / disambiguation
    };

    static_assert(sizeof(FragmentHeader) == 24, "FragmentHeader must be 24 bytes");

    /// Quick check for a valid fragment header.
    inline bool isFragment(const void *data, size_t len)
    {
        if (len < sizeof(FragmentHeader))
            return false;
        auto magic = *static_cast<const uint32_t *>(data);
        return magic == kFragmentMagic;
    }

} // namespace lux::communication::transport
