#pragma once
#include <cstdint>
#include <chrono>
#include <optional>
#include <unordered_map>
#include <vector>

#include <lux/communication/transport/FragmentHeader.hpp>
#include <lux/communication/visibility.h>

namespace lux::communication::transport
{
    /// Reassembles fragmented UDP messages from individual datagrams.
    ///
    /// Thread-safety: **not** thread-safe. Intended to be called from a single
    /// IO thread (the Reactor thread).
    class LUX_COMMUNICATION_PUBLIC FragmentAssembler
    {
    public:
        /// @param timeout  Max time to wait for all fragments of a group.
        explicit FragmentAssembler(
            std::chrono::milliseconds timeout = std::chrono::milliseconds{200});

        ~FragmentAssembler();

        /// A fully reassembled message (FrameHeader + serialized payload).
        struct CompleteMessage
        {
            std::vector<uint8_t> data;
            uint64_t topic_hash;
        };

        /// Feed one received fragment.
        /// If this fragment completes a group, the assembled message is returned.
        /// Otherwise returns std::nullopt.
        std::optional<CompleteMessage> feed(const FragmentHeader &frag_hdr,
                                            const void *payload, size_t len);

        /// Garbage-collect incomplete groups that have exceeded the timeout.
        /// Should be called periodically (e.g. every 100 ms).
        void gc();

        /// Statistics.
        struct Stats
        {
            uint64_t complete_messages = 0;
            uint64_t timed_out_groups = 0;
            uint64_t duplicate_fragments = 0;
            uint64_t pending_groups = 0;
        };
        Stats stats() const;

        /// Reset all state (for testing).
        void reset();

    private:
        struct FragmentGroup
        {
            std::vector<uint8_t> buffer; // pre-allocated to total_msg_size
            std::vector<bool> received;  // which fragments have arrived
            uint16_t total_fragments = 0;
            uint16_t received_count = 0;
            uint32_t total_msg_size = 0;
            uint64_t topic_hash = 0;
            std::chrono::steady_clock::time_point created_at;
        };

        struct GroupKey
        {
            uint64_t topic_hash;
            uint32_t group_id;
            bool operator==(const GroupKey &) const = default;
        };

        struct GroupKeyHash
        {
            size_t operator()(const GroupKey &k) const
            {
                return std::hash<uint64_t>{}(k.topic_hash) ^ (std::hash<uint32_t>{}(k.group_id) << 1);
            }
        };

        std::unordered_map<GroupKey, FragmentGroup, GroupKeyHash> groups_;
        std::chrono::milliseconds timeout_;

        uint64_t stat_complete_ = 0;
        uint64_t stat_timed_out_ = 0;
        uint64_t stat_duplicates_ = 0;
    };

} // namespace lux::communication::transport
