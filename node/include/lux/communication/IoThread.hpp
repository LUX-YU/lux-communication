#pragma once
/// IoThread — per-Node IO thread that replaces per-subscriber recv threads.
///
/// Responsibilities:
///   1. Poll SHM readers of all registered subscribers.
///   2. Drive the IoReactor for network sockets.
///
/// This consolidates O(subscriber_count) threads into O(1).

#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <functional>
#include <chrono>

#include <lux/communication/NodeOptions.hpp>
#include <lux/communication/transport/IoReactor.hpp>
#include <lux/communication/visibility.h>

namespace lux::communication {

/// Type-erased poll callback.
/// Each unified Subscriber registers a function that polls its SHM readers
/// and enqueues any received messages.
using ShmPollFn = std::function<void()>;

class LUX_COMMUNICATION_PUBLIC IoThread {
public:
    explicit IoThread(transport::IoReactor& reactor, const NodeOptions& opts = {});
    ~IoThread();

    IoThread(const IoThread&)            = delete;
    IoThread& operator=(const IoThread&) = delete;

    /// Register a poll callback (returns a handle for unregistering).
    uint64_t registerPoller(ShmPollFn fn);

    /// Unregister a previously registered poller.
    void unregisterPoller(uint64_t handle);

    /// Start the IO thread.
    void start();

    /// Stop the IO thread and join.
    void stop();

    bool isRunning() const { return running_.load(std::memory_order_relaxed); }

private:
    void ioLoop();

    transport::IoReactor& reactor_;
    NodeOptions opts_;

    struct PollEntry {
        uint64_t   handle;
        ShmPollFn  fn;
    };

    std::mutex              poll_mutex_;
    std::vector<PollEntry>  poll_entries_;
    uint64_t                next_handle_{1};

    std::thread       io_thread_;
    std::atomic<bool> running_{false};
};

} // namespace lux::communication
