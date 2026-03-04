#include "lux/communication/transport/IoReactor.hpp"

#include <atomic>
#include <unordered_map>
#include <vector>
#include <mutex>

#include <unistd.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>

namespace lux::communication::transport {

// ═════════════════════════════════════════════════════════════════════════════
// Linux IoReactor implementation using epoll + eventfd
// ═════════════════════════════════════════════════════════════════════════════

struct IoReactor::Impl {
    struct FdEntry {
        platform::socket_t fd;
        uint8_t            events;
        EventCallback      callback;
    };

    int epfd_      = -1;
    int wakeup_fd_ = -1;

    std::unordered_map<platform::socket_t, FdEntry> fds_;
    std::mutex                                      mutex_;
    std::atomic<bool>                               running_{false};

    Impl() {
        epfd_ = ::epoll_create1(0);
        wakeup_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);

        // Register wakeup fd with epoll
        epoll_event ev{};
        ev.events  = EPOLLIN;
        ev.data.fd = wakeup_fd_;
        ::epoll_ctl(epfd_, EPOLL_CTL_ADD, wakeup_fd_, &ev);
    }

    ~Impl() {
        if (wakeup_fd_ >= 0) ::close(wakeup_fd_);
        if (epfd_ >= 0)      ::close(epfd_);
    }

    static uint32_t toEpoll(uint8_t events) {
        uint32_t ep = 0;
        if (events & IoReactor::Readable) ep |= EPOLLIN;
        if (events & IoReactor::Writable) ep |= EPOLLOUT;
        if (events & IoReactor::Error)    ep |= EPOLLERR | EPOLLHUP;
        return ep;
    }

    static uint8_t fromEpoll(uint32_t ep) {
        uint8_t e = 0;
        if (ep & EPOLLIN)                   e |= IoReactor::Readable;
        if (ep & EPOLLOUT)                  e |= IoReactor::Writable;
        if (ep & (EPOLLERR | EPOLLHUP))     e |= IoReactor::Error;
        return e;
    }

    void doWakeup() {
        uint64_t val = 1;
        ::write(wakeup_fd_, &val, sizeof(val));
    }

    void drainWakeup() {
        uint64_t val;
        ::read(wakeup_fd_, &val, sizeof(val));
    }

    bool addFd(platform::socket_t fd, uint8_t events, EventCallback cb) {
        epoll_event ev{};
        ev.events  = toEpoll(events);
        ev.data.fd = fd;
        if (::epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev) != 0)
            return false;

        std::lock_guard lock(mutex_);
        fds_[fd] = FdEntry{fd, events, std::move(cb)};
        return true;
    }

    bool modifyFd(platform::socket_t fd, uint8_t events) {
        epoll_event ev{};
        ev.events  = toEpoll(events);
        ev.data.fd = fd;
        if (::epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev) != 0)
            return false;

        std::lock_guard lock(mutex_);
        auto it = fds_.find(fd);
        if (it != fds_.end())
            it->second.events = events;
        return true;
    }

    bool removeFd(platform::socket_t fd) {
        ::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
        std::lock_guard lock(mutex_);
        return fds_.erase(fd) > 0;
    }

    int pollOnce(std::chrono::milliseconds timeout) {
        constexpr int kMaxEvents = 64;
        epoll_event events[kMaxEvents];

        int nready = ::epoll_wait(epfd_, events, kMaxEvents,
                                   static_cast<int>(timeout.count()));
        if (nready <= 0) return 0;

        int dispatched = 0;
        for (int i = 0; i < nready; ++i) {
            if (events[i].data.fd == wakeup_fd_) {
                drainWakeup();
                continue;
            }

            auto fd_val = static_cast<platform::socket_t>(events[i].data.fd);
            uint8_t fired = fromEpoll(events[i].events);

            EventCallback cb;
            {
                std::lock_guard lock(mutex_);
                auto it = fds_.find(fd_val);
                if (it != fds_.end())
                    cb = it->second.callback;
            }
            if (cb) {
                cb(fd_val, fired);
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
