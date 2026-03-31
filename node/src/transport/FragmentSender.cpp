#include "lux/communication/transport/FragmentSender.hpp"
#include "lux/communication/transport/FragmentHeader.hpp"
#include "lux/communication/transport/NetConstants.hpp"

#include <algorithm>
#include <cstring>

namespace lux::communication::transport
{
    FragmentSender::FragmentSender(platform::UdpSocket &sock) : sock_(sock) {}

    bool FragmentSender::send(uint32_t group_id,
                              const void *data, size_t len,
                              uint64_t topic_hash,
                              const std::string &dest_addr, uint16_t dest_port)
    {
        if (len == 0 || len > kMaxFragmentedMsgSize)
            return false;

        const auto total_frags = static_cast<uint16_t>(
            (len + kMaxFragPayload - 1) / kMaxFragPayload);
        const auto *src = static_cast<const uint8_t *>(data);
        size_t offset = 0;

        for (uint16_t i = 0; i < total_frags; ++i)
        {
            const size_t remaining = len - offset;
            const auto frag_size = static_cast<uint16_t>(
                std::min<size_t>(remaining, kMaxFragPayload));

            FragmentHeader fh{};
            fh.frag_magic = kFragmentMagic;
            fh.group_id = group_id;
            fh.seq_in_group = i;
            fh.total_fragments = total_frags;
            fh.total_msg_size = static_cast<uint32_t>(len);
            fh.topic_hash = topic_hash;

            // Scatter-gather: FragmentHeader + fragment payload
            platform::IoVec iov[2] = {
                {&fh, sizeof(fh)},
                {src + offset, frag_size}};
            int sent = sock_.sendToV(iov, 2, dest_addr, dest_port);
            if (sent < 0)
                return false;

            offset += frag_size;
        }
        return true;
    }

} // namespace lux::communication::transport
