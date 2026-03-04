#pragma once
#include <cstdint>
#include <chrono>
#include <lux/communication/QoSProfile.hpp>

namespace lux::communication {

/// Hint to force a specific transport for a publisher.
enum class PublishTransportHint : uint8_t {
    Auto      = 0,   ///< Framework selects the optimal transport per peer.
    IntraOnly = 1,   ///< Only same-process subscribers.
    ShmOnly   = 2,   ///< Only same-machine SHM subscribers.
    NetOnly   = 3,   ///< Only remote subscribers via network.
};

/// Options when creating a unified Publisher.
struct PublishOptions {
    // ── SHM ring options ──
    uint32_t shm_ring_slot_count = 16;
    uint32_t shm_ring_slot_size  = 1024 * 1024;       // 1 MB
    uint64_t shm_pool_capacity   = 64 * 1024 * 1024;  // 64 MB
    bool     shm_huge_pages      = false;

    // ── Network options ──
    uint16_t net_udp_port        = 0;   // 0 = auto-bind
    uint16_t net_tcp_port        = 0;   // 0 = auto-bind
    uint32_t net_large_threshold = 64 * 1024; // > 64 KB → prefer TCP

    // ── Transport hint ──
    PublishTransportHint transport_hint = PublishTransportHint::Auto;

    // ── QoS (Phase 6) ──
    QoSProfile qos{};

    /// SHM Reliable mode timeout (spin-wait ceiling when ring is full).
    std::chrono::milliseconds shm_reliable_timeout{10};
};

} // namespace lux::communication
