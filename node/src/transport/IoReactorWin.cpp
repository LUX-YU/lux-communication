#include "lux/communication/transport/IoReactor.hpp"

#include <atomic>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <algorithm>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

namespace lux::communication::transport {

// ═════════════════════════════════════════════════════════════════════════════
// Windows IoReactor implementation using select()
// ═════════════════════════════════════════════════════════════════════════════

struct IoReactor::Impl {
    struct FdEntry {
        platform::socket_t fd;
        uint8_t            events;
        EventCallback      callback;
    };

    std::unordered_map<platform::socket_t, FdEntry> fds_;
    std::mutex                                      mutex_;
    std::atomic<bool>                               running_{false};

    // Wakeup mechanism: a self-connected UDP loopback socket pair.
    SOCKET wakeup_send_ = INVALID_SOCKET;
    SOCKET wakeup_recv_ = INVALID_SOCKET;

    Impl() {
        initWakeup();
    }

    ~Impl() {
        if (wakeup_send_ != INVALID_SOCKET) ::closesocket(wakeup_send_);
        if (wakeup_recv_ != INVALID_SOCKET) ::closesocket(wakeup_recv_);
    }

    void initWakeup() {
        // Create a UDP loopback socket pair for wakeup signalling.
        wakeup_recv_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        wakeup_send_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (wakeup_recv_ == INVALID_SOCKET || wakeup_send_ == INVALID_SOCKET) return;

        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port        = 0; // OS-assigned

        ::bind(wakeup_recv_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

        // Retrieve assigned port
        int len = sizeof(addr);
        ::getsockname(wakeup_recv_, reinterpret_cast<sockaddr*>(&addr), &len);

        // Connect the send socket to the recv socket's address
        ::connect(wakeup_send_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

        // Make recv non-blocking
        u_long mode = 1;
        ::ioctlsocket(wakeup_recv_, FIONBIO, &mode);
    }

    void doWakeup() {
        char c = 'W';
        ::send(wakeup_send_, &c, 1, 0);
    }

    void drainWakeup() {
        char buf[64];
        while (::recv(wakeup_recv_, buf, sizeof(buf), 0) > 0) {}
    }

    bool addFd(platform::socket_t fd, uint8_t events, EventCallback cb) {
        std::lock_guard lock(mutex_);
        fds_[fd] = FdEntry{fd, events, std::move(cb)};
        return true;
    }

    bool modifyFd(platform::socket_t fd, uint8_t events) {
        std::lock_guard lock(mutex_);
        auto it = fds_.find(fd);
        if (it == fds_.end()) return false;
        it->second.events = events;
        return true;
    }

    bool removeFd(platform::socket_t fd) {
        std::lock_guard lock(mutex_);
        return fds_.erase(fd) > 0;
    }

    int pollOnce(std::chrono::milliseconds timeout) {
        // Snapshot fds under lock
        std::vector<FdEntry> snapshot;
        {
            std::lock_guard lock(mutex_);
            snapshot.reserve(fds_.size());
            for (auto& [_, entry] : fds_)
                snapshot.push_back(entry);
        }

        fd_set read_set, write_set, error_set;
        FD_ZERO(&read_set);
        FD_ZERO(&write_set);
        FD_ZERO(&error_set);

        // Always add wakeup fd
        FD_SET(wakeup_recv_, &read_set);

        for (auto& e : snapshot) {
            SOCKET s = static_cast<SOCKET>(e.fd);
            if (e.events & IoReactor::Readable) FD_SET(s, &read_set);
            if (e.events & IoReactor::Writable) FD_SET(s, &write_set);
            FD_SET(s, &error_set);
        }

        timeval tv{};
        tv.tv_sec  = static_cast<long>(timeout.count() / 1000);
        tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);

        int nready = ::select(0 /* ignored on Windows */, &read_set, &write_set, &error_set, &tv);
        if (nready <= 0) return 0;

        // Drain wakeup
        if (FD_ISSET(wakeup_recv_, &read_set)) {
            drainWakeup();
            --nready;
        }

        int dispatched = 0;
        for (auto& e : snapshot) {
            SOCKET s = static_cast<SOCKET>(e.fd);
            uint8_t fired = 0;
            if (FD_ISSET(s, &read_set))  fired |= IoReactor::Readable;
            if (FD_ISSET(s, &write_set)) fired |= IoReactor::Writable;
            if (FD_ISSET(s, &error_set)) fired |= IoReactor::Error;
            if (fired && e.callback) {
                e.callback(e.fd, fired);
                ++dispatched;
            }
        }
        return dispatched;
    }

    void run() {
        running_.store(true, std::memory_order_release);
        while (running_.load(std::memory_order_acquire)) {
            pollOnce(std::chrono::milliseconds{100});
        }
    }

    void stop() {
        running_.store(false, std::memory_order_release);
        doWakeup();
    }
};

// ── IoReactor forwarding ────────────────────────────────────────────────────

IoReactor::IoReactor()  : impl_(std::make_unique<Impl>()) {}
IoReactor::~IoReactor() { stop(); }

bool IoReactor::addFd(platform::socket_t fd, uint8_t events, EventCallback cb) {
    return impl_->addFd(fd, events, std::move(cb));
}

bool IoReactor::modifyFd(platform::socket_t fd, uint8_t events) {
    return impl_->modifyFd(fd, events);
}

bool IoReactor::removeFd(platform::socket_t fd) {
    return impl_->removeFd(fd);
}

int IoReactor::pollOnce(std::chrono::milliseconds timeout) {
    return impl_->pollOnce(timeout);
}

void IoReactor::run() { impl_->run(); }

void IoReactor::stop() {
    if (impl_) impl_->stop();
}

void IoReactor::wakeup() {
    if (impl_) impl_->doWakeup();
}

bool IoReactor::isRunning() const {
    return impl_ && impl_->running_.load(std::memory_order_acquire);
}

size_t IoReactor::fdCount() const {
    if (!impl_) return 0;
    std::lock_guard lock(impl_->mutex_);
    return impl_->fds_.size();
}

} // namespace lux::communication::transport
