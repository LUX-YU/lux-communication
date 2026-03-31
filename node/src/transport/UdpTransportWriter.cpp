#include "lux/communication/transport/UdpTransportWriter.hpp"
#include "lux/communication/transport/NetConstants.hpp"

#include <cstring>
#include <vector>

namespace lux::communication::transport
{
    UdpTransportWriter::UdpTransportWriter(const std::string &dest_addr, uint16_t dest_port)
        : dest_addr_(dest_addr), dest_port_(dest_port)
    {
        frag_sender_ = std::make_unique<FragmentSender>(sock_);
    }

    UdpTransportWriter::~UdpTransportWriter() { close(); }

    UdpTransportWriter::UdpTransportWriter(UdpTransportWriter &&o) noexcept
        : sock_(std::move(o.sock_)),
          dest_addr_(std::move(o.dest_addr_)),
          dest_port_(o.dest_port_),
          frag_sender_(std::move(o.frag_sender_)),
          next_group_id_(o.next_group_id_.load(std::memory_order_relaxed))
    {
    }

    UdpTransportWriter &UdpTransportWriter::operator=(UdpTransportWriter &&o) noexcept
    {
        if (this != &o)
        {
            close();
            sock_ = std::move(o.sock_);
            dest_addr_ = std::move(o.dest_addr_);
            dest_port_ = o.dest_port_;
            frag_sender_ = std::move(o.frag_sender_);
            next_group_id_.store(o.next_group_id_.load(std::memory_order_relaxed),
                                 std::memory_order_relaxed);
        }
        return *this;
    }

    bool UdpTransportWriter::send(const FrameHeader &hdr,
                                  const void *payload, uint32_t payload_size)
    {
        const size_t total = sizeof(FrameHeader) + payload_size;

        if (total <= kMaxUdpPayload)
        {
            // ── Fast path: single datagram via scatter-gather ──
            platform::IoVec iov[2] = {
                {&hdr, sizeof(FrameHeader)},
                {payload, payload_size}};
            int n = sock_.sendToV(iov, 2, dest_addr_, dest_port_);
            return n >= 0;
        }

        // ── Slow path: fragmentation ──
        // Build a contiguous buffer of FrameHeader + payload
        std::vector<uint8_t> buf(total);
        std::memcpy(buf.data(), &hdr, sizeof(FrameHeader));
        if (payload_size > 0)
            std::memcpy(buf.data() + sizeof(FrameHeader), payload, payload_size);

        uint32_t gid = next_group_id_.fetch_add(1, std::memory_order_relaxed);
        return frag_sender_->send(gid, buf.data(), total, hdr.topic_hash,
                                  dest_addr_, dest_port_);
    }

    int UdpTransportWriter::sendRaw(const void *data, size_t len)
    {
        return sock_.sendTo(data, len, dest_addr_, dest_port_);
    }

    void UdpTransportWriter::close()
    {
        sock_.close();
    }

} // namespace lux::communication::transport
