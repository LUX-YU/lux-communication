#include "lux/communication/transport/FragmentAssembler.hpp"
#include "lux/communication/transport/NetConstants.hpp"

#include <algorithm>
#include <cstring>

namespace lux::communication::transport
{
    FragmentAssembler::FragmentAssembler(std::chrono::milliseconds timeout)
        : timeout_(timeout) {}

    FragmentAssembler::~FragmentAssembler() = default;

    std::optional<FragmentAssembler::CompleteMessage>
    FragmentAssembler::feed(const FragmentHeader &fh, const void *payload, size_t len)
    {
        if (fh.frag_magic != kFragmentMagic)
            return std::nullopt;
        if (fh.seq_in_group >= fh.total_fragments)
            return std::nullopt;
        if (fh.total_msg_size > kMaxFragmentedMsgSize)
            return std::nullopt;

        GroupKey key{fh.topic_hash, fh.group_id};
        auto it = groups_.find(key);

        if (it == groups_.end())
        {
            // First fragment of a new group
            FragmentGroup grp;
            grp.total_fragments = fh.total_fragments;
            grp.total_msg_size = fh.total_msg_size;
            grp.topic_hash = fh.topic_hash;
            grp.received_count = 0;
            grp.buffer.resize(fh.total_msg_size, 0);
            grp.received.resize(fh.total_fragments, false);
            grp.created_at = std::chrono::steady_clock::now();
            auto [ins, _] = groups_.emplace(key, std::move(grp));
            it = ins;
        }

        auto &grp = it->second;

        // Validate consistency
        if (fh.total_fragments != grp.total_fragments ||
            fh.total_msg_size != grp.total_msg_size)
        {
            return std::nullopt; // inconsistent header — drop
        }

        // Check for duplicate
        if (grp.received[fh.seq_in_group])
        {
            ++stat_duplicates_;
            return std::nullopt;
        }

        // Copy fragment payload into the correct position
        size_t offset = static_cast<size_t>(fh.seq_in_group) * kMaxFragPayload;
        size_t copy_len = std::min<size_t>(len, grp.buffer.size() - offset);
        std::memcpy(grp.buffer.data() + offset, payload, copy_len);

        grp.received[fh.seq_in_group] = true;
        ++grp.received_count;

        // Check if complete
        if (grp.received_count == grp.total_fragments)
        {
            CompleteMessage msg;
            msg.data = std::move(grp.buffer);
            msg.topic_hash = grp.topic_hash;
            groups_.erase(it);
            ++stat_complete_;
            return msg;
        }

        return std::nullopt;
    }

    void FragmentAssembler::gc()
    {
        auto now = std::chrono::steady_clock::now();
        for (auto it = groups_.begin(); it != groups_.end();)
        {
            if (now - it->second.created_at > timeout_)
            {
                ++stat_timed_out_;
                it = groups_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    FragmentAssembler::Stats FragmentAssembler::stats() const
    {
        return Stats{
            stat_complete_,
            stat_timed_out_,
            stat_duplicates_,
            groups_.size()};
    }

    void FragmentAssembler::reset()
    {
        groups_.clear();
        stat_complete_ = 0;
        stat_timed_out_ = 0;
        stat_duplicates_ = 0;
    }

} // namespace lux::communication::transport
