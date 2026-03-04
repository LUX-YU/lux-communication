#pragma once
#include <cstdint>
#include <cstring>

namespace lux::communication::transport {

/// Magic number for handshake messages — "LUXH".
static constexpr uint32_t kHandshakeMagic = 0x4C555848;

/// Handshake request: Subscriber → Publisher (sent immediately after TCP connect).
struct HandshakeRequest {
    uint32_t magic          = kHandshakeMagic;
    uint32_t version        = 1;
    uint64_t topic_hash     = 0;
    uint64_t type_hash      = 0;
    uint32_t subscriber_pid = 0;
    char     hostname[60]   = {};

    void setHostname(const char* name) {
        std::strncpy(hostname, name, sizeof(hostname) - 1);
        hostname[sizeof(hostname) - 1] = '\0';
    }
};
static_assert(sizeof(HandshakeRequest) == 88, "HandshakeRequest must be 88 bytes");

/// Handshake response: Publisher → Subscriber.
struct HandshakeResponse {
    uint32_t magic          = kHandshakeMagic;
    uint32_t version        = 1;
    uint8_t  accepted       = 0;   ///< 1 = accepted, 0 = rejected
    uint8_t  reject_reason  = 0;   ///< See HandshakeRejectReason
    uint16_t reserved       = 0;
    uint64_t publisher_seq  = 0;   ///< Current publisher sequence number
};
static_assert(sizeof(HandshakeResponse) == 24, "HandshakeResponse must be 24 bytes");

enum class HandshakeRejectReason : uint8_t {
    Ok             = 0,
    TypeMismatch   = 1,
    TopicNotFound  = 2,
    CapacityFull   = 3,
};

} // namespace lux::communication::transport
