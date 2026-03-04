#pragma once
/// Predefined QoS profiles for common use-cases.

#include <lux/communication/QoSProfile.hpp>

namespace lux::communication::QoSProfiles {

/// Sensor data: only care about the latest value; drop is acceptable.
inline constexpr QoSProfile SensorData {
    .reliability    = Reliability::BestEffort,
    .history        = History::KeepLast,
    .depth          = 1,
    .lifespan       = std::chrono::milliseconds{0},
    .deadline       = std::chrono::milliseconds{0},
    .latency_budget = std::chrono::microseconds{1000},  // 1 ms
    .bandwidth_limit = 0,
};

/// Reliable command: guaranteed delivery, keep all messages.
inline constexpr QoSProfile ReliableCommand {
    .reliability    = Reliability::Reliable,
    .history        = History::KeepAll,
    .depth          = 100,
    .lifespan       = std::chrono::milliseconds{0},
    .deadline       = std::chrono::milliseconds{0},
    .latency_budget = std::chrono::microseconds{0},
    .bandwidth_limit = 0,
};

/// Large data transfer: reliable, keep few in queue.
inline constexpr QoSProfile LargeTransfer {
    .reliability    = Reliability::Reliable,
    .history        = History::KeepLast,
    .depth          = 2,
    .lifespan       = std::chrono::milliseconds{0},
    .deadline       = std::chrono::milliseconds{0},
    .latency_budget = std::chrono::microseconds{10000},  // 10 ms
    .bandwidth_limit = 0,
};

/// Keep only the latest value (BestEffort).
inline constexpr QoSProfile KeepLatest {
    .reliability    = Reliability::BestEffort,
    .history        = History::KeepLast,
    .depth          = 1,
    .lifespan       = std::chrono::milliseconds{0},
    .deadline       = std::chrono::milliseconds{0},
    .latency_budget = std::chrono::microseconds{0},
    .bandwidth_limit = 0,
};

/// Real-time control: BestEffort + deadline + short lifespan.
inline constexpr QoSProfile RealtimeControl {
    .reliability    = Reliability::BestEffort,
    .history        = History::KeepLast,
    .depth          = 1,
    .lifespan       = std::chrono::milliseconds{50},
    .deadline       = std::chrono::milliseconds{10},
    .latency_budget = std::chrono::microseconds{500},
    .bandwidth_limit = 0,
};

} // namespace lux::communication::QoSProfiles
