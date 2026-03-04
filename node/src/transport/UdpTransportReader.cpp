#include "lux/communication/transport/UdpTransportReader.hpp"
#include "lux/communication/transport/NetConstants.hpp"
#include "lux/communication/transport/FragmentHeader.hpp"

#include <cstring>

namespace lux::communication::transport {

UdpTransportReader::UdpTransportReader(uint16_t bind_port)
    : recv_buf_(kMaxUdpPayload + 64)  // a little extra for safety
{
    sock_.setReuseAddr(true);
    sock_.bindAny(bind_port);
    sock_.setNonBlocking(true);
    sock_.setRecvBufferSize(kUdpRecvBufferSize);
}

UdpTransportReader::~UdpTransportReader() { close(); }

UdpTransportReader::UdpTransportReader(UdpTransportReader&&) noexcept            = default;
UdpTransportReader& UdpTransportReader::operator=(UdpTransportReader&&) noexcept  = default;

bool UdpTransportReader::pollOnce(FrameCallback cb) {
    if (!sock_.isValid()) return false;

    std::string src_addr;
    uint16_t    src_port = 0;
    int n = sock_.recvFrom(recv_buf_.data(), recv_buf_.size(), src_addr, src_port);
    if (n <= 0) return false;

    const auto recv_len = static_cast<size_t>(n);
    const auto* raw = recv_buf_.data();

    // ── Check if this is a fragment ──
    if (recv_len >= sizeof(FragmentHeader) && isFragment(raw, recv_len)) {
        FragmentHeader fh;
        std::memcpy(&fh, raw, sizeof(fh));
        const void* frag_payload = raw + sizeof(FragmentHeader);
        size_t frag_payload_len  = recv_len - sizeof(FragmentHeader);

        auto result = assembler_.feed(fh, frag_payload, frag_payload_len);
        if (result && cb) {
            // Reassembled: data contains FrameHeader + payload
            if (result->data.size() >= sizeof(FrameHeader)) {
                FrameHeader hdr;
                std::memcpy(&hdr, result->data.data(), sizeof(FrameHeader));
                const void* payload = result->data.data() + sizeof(FrameHeader);
                uint32_t payload_sz = static_cast<uint32_t>(
                    result->data.size() - sizeof(FrameHeader));

                // Mark as reassembled (bit 6)
                hdr.flags |= 0x40;  // kFlagReassembled

                cb(hdr, payload, payload_sz);
            }
        }
        return true;
    }

    // ── Single-datagram frame ──
    if (recv_len >= sizeof(FrameHeader)) {
        FrameHeader hdr;
        std::memcpy(&hdr, raw, sizeof(FrameHeader));
        if (isValidFrame(hdr) && cb) {
            const void* payload = raw + sizeof(FrameHeader);
            uint32_t payload_sz = static_cast<uint32_t>(recv_len - sizeof(FrameHeader));
            cb(hdr, payload, payload_sz);
        }
        return true;
    }

    return true;  // received something but too small to be a valid frame
}

void UdpTransportReader::gc() {
    assembler_.gc();
}

void UdpTransportReader::close() {
    sock_.close();
}

} // namespace lux::communication::transport
