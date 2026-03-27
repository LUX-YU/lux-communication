#include "lux/communication/transport/TcpTransportWriter.hpp"
#include "lux/communication/transport/NetConstants.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace lux::communication::transport {

TcpTransportWriter::TcpTransportWriter(const std::string& bind_addr, uint16_t bind_port,
                                       uint64_t topic_hash, uint64_t type_hash)
    : topic_hash_(topic_hash), type_hash_(type_hash),
      bind_addr_(bind_addr), bind_port_(bind_port)
{}

TcpTransportWriter::~TcpTransportWriter() { close(); }

TcpTransportWriter::TcpTransportWriter(TcpTransportWriter&& o) noexcept
    : topic_hash_(o.topic_hash_),
      type_hash_(o.type_hash_),
      bind_addr_(std::move(o.bind_addr_)),
      bind_port_(o.bind_port_),
      listener_(std::move(o.listener_)),
      connections_(std::move(o.connections_))
{}

TcpTransportWriter& TcpTransportWriter::operator=(TcpTransportWriter&& o) noexcept {
    if (this != &o) {
        close();
        topic_hash_   = o.topic_hash_;
        type_hash_    = o.type_hash_;
        bind_addr_    = std::move(o.bind_addr_);
        bind_port_    = o.bind_port_;
        listener_     = std::move(o.listener_);
        connections_  = std::move(o.connections_);
    }
    return *this;
}

bool TcpTransportWriter::startListening() {
    if (!listener_.listen(bind_addr_, bind_port_))
        return false;
    // Keep the listener in blocking mode initially; the Reactor will set
    // non-blocking when it registers the fd.
    return true;
}

void TcpTransportWriter::onAcceptReady() {
    std::string remote_addr;
    uint16_t    remote_port = 0;
    auto client = listener_.accept(remote_addr, remote_port);
    if (!client.isValid()) return;

    // Set TCP_NODELAY for low latency
    client.setNoDelay(true);
    client.setSendBufferSize(kTcpBufferSize);
    client.setRecvBufferSize(kTcpBufferSize);

    // ── Read handshake request ──
    HandshakeRequest req{};
    if (!client.recvAll(&req, sizeof(req))) {
        client.close();
        return;
    }
    if (req.magic != kHandshakeMagic) {
        client.close();
        return;
    }

    // ── Validate ──
    HandshakeResponse resp{};
    resp.magic   = kHandshakeMagic;
    resp.version = 1;

    if (req.topic_hash != topic_hash_) {
        resp.accepted      = 0;
        resp.reject_reason = static_cast<uint8_t>(HandshakeRejectReason::TopicNotFound);
        client.sendAll(&resp, sizeof(resp));
        client.close();
        return;
    }
    if (req.type_hash != type_hash_) {
        resp.accepted      = 0;
        resp.reject_reason = static_cast<uint8_t>(HandshakeRejectReason::TypeMismatch);
        client.sendAll(&resp, sizeof(resp));
        client.close();
        return;
    }

    resp.accepted      = 1;
    resp.reject_reason = 0;
    resp.publisher_seq = seq_supplier_ ? seq_supplier_() : 0;

    if (!client.sendAll(&resp, sizeof(resp))) {
        client.close();
        return;
    }

    // ── Register connection ──
    auto conn = std::make_unique<Connection>();
    conn->sock           = std::move(client);
    conn->subscriber_pid = req.subscriber_pid;
    conn->hostname       = std::string(req.hostname);
    conn->last_pong_time = std::chrono::steady_clock::now();

    // The subscriber may send Pong frames back; make the socket non-blocking
    // so recvPongAll() won't block.
    conn->sock.setNonBlocking(true);

    std::lock_guard lock(conn_mutex_);
    connections_.push_back(std::move(conn));
}

uint32_t TcpTransportWriter::send(const FrameHeader& hdr,
                                   const void* payload, uint32_t payload_size)
{
    std::lock_guard lock(conn_mutex_);
    uint32_t ok_count = 0;

    for (auto it = connections_.begin(); it != connections_.end(); ) {
        auto& conn = *it;
        platform::IoVec iov[2] = {
            { &hdr, sizeof(FrameHeader) },
            { payload, payload_size }
        };
        int sent = conn->sock.sendV(iov, 2);
        if (sent < 0) {
            // Connection broken — remove
            it = connections_.erase(it);
        } else {
            // For large payloads, sendV may be partial.  Do a sendAll fallback.
            auto total = sizeof(FrameHeader) + payload_size;
            if (static_cast<size_t>(sent) < total) {
                // The scatter-gather only sent partial data.
                // Re-send using blocking sendAll for the remainder is complex;
                // for now we'll build a contiguous buffer.
                size_t remaining = total - sent;
                // Build contiguous buffer
                std::vector<uint8_t> buf(total);
                std::memcpy(buf.data(), &hdr, sizeof(FrameHeader));
                if (payload_size > 0)
                    std::memcpy(buf.data() + sizeof(FrameHeader), payload, payload_size);
                if (!conn->sock.sendAll(buf.data() + sent, remaining)) {
                    it = connections_.erase(it);
                    continue;
                }
            }
            ++ok_count;
            ++it;
        }
    }
    return ok_count;
}

platform::socket_t TcpTransportWriter::listenFd() const {
    return listener_.nativeFd();
}

uint16_t TcpTransportWriter::listeningPort() const {
    return listener_.localPort();
}

size_t TcpTransportWriter::connectionCount() const {
    std::lock_guard lock(conn_mutex_);
    return connections_.size();
}

void TcpTransportWriter::removeConnection(platform::socket_t fd) {
    std::lock_guard lock(conn_mutex_);
    connections_.erase(
        std::remove_if(connections_.begin(), connections_.end(),
            [fd](const auto& c) { return c->sock.nativeFd() == fd; }),
        connections_.end());
}

// ════════════════════════════════════════════════════════════════════
//  Heartbeat  (Ping / Pong)
// ════════════════════════════════════════════════════════════════════

void TcpTransportWriter::sendPingAll() {
    FrameHeader ping = makeControlFrame(kFlagPing);

    std::lock_guard lock(conn_mutex_);
    for (auto it = connections_.begin(); it != connections_.end(); ) {
        if (!it->get()->sock.sendAll(&ping, sizeof(ping))) {
            // Send failed — connection broken, remove.
            it = connections_.erase(it);
        } else {
            ++it;
        }
    }
}

void TcpTransportWriter::recvPongAll() {
    std::lock_guard lock(conn_mutex_);
    for (auto& conn : connections_) {
        // Non-blocking recv: try to read FrameHeader-sized chunks.
        FrameHeader hdr{};
        while (true) {
            int n = conn->sock.recv(&hdr, sizeof(hdr));
            if (n <= 0) break;   // EWOULDBLOCK or error
            if (n == sizeof(FrameHeader) && isValidFrame(hdr) && isPong(hdr)) {
                conn->last_pong_time = std::chrono::steady_clock::now();
            }
            // Any other partial/garbage data is discarded — the Publisher
            // socket should only ever receive Pong frames from the Subscriber.
        }
    }
}

size_t TcpTransportWriter::gcDeadConnections(std::chrono::milliseconds timeout) {
    auto now = std::chrono::steady_clock::now();

    std::lock_guard lock(conn_mutex_);
    size_t before = connections_.size();
    connections_.erase(
        std::remove_if(connections_.begin(), connections_.end(),
            [&](const auto& c) {
                return (now - c->last_pong_time) > timeout;
            }),
        connections_.end());
    return before - connections_.size();
}

void TcpTransportWriter::close() {
    {
        std::lock_guard lock(conn_mutex_);
        connections_.clear();
    }
    listener_.close();
}

} // namespace lux::communication::transport
