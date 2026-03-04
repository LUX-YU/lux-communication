#pragma once
/// TokenBucket — token-bucket rate limiter for Publisher bandwidth control.
///
/// Thread-safety: the mutex protects refill + consume atomically.
/// Publisher::publish() is typically called from a single thread,
/// but the mutex is kept for safety.

#include <cstddef>
#include <chrono>
#include <mutex>
#include <algorithm>
#include <thread>

#include <lux/communication/visibility.h>

namespace lux::communication {

class LUX_COMMUNICATION_PUBLIC TokenBucket {
public:
    /// @param rate   Token refill rate (bytes/sec).
    /// @param burst  Bucket capacity (max burst bytes).  0 = use rate (1 s burst).
    TokenBucket(size_t rate, size_t burst = 0);

    /// Try to consume @p bytes tokens without blocking.
    /// @return true if tokens available and consumed; false otherwise.
    bool tryConsume(size_t bytes);

    /// Block until enough tokens are available, then consume.
    void waitAndConsume(size_t bytes);

    size_t rate()  const { return rate_; }
    size_t burst() const { return burst_; }

private:
    void refill();

    size_t rate_;
    size_t burst_;
    double tokens_;
    std::chrono::steady_clock::time_point last_refill_;
    std::mutex mutex_;
};

} // namespace lux::communication
