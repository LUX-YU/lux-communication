#pragma once
#include <cstdint>
#include <functional>
#include <lux/communication/QoSProfile.hpp>

namespace lux::communication {

/// Hint to force a specific transport for a subscriber.
enum class SubscribeTransportHint : uint8_t {
    Auto      = 0,   ///< Framework selects the optimal transport per peer.
    IntraOnly = 1,   ///< Only same-process publishers.
    ShmOnly   = 2,   ///< Only same-machine SHM publishers.
    NetOnly   = 3,   ///< Only remote publishers via network.
};

/// Options when creating a unified Subscriber.
struct SubscribeOptions {
    // ── Transport hint ──
    SubscribeTransportHint transport_hint = SubscribeTransportHint::Auto;

    // ── QoS (Phase 6) ──
    QoSProfile qos{};

    /// Called when qos.deadline > 0 and no message arrives within the deadline.
    std::function<void()> on_deadline_missed;
};

} // namespace lux::communication
