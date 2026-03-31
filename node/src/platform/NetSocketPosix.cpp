#include "lux/communication/platform/NetSocket.hpp"

#include <cstring>
#include <cerrno>

#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

namespace lux::communication::platform
{
    // ── netInit / netCleanup — no-op on POSIX ────────────────────────────────────

    void netInit() {}
    void netCleanup() {}

    // ── helpers ──────────────────────────────────────────────────────────────────

    static sockaddr_in makeAddr(const std::string &ip, uint16_t port)
    {
        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port = htons(port);
        if (ip.empty() || ip == "0.0.0.0")
            sa.sin_addr.s_addr = INADDR_ANY;
        else
            ::inet_pton(AF_INET, ip.c_str(), &sa.sin_addr);
        return sa;
    }

    static uint16_t getLocalPort(int fd)
    {
        sockaddr_in sa{};
        socklen_t len = sizeof(sa);
        if (::getsockname(fd, reinterpret_cast<sockaddr *>(&sa), &len) == 0)
            return ntohs(sa.sin_port);
        return 0;
    }

    static bool setNonBlockingImpl(int fd, bool on)
    {
        int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags < 0)
            return false;
        if (on)
            flags |= O_NONBLOCK;
        else
            flags &= ~O_NONBLOCK;
        return ::fcntl(fd, F_SETFL, flags) == 0;
    }

    // ═════════════════════════════════════════════════════════════════════════════
    // UdpSocket
    // ═════════════════════════════════════════════════════════════════════════════

    UdpSocket::UdpSocket() = default;
    UdpSocket::~UdpSocket() { close(); }

    UdpSocket::UdpSocket(UdpSocket &&o) noexcept : sock_(o.sock_) { o.sock_ = kInvalidSocket; }
    UdpSocket &UdpSocket::operator=(UdpSocket &&o) noexcept
    {
        if (this != &o)
        {
            close();
            sock_ = o.sock_;
            o.sock_ = kInvalidSocket;
        }
        return *this;
    }

    bool UdpSocket::ensureSocket()
    {
        if (sock_ != kInvalidSocket)
            return true;
        int s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s < 0)
            return false;
        sock_ = s;
        return true;
    }

    bool UdpSocket::bind(const std::string &addr, uint16_t port)
    {
        if (!ensureSocket())
            return false;
        auto sa = makeAddr(addr, port);
        return ::bind(sock_, reinterpret_cast<const sockaddr *>(&sa), sizeof(sa)) == 0;
    }

    bool UdpSocket::bindAny(uint16_t port) { return bind("0.0.0.0", port); }

    bool UdpSocket::setNonBlocking(bool on)
    {
        return sock_ != kInvalidSocket && setNonBlockingImpl(sock_, on);
    }

    bool UdpSocket::setReuseAddr(bool on)
    {
        int val = on ? 1 : 0;
        return sock_ != kInvalidSocket &&
               ::setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) == 0;
    }

    bool UdpSocket::setRecvBufferSize(int bytes)
    {
        return sock_ != kInvalidSocket &&
               ::setsockopt(sock_, SOL_SOCKET, SO_RCVBUF, &bytes, sizeof(bytes)) == 0;
    }

    bool UdpSocket::setSendBufferSize(int bytes)
    {
        return sock_ != kInvalidSocket &&
               ::setsockopt(sock_, SOL_SOCKET, SO_SNDBUF, &bytes, sizeof(bytes)) == 0;
    }

    bool UdpSocket::setBroadcast(bool on)
    {
        int val = on ? 1 : 0;
        return sock_ != kInvalidSocket &&
               ::setsockopt(sock_, SOL_SOCKET, SO_BROADCAST, &val, sizeof(val)) == 0;
    }

    bool UdpSocket::joinMulticastGroup(const std::string &group_addr)
    {
        ip_mreq mreq{};
        ::inet_pton(AF_INET, group_addr.c_str(), &mreq.imr_multiaddr);
        mreq.imr_interface.s_addr = INADDR_ANY;
        return sock_ != kInvalidSocket &&
               ::setsockopt(sock_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) == 0;
    }

    int UdpSocket::sendTo(const void *data, size_t len,
                          const std::string &dest_addr, uint16_t dest_port)
    {
        if (!ensureSocket())
            return -1;
        auto sa = makeAddr(dest_addr, dest_port);
        return static_cast<int>(
            ::sendto(sock_, data, len, 0,
                     reinterpret_cast<const sockaddr *>(&sa), sizeof(sa)));
    }

    int UdpSocket::sendToV(const IoVec *iov, int iovcnt,
                           const std::string &dest_addr, uint16_t dest_port)
    {
        if (!ensureSocket())
            return -1;

        // Use sendmsg for scatter-gather
        auto sa = makeAddr(dest_addr, dest_port);

        // iovec and IoVec have identical layout on most platforms,
        // but build explicit array for safety.
        constexpr int kMaxIov = 8;
        struct iovec vecs[kMaxIov];
        int cnt = (iovcnt < kMaxIov) ? iovcnt : kMaxIov;
        for (int i = 0; i < cnt; ++i)
        {
            vecs[i].iov_base = const_cast<void *>(iov[i].base);
            vecs[i].iov_len = iov[i].len;
        }

        msghdr msg{};
        msg.msg_name = &sa;
        msg.msg_namelen = sizeof(sa);
        msg.msg_iov = vecs;
        msg.msg_iovlen = cnt;

        ssize_t n = ::sendmsg(sock_, &msg, 0);
        return static_cast<int>(n);
    }

    int UdpSocket::recvFrom(void *buf, size_t max_len,
                            std::string &src_addr, uint16_t &src_port)
    {
        if (sock_ == kInvalidSocket)
            return -1;
        sockaddr_in sa{};
        socklen_t sa_len = sizeof(sa);
        ssize_t n = ::recvfrom(sock_, buf, max_len, 0,
                               reinterpret_cast<sockaddr *>(&sa), &sa_len);
        if (n < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return 0;
            return -1;
        }
        char ip_str[INET_ADDRSTRLEN]{};
        ::inet_ntop(AF_INET, &sa.sin_addr, ip_str, sizeof(ip_str));
        src_addr = ip_str;
        src_port = ntohs(sa.sin_port);
        return static_cast<int>(n);
    }

    uint16_t UdpSocket::localPort() const
    {
        if (sock_ == kInvalidSocket)
            return 0;
        return getLocalPort(sock_);
    }

    void UdpSocket::close()
    {
        if (sock_ != kInvalidSocket)
        {
            ::close(sock_);
            sock_ = kInvalidSocket;
        }
    }

    // ═════════════════════════════════════════════════════════════════════════════
    // TcpSocket
    // ═════════════════════════════════════════════════════════════════════════════

    TcpSocket::TcpSocket() = default;
    TcpSocket::TcpSocket(socket_t fd) : sock_(fd) {}
    TcpSocket::~TcpSocket() { close(); }

    TcpSocket::TcpSocket(TcpSocket &&o) noexcept
        : sock_(o.sock_), remote_addr_(std::move(o.remote_addr_)), remote_port_(o.remote_port_)
    {
        o.sock_ = kInvalidSocket;
        o.remote_port_ = 0;
    }

    TcpSocket &TcpSocket::operator=(TcpSocket &&o) noexcept
    {
        if (this != &o)
        {
            close();
            sock_ = o.sock_;
            remote_addr_ = std::move(o.remote_addr_);
            remote_port_ = o.remote_port_;
            o.sock_ = kInvalidSocket;
            o.remote_port_ = 0;
        }
        return *this;
    }

    bool TcpSocket::connect(const std::string &addr, uint16_t port)
    {
        int s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s < 0)
            return false;
        sock_ = s;

        auto sa = makeAddr(addr, port);
        if (::connect(s, reinterpret_cast<const sockaddr *>(&sa), sizeof(sa)) != 0)
        {
            close();
            return false;
        }
        remote_addr_ = addr;
        remote_port_ = port;
        return true;
    }

    bool TcpSocket::setNonBlocking(bool on)
    {
        return sock_ != kInvalidSocket && setNonBlockingImpl(sock_, on);
    }

    bool TcpSocket::setNoDelay(bool on)
    {
        int val = on ? 1 : 0;
        return sock_ != kInvalidSocket &&
               ::setsockopt(sock_, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)) == 0;
    }

    bool TcpSocket::setQuickAck(bool on)
    {
#ifdef TCP_QUICKACK
        int val = on ? 1 : 0;
        return sock_ != kInvalidSocket &&
               ::setsockopt(sock_, IPPROTO_TCP, TCP_QUICKACK, &val, sizeof(val)) == 0;
#else
        (void)on;
        return true;
#endif
    }

    bool TcpSocket::setSendBufferSize(int bytes)
    {
        return sock_ != kInvalidSocket &&
               ::setsockopt(sock_, SOL_SOCKET, SO_SNDBUF, &bytes, sizeof(bytes)) == 0;
    }

    bool TcpSocket::setRecvBufferSize(int bytes)
    {
        return sock_ != kInvalidSocket &&
               ::setsockopt(sock_, SOL_SOCKET, SO_RCVBUF, &bytes, sizeof(bytes)) == 0;
    }

    int TcpSocket::send(const void *data, size_t len)
    {
        if (sock_ == kInvalidSocket)
            return -1;
        return static_cast<int>(::send(sock_, data, len, MSG_NOSIGNAL));
    }

    int TcpSocket::sendV(const IoVec *iov, int iovcnt)
    {
        if (sock_ == kInvalidSocket)
            return -1;

        constexpr int kMaxIov = 8;
        struct iovec vecs[kMaxIov];
        int cnt = (iovcnt < kMaxIov) ? iovcnt : kMaxIov;
        for (int i = 0; i < cnt; ++i)
        {
            vecs[i].iov_base = const_cast<void *>(iov[i].base);
            vecs[i].iov_len = iov[i].len;
        }

        msghdr msg{};
        msg.msg_iov = vecs;
        msg.msg_iovlen = cnt;

        ssize_t n = ::sendmsg(sock_, &msg, MSG_NOSIGNAL);
        return static_cast<int>(n);
    }

    int TcpSocket::recv(void *buf, size_t max_len)
    {
        if (sock_ == kInvalidSocket)
            return -1;
        ssize_t n = ::recv(sock_, buf, max_len, 0);
        if (n < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return -1;
            return -1;
        }
        return static_cast<int>(n); // 0 = peer closed
    }

    bool TcpSocket::sendAll(const void *data, size_t len)
    {
        auto p = static_cast<const char *>(data);
        size_t sent = 0;
        while (sent < len)
        {
            ssize_t n = ::send(sock_, p + sent, len - sent, MSG_NOSIGNAL);
            if (n <= 0)
                return false;
            sent += static_cast<size_t>(n);
        }
        return true;
    }

    bool TcpSocket::recvAll(void *buf, size_t len)
    {
        auto p = static_cast<char *>(buf);
        size_t got = 0;
        while (got < len)
        {
            ssize_t n = ::recv(sock_, p + got, len - got, 0);
            if (n <= 0)
                return false;
            got += static_cast<size_t>(n);
        }
        return true;
    }

    void TcpSocket::close()
    {
        if (sock_ != kInvalidSocket)
        {
            ::close(sock_);
            sock_ = kInvalidSocket;
        }
    }

    // ═════════════════════════════════════════════════════════════════════════════
    // TcpListener
    // ═════════════════════════════════════════════════════════════════════════════

    TcpListener::TcpListener() = default;
    TcpListener::~TcpListener() { close(); }

    TcpListener::TcpListener(TcpListener &&o) noexcept : sock_(o.sock_) { o.sock_ = kInvalidSocket; }
    TcpListener &TcpListener::operator=(TcpListener &&o) noexcept
    {
        if (this != &o)
        {
            close();
            sock_ = o.sock_;
            o.sock_ = kInvalidSocket;
        }
        return *this;
    }

    bool TcpListener::listen(const std::string &addr, uint16_t port, int backlog)
    {
        int s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s < 0)
            return false;

        int reuse = 1;
        ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        auto sa = makeAddr(addr, port);
        if (::bind(s, reinterpret_cast<const sockaddr *>(&sa), sizeof(sa)) != 0)
        {
            ::close(s);
            return false;
        }
        if (::listen(s, backlog) != 0)
        {
            ::close(s);
            return false;
        }
        sock_ = s;
        return true;
    }

    bool TcpListener::listenAny(uint16_t port, int backlog)
    {
        return listen("0.0.0.0", port, backlog);
    }

    bool TcpListener::setNonBlocking(bool on)
    {
        return sock_ != kInvalidSocket && setNonBlockingImpl(sock_, on);
    }

    TcpSocket TcpListener::accept(std::string &remote_addr, uint16_t &remote_port)
    {
        if (sock_ == kInvalidSocket)
            return TcpSocket{};

        sockaddr_in sa{};
        socklen_t sa_len = sizeof(sa);
        int client = ::accept(sock_, reinterpret_cast<sockaddr *>(&sa), &sa_len);
        if (client < 0)
            return TcpSocket{};

        char ip_str[INET_ADDRSTRLEN]{};
        ::inet_ntop(AF_INET, &sa.sin_addr, ip_str, sizeof(ip_str));
        remote_addr = ip_str;
        remote_port = ntohs(sa.sin_port);

        return TcpSocket{static_cast<socket_t>(client)};
    }

    uint16_t TcpListener::localPort() const
    {
        if (sock_ == kInvalidSocket)
            return 0;
        return getLocalPort(sock_);
    }

    void TcpListener::close()
    {
        if (sock_ != kInvalidSocket)
        {
            ::close(sock_);
            sock_ = kInvalidSocket;
        }
    }

} // namespace lux::communication::platform
