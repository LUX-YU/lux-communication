#pragma once
#include <cstdint>

namespace lux::communication {

/// Options when creating a unified Node.
struct NodeOptions {
    bool enable_discovery    = true;   ///< Start DiscoveryService on construction.
    bool enable_shm          = true;   ///< Allow SHM transport.
    bool enable_net          = true;   ///< Allow Network transport.
    bool enable_intra        = true;   ///< Allow same-process transport.

    // ── IO thread tuning ──
    uint32_t shm_poll_interval_us = 100;   ///< SHM reader poll interval (microseconds).
    uint32_t reactor_timeout_ms   = 10;    ///< IoReactor pollOnce timeout (milliseconds).

    // ── Discovery heartbeat (multicast, cross-machine) ──
    uint32_t discovery_heartbeat_interval_ms = 2000;  ///< Send interval (ms).  0 = disabled.
    uint32_t discovery_heartbeat_timeout_ms  = 6000;  ///< Remote GC threshold (ms).

    // ── TCP heartbeat (Ping/Pong, per-connection) ──
    uint32_t tcp_ping_interval_ms = 1000;  ///< Publisher sends Ping every N ms.  0 = disabled.
    uint32_t tcp_ping_timeout_ms  = 3000;  ///< Drop connection if no Pong within N ms.
};

} // namespace lux::communication
