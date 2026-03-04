#pragma once
#include <cstdint>
#include <cstring>

namespace lux::communication::discovery {

// ──────── Constants ────────

static constexpr uint32_t    kDiscoveryMagic = 0x4C584450; // "LXDP"
static constexpr uint16_t    kDiscoveryPort  = 30000;
static constexpr const char* kDiscoveryGroup = "239.255.0.1";

// ──────── Packet type ────────

enum class PacketType : uint8_t {
    Announce  = 1,   // Publisher/Subscriber is available
    Withdraw  = 2,   // Publisher/Subscriber is going away
    Query     = 3,   // Request: who has this topic?
    Reply     = 4,   // Response to a Query
    Heartbeat = 5,   // Periodic liveness beacon (cross-machine)
};

// ──────── Wire format (512 bytes, fits in one UDP datagram) ────────

#pragma pack(push, 1)
struct DiscoveryPacket {
    uint32_t magic           = kDiscoveryMagic;
    uint8_t  version         = 1;
    uint8_t  type            = 0;      // PacketType
    uint8_t  role            = 0;      // EndpointRole (1=Pub, 2=Sub)
    uint8_t  reserved1       = 0;

    uint64_t domain_id       = 0;
    uint64_t topic_name_hash = 0;
    uint64_t type_hash       = 0;

    uint32_t pid             = 0;
    uint32_t reserved2       = 0;

    char     topic_name[240]   = {};
    char     net_endpoint[128] = {};
    char     hostname[64]      = {};

    uint64_t timestamp_ns    = 0;

    char     padding[32]     = {};     // pad to 512 bytes
};
#pragma pack(pop)

static_assert(sizeof(DiscoveryPacket) == 512, "DiscoveryPacket must be exactly 512 bytes");

/// Quick validation of a received packet.
inline bool isValidPacket(const DiscoveryPacket& pkt) {
    return pkt.magic == kDiscoveryMagic && pkt.version == 1;
}

} // namespace lux::communication::discovery
