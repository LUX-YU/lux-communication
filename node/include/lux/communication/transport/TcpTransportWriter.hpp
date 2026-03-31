#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <lux/communication/platform/NetSocket.hpp>
#include <lux/communication/transport/FrameHeader.hpp>
#include <lux/communication/transport/Handshake.hpp>
#include <lux/communication/visibility.h>

namespace lux::communication::transport
{
    /// TCP transport writer: listens for incoming subscriber connections,
    /// performs the handshake, and multicasts frames to all connected clients.
    class LUX_COMMUNICATION_PUBLIC TcpTransportWriter
    {
    public:
        /// @param bind_addr   Local address to listen on ("0.0.0.0" for all).
        /// @param bind_port   Local TCP port. 0 = OS-assigned.
        /// @param topic_hash  For handshake validation.
        /// @param type_hash   For type-safety check.
        /// Callback that returns the publisher's current sequence number.
        using SeqSupplier = std::function<uint64_t()>;

        TcpTransportWriter(const std::string &bind_addr, uint16_t bind_port,
                           uint64_t topic_hash, uint64_t type_hash);
        ~TcpTransportWriter();

        /// Set the sequence supplier for handshake responses.
        void setSeqSupplier(SeqSupplier fn) { seq_supplier_ = std::move(fn); }

        TcpTransportWriter(TcpTransportWriter &&) noexcept;
        TcpTransportWriter &operator=(TcpTransportWriter &&) noexcept;

        TcpTransportWriter(const TcpTransportWriter &) = delete;
        TcpTransportWriter &operator=(const TcpTransportWriter &) = delete;

        /// Start listening for subscriber connections.
        bool startListening();

        /// Called when the listen socket has a pending connection (e.g. from Reactor).
        /// Performs accept + handshake.
        void onAcceptReady();

        /// Send a frame to all connected subscribers.
        /// @return Number of subscribers that successfully received the data.
        uint32_t send(const FrameHeader &hdr, const void *payload, uint32_t payload_size);

        /// Access the listen socket fd (for Reactor registration).
        platform::socket_t listenFd() const;

        /// Get the actual listening port (useful when bind_port was 0).
        uint16_t listeningPort() const;

        /// Number of active connections.
        size_t connectionCount() const;

        /// Remove a connection identified by its native fd.
        void removeConnection(platform::socket_t fd);

        // ── Heartbeat (Ping / Pong) ──

        /// Send a Ping control frame to every connection.
        void sendPingAll();

        /// Non-blocking: try to recv Pong frames from all connections.
        /// Updates each connection's last_pong_time when a Pong is received.
        void recvPongAll();

        /// Remove connections whose last_pong_time exceeds @p timeout.
        /// @return Number of connections removed.
        size_t gcDeadConnections(std::chrono::milliseconds timeout);

        void close();

    private:
        struct Connection
        {
            platform::TcpSocket sock;
            uint32_t subscriber_pid = 0;
            std::string hostname;
            std::chrono::steady_clock::time_point last_pong_time;
        };

        platform::TcpListener listener_;
        uint64_t topic_hash_;
        uint64_t type_hash_;
        std::string bind_addr_;
        uint16_t bind_port_;
        std::vector<std::unique_ptr<Connection>> connections_;
        mutable std::mutex conn_mutex_;
        SeqSupplier seq_supplier_;
    };

} // namespace lux::communication::transport
