#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include <lux/communication/platform/NetSocket.hpp>
#include <lux/communication/transport/FrameHeader.hpp>
#include <lux/communication/transport/FragmentSender.hpp>
#include <lux/communication/visibility.h>

namespace lux::communication::transport {

/// Sends message frames over UDP unicast.
///
/// Small messages (FrameHeader + payload ≤ kMaxUdpPayload) are sent as a single
/// datagram using scatter-gather.  Larger messages are fragmented via
/// FragmentSender.
class LUX_COMMUNICATION_PUBLIC UdpTransportWriter {
public:
    /// @param dest_addr  Remote subscriber IP address.
    /// @param dest_port  Remote subscriber UDP port.
    UdpTransportWriter(const std::string& dest_addr, uint16_t dest_port);
    ~UdpTransportWriter();

    UdpTransportWriter(UdpTransportWriter&&) noexcept;
    UdpTransportWriter& operator=(UdpTransportWriter&&) noexcept;

    UdpTransportWriter(const UdpTransportWriter&)            = delete;
    UdpTransportWriter& operator=(const UdpTransportWriter&) = delete;

    /// Send a complete message frame.
    /// Returns true on success.
    bool send(const FrameHeader& hdr, const void* payload, uint32_t payload_size);

    /// Direct raw send (for custom protocols).
    int sendRaw(const void* data, size_t len);

    const std::string& destAddr() const { return dest_addr_; }
    uint16_t           destPort() const { return dest_port_; }

    void close();

private:
    platform::UdpSocket              sock_;
    std::string                      dest_addr_;
    uint16_t                         dest_port_;
    std::unique_ptr<FragmentSender>  frag_sender_;
    std::atomic<uint32_t>            next_group_id_{0};
};

} // namespace lux::communication::transport
