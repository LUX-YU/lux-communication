#include "lux/communication/transport/IoReactor.hpp"

#include <atomic>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <vector>
#include <chrono>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

namespace lux::communication::transport
{
    // =========================================================================
    // Windows IoReactor – IOCP + WSAPoll hybrid
    //
    // * Connected TCP sockets  → zero-byte overlapped WSARecv through IOCP
    // * UDP (SOCK_DGRAM)       → zero-byte overlapped WSARecvFrom + MSG_PEEK
    // * Listening TCP sockets  → WSAPoll fallback  (IOCP cannot recv on them)
    //
    // Wakeup: PostQueuedCompletionStatus (for IOCP path)
    //         + UDP loopback pair        (for WSAPoll path)
    // =========================================================================

    struct IoReactor::Impl
    {

        // OVERLAPPED subclass with per-operation metadata.
        struct OverlappedEx
        {
            OVERLAPPED ov{};
            platform::socket_t fd;
            uint8_t event_type; // Readable or Writable
        };

        struct FdContext
        {
            platform::socket_t fd;
            uint8_t events;
            EventCallback callback;
            OverlappedEx read_ov;
            OverlappedEx write_ov;
            bool read_pending = false;
            bool write_pending = false;
            bool closing = false;
            bool is_dgram = false;   // SOCK_DGRAM
            bool needs_poll = false; // WSAPoll fallback

            FdContext(platform::socket_t f, uint8_t ev, EventCallback cb)
                : fd(f), events(ev), callback(std::move(cb))
            {
                read_ov.fd = f;
                read_ov.event_type = IoReactor::Readable;
                write_ov.fd = f;
                write_ov.event_type = IoReactor::Writable;
            }
        };

        static constexpr ULONG_PTR kWakeupKey = ~ULONG_PTR{0};

        HANDLE iocp_{nullptr};
        std::unordered_map<platform::socket_t,
                           std::unique_ptr<FdContext>>
            fds_;
        // Contexts with pending IOCP ops after removeFd – kept alive until
        // their completions arrive so the OVERLAPPED stays valid.
        std::vector<std::unique_ptr<FdContext>> graveyard_;
        std::mutex mutex_;
        std::atomic<bool> running_{false};

        // UDP loopback pair for waking up WSAPoll.
        SOCKET wakeup_send_ = INVALID_SOCKET;
        SOCKET wakeup_recv_ = INVALID_SOCKET;

        // ── lifecycle ──────────────────────────────────────────────────────

        Impl()
        {
            iocp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
            initWakeup();
        }

        ~Impl()
        {
            {
                std::lock_guard lock(mutex_);
                for (auto &[fd, ctx] : fds_)
                    if (!ctx->needs_poll)
                        CancelIoEx(reinterpret_cast<HANDLE>(ctx->fd), nullptr);
                fds_.clear();
                graveyard_.clear();
            }
            if (iocp_)
            {
                CloseHandle(iocp_);
                iocp_ = nullptr;
            }
            if (wakeup_send_ != INVALID_SOCKET)
                ::closesocket(wakeup_send_);
            if (wakeup_recv_ != INVALID_SOCKET)
                ::closesocket(wakeup_recv_);
        }

        void initWakeup()
        {
            wakeup_recv_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            wakeup_send_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (wakeup_recv_ == INVALID_SOCKET || wakeup_send_ == INVALID_SOCKET)
                return;

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            addr.sin_port = 0;
            ::bind(wakeup_recv_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));

            int len = sizeof(addr);
            ::getsockname(wakeup_recv_, reinterpret_cast<sockaddr *>(&addr), &len);
            ::connect(wakeup_send_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));

            u_long mode = 1;
            ::ioctlsocket(wakeup_recv_, FIONBIO, &mode);
        }

        void drainWakeup()
        {
            char buf[64];
            while (::recv(wakeup_recv_, buf, sizeof(buf), 0) > 0)
            {
            }
        }

        // ── zero-byte overlapped helpers ───────────────────────────────────

        bool postZeroRead(FdContext *ctx)
        {
            if (ctx->read_pending || ctx->closing || ctx->needs_poll)
                return false;
            ZeroMemory(&ctx->read_ov.ov, sizeof(OVERLAPPED));
            WSABUF buf{0, nullptr};
            DWORD bytes = 0, flags = 0;
            int r;

            if (ctx->is_dgram)
            {
                flags = MSG_PEEK;
                r = WSARecvFrom(static_cast<SOCKET>(ctx->fd),
                                &buf, 1, &bytes, &flags,
                                nullptr, nullptr,
                                &ctx->read_ov.ov, nullptr);
            }
            else
            {
                r = WSARecv(static_cast<SOCKET>(ctx->fd),
                            &buf, 1, &bytes, &flags,
                            &ctx->read_ov.ov, nullptr);
            }

            if (r == 0 || (r == SOCKET_ERROR && WSAGetLastError() == WSA_IO_PENDING))
            {
                ctx->read_pending = true;
                return true;
            }
            return false;
        }

        bool postZeroWrite(FdContext *ctx)
        {
            if (ctx->write_pending || ctx->closing || ctx->needs_poll)
                return false;
            ZeroMemory(&ctx->write_ov.ov, sizeof(OVERLAPPED));
            WSABUF buf{0, nullptr};
            DWORD bytes = 0;
            int r = WSASend(static_cast<SOCKET>(ctx->fd),
                            &buf, 1, &bytes, 0,
                            &ctx->write_ov.ov, nullptr);
            if (r == 0 || (r == SOCKET_ERROR && WSAGetLastError() == WSA_IO_PENDING))
            {
                ctx->write_pending = true;
                return true;
            }
            return false;
        }

        // ── public API ─────────────────────────────────────────────────────

        bool addFd(platform::socket_t fd, uint8_t events, EventCallback cb)
        {
            std::lock_guard lock(mutex_);
            if (fds_.count(fd))
                return false;

            auto ctx = std::make_unique<FdContext>(fd, events, std::move(cb));

            // Detect socket kind.
            int socktype = 0;
            int optlen = sizeof(socktype);
            getsockopt(static_cast<SOCKET>(fd), SOL_SOCKET, SO_TYPE,
                       reinterpret_cast<char *>(&socktype), &optlen);

            ctx->is_dgram = (socktype == SOCK_DGRAM);

            if (!ctx->is_dgram)
            {
                BOOL acceptconn = FALSE;
                int alen = sizeof(acceptconn);
                getsockopt(static_cast<SOCKET>(fd), SOL_SOCKET, SO_ACCEPTCONN,
                           reinterpret_cast<char *>(&acceptconn), &alen);
                if (acceptconn)
                    ctx->needs_poll = true; // listener → WSAPoll only
            }

            // Associate with IOCP (skip for poll-only fds).
            if (!ctx->needs_poll)
            {
                if (!CreateIoCompletionPort(reinterpret_cast<HANDLE>(fd), iocp_,
                                            static_cast<ULONG_PTR>(0), 0))
                    ctx->needs_poll = true; // fallback
            }

            auto *raw = ctx.get();
            fds_[fd] = std::move(ctx);

            if (!raw->needs_poll)
            {
                if (events & Readable)
                    postZeroRead(raw);
                if (events & Writable)
                    postZeroWrite(raw);
            }
            return true;
        }

        bool modifyFd(platform::socket_t fd, uint8_t events)
        {
            std::lock_guard lock(mutex_);
            auto it = fds_.find(fd);
            if (it == fds_.end())
                return false;
            auto *ctx = it->second.get();
            uint8_t old = ctx->events;
            ctx->events = events;

            if (!ctx->needs_poll)
            {
                if ((events & Readable) && !(old & Readable))
                    postZeroRead(ctx);
                if ((events & Writable) && !(old & Writable))
                    postZeroWrite(ctx);
            }
            return true;
        }

        bool removeFd(platform::socket_t fd)
        {
            std::lock_guard lock(mutex_);
            auto it = fds_.find(fd);
            if (it == fds_.end())
                return false;

            auto *ctx = it->second.get();
            ctx->closing = true;

            if (!ctx->needs_poll)
                CancelIoEx(reinterpret_cast<HANDLE>(fd), nullptr);

            if (ctx->read_pending || ctx->write_pending)
                graveyard_.push_back(std::move(it->second));

            fds_.erase(it);
            return true;
        }

        // ── dispatch helpers ───────────────────────────────────────────────

        // Dispatch IOCP completions (call under NO lock – acquires internally).
        int dispatchIOCP(OVERLAPPED_ENTRY *entries, ULONG count)
        {
            int dispatched = 0;
            for (ULONG i = 0; i < count; ++i)
            {
                if (entries[i].lpCompletionKey == kWakeupKey)
                    continue;
                if (!entries[i].lpOverlapped)
                    continue;

                auto *ovex = reinterpret_cast<OverlappedEx *>(entries[i].lpOverlapped);
                platform::socket_t fd = ovex->fd;
                uint8_t evt = ovex->event_type;

                EventCallback cb_copy;
                bool should_dispatch = false;
                bool rearm_read = false;
                bool rearm_write = false;

                {
                    std::lock_guard lock(mutex_);

                    // Check main map first, then graveyard.
                    auto it = fds_.find(fd);
                    if (it != fds_.end())
                    {
                        auto *ctx = it->second.get();

                        if (evt == Readable)
                            ctx->read_pending = false;
                        if (evt == Writable)
                            ctx->write_pending = false;

                        if (ctx->closing)
                            continue;

                        bool is_error = (ovex->ov.Internal != 0);
                        if (is_error)
                        {
                            cb_copy = ctx->callback;
                            should_dispatch = true;
                            evt = Error;
                        }
                        else if (ctx->events & evt)
                        {
                            cb_copy = ctx->callback;
                            should_dispatch = true;
                            rearm_read = (evt == Readable);
                            rearm_write = (evt == Writable);
                        }
                    }
                    else
                    {
                        // Completion for a graveyard entry → clean up.
                        for (auto git = graveyard_.begin(); git != graveyard_.end(); ++git)
                        {
                            auto *gctx = git->get();
                            if (gctx->fd != fd)
                                continue;
                            if (evt == Readable)
                                gctx->read_pending = false;
                            if (evt == Writable)
                                gctx->write_pending = false;
                            if (!gctx->read_pending && !gctx->write_pending)
                                graveyard_.erase(git);
                            break;
                        }
                        continue;
                    }
                }

                if (should_dispatch && cb_copy)
                {
                    cb_copy(fd, evt);
                    ++dispatched;
                }

                if (rearm_read || rearm_write)
                {
                    std::lock_guard lock(mutex_);
                    auto it = fds_.find(fd);
                    if (it != fds_.end() && !it->second->closing)
                    {
                        if (rearm_read)
                            postZeroRead(it->second.get());
                        if (rearm_write)
                            postZeroWrite(it->second.get());
                    }
                }
            }
            return dispatched;
        }

        // Dispatch ready poll-fds via WSAPoll (call under NO lock).
        int dispatchPollFds(int timeout_ms)
        {
            std::vector<WSAPOLLFD> pollfds;
            std::vector<platform::socket_t> poll_keys;

            {
                std::lock_guard lock(mutex_);
                for (auto &[fd, ctx] : fds_)
                {
                    if (!ctx->needs_poll || ctx->closing)
                        continue;
                    WSAPOLLFD pfd{};
                    pfd.fd = static_cast<SOCKET>(fd);
                    if (ctx->events & Readable)
                        pfd.events |= POLLIN;
                    if (ctx->events & Writable)
                        pfd.events |= POLLOUT;
                    pollfds.push_back(pfd);
                    poll_keys.push_back(fd);
                }
            }

            if (pollfds.empty())
                return 0;

            // Include wakeup socket so we can break out of WSAPoll.
            WSAPOLLFD wpfd{};
            wpfd.fd = wakeup_recv_;
            wpfd.events = POLLIN;
            pollfds.push_back(wpfd);

            int r = WSAPoll(pollfds.data(), static_cast<ULONG>(pollfds.size()), timeout_ms);
            if (r <= 0)
                return 0;

            // Drain wakeup if fired.
            if (pollfds.back().revents & POLLIN)
                drainWakeup();

            int dispatched = 0;
            for (size_t i = 0; i < poll_keys.size(); ++i)
            {
                if (pollfds[i].revents == 0)
                    continue;
                uint8_t fired = 0;
                if (pollfds[i].revents & POLLIN)
                    fired |= Readable;
                if (pollfds[i].revents & POLLOUT)
                    fired |= Writable;
                if (pollfds[i].revents & (POLLERR | POLLHUP))
                    fired |= Error;
                if (!fired)
                    continue;

                EventCallback cb_copy;
                {
                    std::lock_guard lock(mutex_);
                    auto it = fds_.find(poll_keys[i]);
                    if (it == fds_.end() || it->second->closing)
                        continue;
                    cb_copy = it->second->callback;
                }
                if (cb_copy)
                {
                    cb_copy(poll_keys[i], fired);
                    ++dispatched;
                }
            }
            return dispatched;
        }

        bool hasPollFds() const
        {
            for (auto &[_, ctx] : fds_)
                if (ctx->needs_poll && !ctx->closing)
                    return true;
            return false;
        }

        // ── pollOnce ───────────────────────────────────────────────────────

        int pollOnce(std::chrono::milliseconds timeout)
        {
            bool poll_path;
            {
                std::lock_guard lock(mutex_);
                poll_path = hasPollFds();
            }

            int dispatched = 0;

            if (poll_path)
            {
                // Hybrid: IOCP check (0 ms) + WSAPoll with timeout.
                constexpr ULONG kMax = 64;
                OVERLAPPED_ENTRY entries[kMax];
                ULONG count = 0;
                if (GetQueuedCompletionStatusEx(iocp_, entries, kMax, &count, 0, FALSE))
                    dispatched += dispatchIOCP(entries, count);

                if (dispatched == 0)
                    dispatched += dispatchPollFds(static_cast<int>(timeout.count()));
            }
            else
            {
                // Pure IOCP path.
                constexpr ULONG kMax = 64;
                OVERLAPPED_ENTRY entries[kMax];
                ULONG count = 0;
                DWORD ms = static_cast<DWORD>(timeout.count());
                if (GetQueuedCompletionStatusEx(iocp_, entries, kMax, &count, ms, FALSE))
                    dispatched += dispatchIOCP(entries, count);
            }

            return dispatched;
        }

        // ── run / stop / wakeup ────────────────────────────────────────────

        void run()
        {
            running_.store(true, std::memory_order_release);
            while (running_.load(std::memory_order_acquire))
            {
                pollOnce(std::chrono::milliseconds{100});
            }
        }

        void stop()
        {
            running_.store(false, std::memory_order_release);
            doWakeup();
        }

        void doWakeup()
        {
            PostQueuedCompletionStatus(iocp_, 0, kWakeupKey, nullptr);
            // Also poke the WSAPoll wakeup socket.
            char c = 'W';
            ::send(wakeup_send_, &c, 1, 0);
        }
    };

    // -- IoReactor forwarding ------------------------------------------------

    IoReactor::IoReactor() : impl_(std::make_unique<Impl>()) {}
    IoReactor::~IoReactor() { stop(); }

    bool IoReactor::addFd(platform::socket_t fd, uint8_t events, EventCallback cb)
    {
        return impl_->addFd(fd, events, std::move(cb));
    }

    bool IoReactor::modifyFd(platform::socket_t fd, uint8_t events)
    {
        return impl_->modifyFd(fd, events);
    }

    bool IoReactor::removeFd(platform::socket_t fd)
    {
        return impl_->removeFd(fd);
    }

    int IoReactor::pollOnce(std::chrono::milliseconds timeout)
    {
        return impl_->pollOnce(timeout);
    }

    void IoReactor::run() { impl_->run(); }

    void IoReactor::stop()
    {
        if (impl_)
            impl_->stop();
    }

    void IoReactor::wakeup()
    {
        if (impl_)
            impl_->doWakeup();
    }

    bool IoReactor::isRunning() const
    {
        return impl_ && impl_->running_.load(std::memory_order_acquire);
    }

    size_t IoReactor::fdCount() const
    {
        if (!impl_)
            return 0;
        std::lock_guard lock(impl_->mutex_);
        return impl_->fds_.size();
    }

} // namespace lux::communication::transport
