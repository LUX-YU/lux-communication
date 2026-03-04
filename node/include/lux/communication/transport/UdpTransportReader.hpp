#pragma once
#include <cstdint>
#include <chrono>
#include <functional>
#include <vector>

#include <lux/communication/platform/NetSocket.hpp>
#include <lux/communication/transport/FrameHeader.hpp>
#include <lux/communication/transport/FragmentAssembler.hpp>
#include <lux/communication/visibility.h>

namespace lux::communication::transport {

/// Receives message frames over UDP.
///
/// Handles both single-datagram messages and fragmented messages
/// (via the internal FragmentAssembler).
class LUX_COMMUNICATION_PUBLIC UdpTransportReader {
public:
    /// @param bind_port  Local port to bind.  0 = OS-assigned.
    explicit UdpTransportReader(uint16_t bind_port = 0);
    ~UdpTransportReader();

    UdpTransportReader(UdpTransportReader&&) noexcept;
    UdpTransportReader& operator=(UdpTransportReader&&) noexcept;

    UdpTransportReader(const UdpTransportReader&)            = delete;
    UdpTransportReader& operator=(const UdpTransportReader&) = delete;

    /// Callback type for delivering a complete frame.
    using FrameCallback = std::function<void(const FrameHeader& hdr,
                                             const void* payload,
                                             uint32_t payload_size)>;

    /// Attempt to receive and process one datagram (non-blocking).
    /// If a complete frame is available, invokes @p cb.
    /// Returns true if a datagram was received (may or may not yield a full frame).
    bool pollOnce(FrameCallback cb);

    /// Run fragment GC (call periodically, e.g. every 100 ms).
    void gc();

    /// Get native fd for Reactor registration.
    platform::socket_t nativeFd() const { return sock_.nativeFd(); }

    /// Get the locally-assigned port.
    uint16_t localPort() const { return sock_.localPort(); }

    bool isValid() const { return sock_.isValid(); }
    void close();

    /// Access the underlying assembler stats.
    FragmentAssembler::Stats assemblerStats() const { return assembler_.stats(); }

private:
    platform::UdpSocket   sock_;
    FragmentAssembler     assembler_;
    std::vector<uint8_t>  recv_buf_;   // scratch buffer for incoming datagrams
};

} // namespace lux::communication::transport
