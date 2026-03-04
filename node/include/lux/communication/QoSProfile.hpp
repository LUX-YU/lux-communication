#pragma once
/// QoS (Quality of Service) profile for Publisher and Subscriber.
///
/// Default values (BestEffort + KeepAll + lifespan(0)) produce zero
/// additional overhead compared to pre-QoS behaviour — all new checks
/// are constant comparisons that the branch predictor will skip.

#include <cstdint>
#include <cstddef>
#include <chrono>

namespace lux::communication {

/// Reliability strategy.
enum class Reliability : uint8_t {
    BestEffort = 0,  ///< Fire-and-forget, no delivery guarantee (default).
    Reliable   = 1,  ///< Guaranteed delivery: SHM spin-wait / Net forces TCP.
};

/// History strategy.
enum class History : uint8_t {
    KeepAll  = 0,   ///< Unbounded queue — keep every message (default).
    KeepLast = 1,   ///< Keep only the most recent N messages; discard oldest.
};

/// QoS policy set — used by both Publisher and Subscriber.
struct QoSProfile {
    // ── Reliability ──
    Reliability reliability = Reliability::BestEffort;

    // ── History depth ──
    History history = History::KeepAll;
    size_t  depth   = 0;   ///< KeepLast maximum queue depth (0 = unlimited).

    // ── Message lifespan ──
    /// Maximum time from publish to consume.  Messages older than this are
    /// discarded at dequeue time.  0 = never expires.
    std::chrono::milliseconds lifespan{0};

    // ── Deadline ──
    /// Maximum expected interval between messages.  If exceeded, the
    /// on_deadline_missed callback fires.  0 = no deadline monitoring.
    std::chrono::milliseconds deadline{0};

    // ── Latency budget ──
    /// Hint for TransportSelector aggressiveness (e.g. < 1 ms → prefer
    /// spin-wait over event-wait).  0 = no special preference.
    std::chrono::microseconds latency_budget{0};

    // ── Bandwidth limit ──
    /// Publisher send-rate cap in bytes/sec.  Enforced via token bucket.
    /// 0 = unlimited.
    size_t bandwidth_limit = 0;
};

} // namespace lux::communication
