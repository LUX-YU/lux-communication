#include "lux/communication/transport/TcpTransportReader.hpp"
#include "lux/communication/transport/NetConstants.hpp"

#include <chrono>
#include <cstring>

namespace lux::communication::transport {

TcpTransportReader::TcpTransportReader(const std::string& remote_addr, uint16_t remote_port,
                                       uint64_t topic_hash, uint64_t type_hash,
                                       uint32_t local_pid, const std::string& hostname)
    : remote_addr_(remote_addr), remote_port_(remote_port),
      topic_hash_(topic_hash), type_hash_(type_hash),
      local_pid_(local_pid), hostname_(hostname),
      last_recv_time_(std::chrono::steady_clock::now())
{
    header_buf_.reserve(sizeof(FrameHeader));
}

TcpTransportReader::~TcpTransportReader() { close(); }

TcpTransportReader::TcpTransportReader(TcpTransportReader&&) noexcept            = default;
TcpTransportReader& TcpTransportReader::operator=(TcpTransportReader&&) noexcept  = default;

bool TcpTransportReader::connect() {
    if (!sock_.connect(remote_addr_, remote_port_))
        return false;

    sock_.setNoDelay(true);
    sock_.setSendBufferSize(kTcpBufferSize);
    sock_.setRecvBufferSize(kTcpBufferSize);

    // ── Send handshake request ──
    HandshakeRequest req{};
    req.topic_hash     = topic_hash_;
    req.type_hash      = type_hash_;
    req.subscriber_pid = local_pid_;
    req.setHostname(hostname_.c_str());

    if (!sock_.sendAll(&req, sizeof(req))) {
        sock_.close();
        return false;
    }

    // ── Receive handshake response ──
    HandshakeResponse resp{};
    if (!sock_.recvAll(&resp, sizeof(resp))) {
        sock_.close();
        return false;
    }
    if (resp.magic != kHandshakeMagic || resp.accepted != 1) {
        sock_.close();
        return false;
    }

    // Switch to non-blocking for polling after handshake
    sock_.setNonBlocking(true);

    connected_      = true;
    last_recv_time_ = std::chrono::steady_clock::now();
    return true;
}

void TcpTransportReader::onDataReady(FrameCallback cb) {
    // Read available data and feed the state machine; may invoke cb 0..N times.
    uint8_t tmp[8192];
    while (true) {
        int n = sock_.recv(tmp, sizeof(tmp));
        if (n <= 0) {
            if (n == 0) {
                // Peer closed
                connected_ = false;
            }
            break;
        }

        // Any successful recv updates the liveness timestamp.
        last_recv_time_ = std::chrono::steady_clock::now();

        size_t pos = 0;
        while (pos < static_cast<size_t>(n)) {
            if (recv_state_ == RecvState::ReadingHeader) {
                size_t need = sizeof(FrameHeader) - header_buf_.size();
                size_t avail = static_cast<size_t>(n) - pos;
                size_t take = std::min(need, avail);
                header_buf_.insert(header_buf_.end(), tmp + pos, tmp + pos + take);
                pos += take;

                if (header_buf_.size() == sizeof(FrameHeader)) {
                    std::memcpy(&pending_hdr_, header_buf_.data(), sizeof(FrameHeader));
                    header_buf_.clear();

                    if (!isValidFrame(pending_hdr_)) {
                        // Bad header — close
                        connected_ = false;
                        return;
                    }

                    // ── Heartbeat: Ping → auto-reply Pong, skip callback ──
                    if (isPing(pending_hdr_)) {
                        sendPong();
                        // Ping is always payload_size == 0, stay in ReadingHeader.
                        continue;
                    }

                    // ── Ignore stray Pong frames (shouldn't happen on Reader) ──
                    if (isPong(pending_hdr_)) {
                        continue;
                    }

                    if (pending_hdr_.payload_size == 0) {
                        // Zero-payload message
                        if (cb) cb(pending_hdr_, nullptr, 0);
                        // Stay in ReadingHeader
                    } else {
                        payload_buf_.clear();
                        payload_buf_.reserve(pending_hdr_.payload_size);
                        recv_state_ = RecvState::ReadingPayload;
                    }
                }
            } else {
                // ReadingPayload
                size_t need = pending_hdr_.payload_size - payload_buf_.size();
                size_t avail = static_cast<size_t>(n) - pos;
                size_t take = std::min(need, avail);
                payload_buf_.insert(payload_buf_.end(), tmp + pos, tmp + pos + take);
                pos += take;

                if (payload_buf_.size() == pending_hdr_.payload_size) {
                    if (cb) cb(pending_hdr_, payload_buf_.data(), pending_hdr_.payload_size);
                    payload_buf_.clear();
                    recv_state_ = RecvState::ReadingHeader;
                }
            }
        }
    }
}

bool TcpTransportReader::pollOnce(FrameCallback cb) {
    bool delivered = false;
    onDataReady([&](const FrameHeader& hdr, const void* payload, uint32_t sz) {
        delivered = true;
        if (cb) cb(hdr, payload, sz);
    });
    return delivered;
}

platform::socket_t TcpTransportReader::nativeFd() const {
    return sock_.nativeFd();
}

void TcpTransportReader::sendPong() {
    FrameHeader pong = makeControlFrame(kFlagPong);
    // Best-effort: if the send fails we'll be GC'd by timeout anyway.
    sock_.sendAll(&pong, sizeof(pong));
}

bool TcpTransportReader::isTimedOut(std::chrono::milliseconds timeout) const {
    auto elapsed = std::chrono::steady_clock::now() - last_recv_time_;
    return elapsed > timeout;
}

void TcpTransportReader::close() {
    sock_.close();
    connected_ = false;
}

} // namespace lux::communication::transport
