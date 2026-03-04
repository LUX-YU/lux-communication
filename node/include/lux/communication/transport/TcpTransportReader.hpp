#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <lux/communication/platform/NetSocket.hpp>
#include <lux/communication/transport/FrameHeader.hpp>
#include <lux/communication/transport/Handshake.hpp>
#include <lux/communication/visibility.h>

namespace lux::communication::transport {

/// TCP transport reader: connects to a remote Publisher, performs a handshake,
/// and receives message frames using a simple state machine.
class LUX_COMMUNICATION_PUBLIC TcpTransportReader {
public:
    /// @param remote_addr  Publisher's IP address.
    /// @param remote_port  Publisher's TCP port.
    /// @param topic_hash   Topic hash for the handshake.
    /// @param type_hash    Type hash for the handshake.
    /// @param local_pid    This subscriber's PID.
    /// @param hostname     This machine's hostname.
    TcpTransportReader(const std::string& remote_addr, uint16_t remote_port,
                       uint64_t topic_hash, uint64_t type_hash,
                       uint32_t local_pid, const std::string& hostname);
    ~TcpTransportReader();

    TcpTransportReader(TcpTransportReader&&) noexcept;
    TcpTransportReader& operator=(TcpTransportReader&&) noexcept;

    TcpTransportReader(const TcpTransportReader&)            = delete;
    TcpTransportReader& operator=(const TcpTransportReader&) = delete;

    /// Connect and perform the handshake.
    /// Returns true if the publisher accepted this subscriber.
    bool connect();

    /// Callback type for delivering a complete frame.
    using FrameCallback = std::function<void(const FrameHeader& hdr,
                                             const void* payload,
                                             uint32_t payload_size)>;

    /// Called by the Reactor when the TCP socket is readable.
    /// Buffers partial reads internally and delivers complete frames via @p cb.
    /// Control frames (Ping) are handled internally: a Pong reply is sent
    /// and the frame is NOT forwarded to @p cb.
    void onDataReady(FrameCallback cb);

    /// Non-blocking manual poll: tries a recv and feeds the state machine.
    /// Returns true if at least one frame was delivered to @p cb.
    bool pollOnce(FrameCallback cb);

    /// Get the native fd for Reactor registration.
    platform::socket_t nativeFd() const;

    bool isConnected() const { return connected_; }

    /// Check whether the connection has timed out (no data received for @p timeout).
    bool isTimedOut(std::chrono::milliseconds timeout) const;

    void close();

private:
    /// Send a Pong control frame back to the Publisher.
    void sendPong();

    enum class RecvState { ReadingHeader, ReadingPayload };

    platform::TcpSocket   sock_;
    std::string           remote_addr_;
    uint16_t              remote_port_;
    uint64_t              topic_hash_;
    uint64_t              type_hash_;
    uint32_t              local_pid_;
    std::string           hostname_;
    bool                  connected_ = false;

    RecvState             recv_state_  = RecvState::ReadingHeader;
    std::vector<uint8_t>  header_buf_;       // accumulates FrameHeader bytes
    std::vector<uint8_t>  payload_buf_;      // accumulates payload bytes
    FrameHeader           pending_hdr_{};

    /// Tracks the last time any data was received (for timeout detection).
    std::chrono::steady_clock::time_point last_recv_time_;
};

} // namespace lux::communication::transport
