#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <chrono>

#include <lux/communication/platform/NetSocket.hpp>
#include <lux/communication/visibility.h>

namespace lux::communication::transport {

/// Cross-platform IO reactor.
///
/// Linux: epoll     Windows: select (initially)
///
/// Monitors multiple socket fds and dispatches callbacks when events fire.
/// Designed to run on a single dedicated IO thread via `run()`, or polled
/// manually with `pollOnce()`.
class LUX_COMMUNICATION_PUBLIC IoReactor {
public:
    IoReactor();
    ~IoReactor();

    IoReactor(const IoReactor&)            = delete;
    IoReactor& operator=(const IoReactor&) = delete;

    /// Event types (bitmask).
    enum EventType : uint8_t {
        Readable = 1,
        Writable = 2,
        Error    = 4,
    };

    using EventCallback = std::function<void(platform::socket_t fd, uint8_t events)>;

    /// Register a socket fd to monitor.
    /// @param fd       The socket handle.
    /// @param events   Bitmask of EventType.
    /// @param callback Invoked when events fire.
    bool addFd(platform::socket_t fd, uint8_t events, EventCallback callback);

    /// Modify watched events for an already-registered fd.
    bool modifyFd(platform::socket_t fd, uint8_t events);

    /// Unregister a previously registered fd.
    bool removeFd(platform::socket_t fd);

    /// Run one round of event polling.
    /// @param timeout  Maximum wait time.
    /// @return Number of events dispatched.
    int pollOnce(std::chrono::milliseconds timeout);

    /// Run the event loop until stop() is called.
    void run();

    /// Signal the event loop to exit (thread-safe).
    void stop();

    /// Wake up a blocked pollOnce() / run() from another thread.
    void wakeup();

    /// Is the reactor currently running?
    bool isRunning() const;

    /// Number of registered fds (excluding internal wakeup fd).
    size_t fdCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lux::communication::transport
