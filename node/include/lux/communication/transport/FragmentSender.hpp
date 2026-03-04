#pragma once
#include <cstdint>
#include <string>
#include <lux/communication/platform/NetSocket.hpp>
#include <lux/communication/visibility.h>

namespace lux::communication::transport {

/// Splits a contiguous message (FrameHeader + payload) into fragments
/// and sends each fragment as a separate UDP datagram.
class LUX_COMMUNICATION_PUBLIC FragmentSender {
public:
    /// @param sock  Reference to a UdpSocket (must outlive this sender).
    explicit FragmentSender(platform::UdpSocket& sock);

    /// Fragment and send a message to (dest_addr, dest_port).
    ///
    /// @param group_id    Caller-provided unique fragment group ID.
    /// @param data        Contiguous buffer = FrameHeader + serialized payload.
    /// @param len         Total bytes.
    /// @param topic_hash  Placed in each FragmentHeader for routing.
    /// @param dest_addr   Remote IPv4 address.
    /// @param dest_port   Remote UDP port.
    /// @return true if all fragments were sent successfully.
    bool send(uint32_t group_id,
              const void* data, size_t len,
              uint64_t topic_hash,
              const std::string& dest_addr, uint16_t dest_port);

private:
    platform::UdpSocket& sock_;
};

} // namespace lux::communication::transport
