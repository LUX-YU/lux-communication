#include "lux/communication/platform/NetSocket.hpp"

#include <atomic>
#include <cstring>
#include <cassert>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

namespace lux::communication::platform
{
    // ── WSAStartup / WSACleanup reference counting ───────────────────────────────
    static std::atomic<int> g_wsaRefCount{0};

    void netInit()
    {
        if (g_wsaRefCount.fetch_add(1, std::memory_order_acq_rel) == 0)
        {
            WSADATA wsa{};
            int rc = ::WSAStartup(MAKEWORD(2, 2), &wsa);
            if (rc != 0)
            {
                g_wsaRefCount.fetch_sub(1, std::memory_order_acq_rel);
                assert(false && "WSAStartup failed");
            }
        }
    }

    void netCleanup()
    {
        if (g_wsaRefCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            ::WSACleanup();
        }
    }

    // ── helpers ──────────────────────────────────────────────────────────────────

    static sockaddr_in makeAddr(const std::string &ip, uint16_t port)
    {
        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port = htons(port);
        if (ip.empty() || ip == "0.0.0.0")
        {
            sa.sin_addr.s_addr = INADDR_ANY;
        }
        else
        {
            ::inet_pton(AF_INET, ip.c_str(), &sa.sin_addr);
        }
        return sa;
    }

    static uint16_t getLocalPort(SOCKET s)
    {
        sockaddr_in sa{};
        int len = sizeof(sa);
        if (::getsockname(s, reinterpret_cast<sockaddr *>(&sa), &len) == 0)
            return ntohs(sa.sin_port);
        return 0;
    }

    static bool setNonBlockingImpl(SOCKET s, bool on)
    {
        u_long mode = on ? 1 : 0;
        return ::ioctlsocket(s, FIONBIO, &mode) == 0;
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
        SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == INVALID_SOCKET)
            return false;
        sock_ = static_cast<socket_t>(s);
        return true;
    }

    bool UdpSocket::bind(const std::string &addr, uint16_t port)
    {
        if (!ensureSocket())
            return false;
        auto sa = makeAddr(addr, port);
        return ::bind(static_cast<SOCKET>(sock_),
                      reinterpret_cast<const sockaddr *>(&sa), sizeof(sa)) == 0;
    }

    bool UdpSocket::bindAny(uint16_t port)
    {
        return bind("0.0.0.0", port);
    }

    bool UdpSocket::setNonBlocking(bool on)
    {
        return sock_ != kInvalidSocket && setNonBlockingImpl(static_cast<SOCKET>(sock_), on);
    }

    bool UdpSocket::setReuseAddr(bool on)
    {
        BOOL val = on ? TRUE : FALSE;
        return sock_ != kInvalidSocket &&
               ::setsockopt(static_cast<SOCKET>(sock_), SOL_SOCKET, SO_REUSEADDR,
                            reinterpret_cast<const char *>(&val), sizeof(val)) == 0;
    }

    bool UdpSocket::setRecvBufferSize(int bytes)
    {
        return sock_ != kInvalidSocket &&
               ::setsockopt(static_cast<SOCKET>(sock_), SOL_SOCKET, SO_RCVBUF,
                            reinterpret_cast<const char *>(&bytes), sizeof(bytes)) == 0;
    }

    bool UdpSocket::setSendBufferSize(int bytes)
    {
        return sock_ != kInvalidSocket &&
               ::setsockopt(static_cast<SOCKET>(sock_), SOL_SOCKET, SO_SNDBUF,
                            reinterpret_cast<const char *>(&bytes), sizeof(bytes)) == 0;
    }

    bool UdpSocket::setBroadcast(bool on)
    {
        BOOL val = on ? TRUE : FALSE;
        return sock_ != kInvalidSocket &&
               ::setsockopt(static_cast<SOCKET>(sock_), SOL_SOCKET, SO_BROADCAST,
                            reinterpret_cast<const char *>(&val), sizeof(val)) == 0;
    }

    bool UdpSocket::joinMulticastGroup(const std::string &group_addr)
    {
        ip_mreq mreq{};
        ::inet_pton(AF_INET, group_addr.c_str(), &mreq.imr_multiaddr);
        mreq.imr_interface.s_addr = INADDR_ANY;
        return sock_ != kInvalidSocket &&
               ::setsockopt(static_cast<SOCKET>(sock_), IPPROTO_IP, IP_ADD_MEMBERSHIP,
                            reinterpret_cast<const char *>(&mreq), sizeof(mreq)) == 0;
    }

    int UdpSocket::sendTo(const void *data, size_t len,
                          const std::string &dest_addr, uint16_t dest_port)
    {
        if (!ensureSocket())
            return -1;
        auto sa = makeAddr(dest_addr, dest_port);
        return ::sendto(static_cast<SOCKET>(sock_),
                        static_cast<const char *>(data), static_cast<int>(len), 0,
                        reinterpret_cast<const sockaddr *>(&sa), sizeof(sa));
    }

    int UdpSocket::sendToV(const IoVec *iov, int iovcnt,
                           const std::string &dest_addr, uint16_t dest_port)
    {
        if (!ensureSocket())
            return -1;

        // WSASendTo with WSABUF array (scatter-gather)
        constexpr int kMaxBufs = 8;
        WSABUF bufs[kMaxBufs];
        int cnt = (iovcnt < kMaxBufs) ? iovcnt : kMaxBufs;
        for (int i = 0; i < cnt; ++i)
        {
            bufs[i].buf = const_cast<char *>(static_cast<const char *>(iov[i].base));
            bufs[i].len = static_cast<ULONG>(iov[i].len);
        }
        auto sa = makeAddr(dest_addr, dest_port);
        DWORD bytesSent = 0;
        int rc = ::WSASendTo(static_cast<SOCKET>(sock_), bufs, static_cast<DWORD>(cnt),
                             &bytesSent, 0,
                             reinterpret_cast<const sockaddr *>(&sa), sizeof(sa),
                             nullptr, nullptr);
        return (rc == 0) ? static_cast<int>(bytesSent) : -1;
    }

    int UdpSocket::recvFrom(void *buf, size_t max_len,
                            std::string &src_addr, uint16_t &src_port)
    {
        if (sock_ == kInvalidSocket)
            return -1;
        sockaddr_in sa{};
        int sa_len = sizeof(sa);
        int n = ::recvfrom(static_cast<SOCKET>(sock_),
                           static_cast<char *>(buf), static_cast<int>(max_len), 0,
                           reinterpret_cast<sockaddr *>(&sa), &sa_len);
        if (n < 0)
        {
            int err = ::WSAGetLastError();
            if (err == WSAEWOULDBLOCK)
                return 0; // non-blocking, no data
            return -1;
        }
        char ip_str[INET_ADDRSTRLEN]{};
        ::inet_ntop(AF_INET, &sa.sin_addr, ip_str, sizeof(ip_str));
        src_addr = ip_str;
        src_port = ntohs(sa.sin_port);
        return n;
    }

    uint16_t UdpSocket::localPort() const
    {
        if (sock_ == kInvalidSocket)
            return 0;
        return getLocalPort(static_cast<SOCKET>(sock_));
    }

    void UdpSocket::close()
    {
        if (sock_ != kInvalidSocket)
        {
            ::closesocket(static_cast<SOCKET>(sock_));
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
        SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET)
            return false;
        sock_ = static_cast<socket_t>(s);

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
        return sock_ != kInvalidSocket && setNonBlockingImpl(static_cast<SOCKET>(sock_), on);
    }

    bool TcpSocket::setNoDelay(bool on)
    {
        BOOL val = on ? TRUE : FALSE;
        return sock_ != kInvalidSocket &&
               ::setsockopt(static_cast<SOCKET>(sock_), IPPROTO_TCP, TCP_NODELAY,
                            reinterpret_cast<const char *>(&val), sizeof(val)) == 0;
    }

    bool TcpSocket::setQuickAck(bool /*on*/)
    {
        // TCP_QUICKACK is Linux-only; no-op on Windows.
        return true;
    }

    bool TcpSocket::setSendBufferSize(int bytes)
    {
        return sock_ != kInvalidSocket &&
               ::setsockopt(static_cast<SOCKET>(sock_), SOL_SOCKET, SO_SNDBUF,
                            reinterpret_cast<const char *>(&bytes), sizeof(bytes)) == 0;
    }

    bool TcpSocket::setRecvBufferSize(int bytes)
    {
        return sock_ != kInvalidSocket &&
               ::setsockopt(static_cast<SOCKET>(sock_), SOL_SOCKET, SO_RCVBUF,
                            reinterpret_cast<const char *>(&bytes), sizeof(bytes)) == 0;
    }

    int TcpSocket::send(const void *data, size_t len)
    {
        if (sock_ == kInvalidSocket)
            return -1;
        return ::send(static_cast<SOCKET>(sock_),
                      static_cast<const char *>(data), static_cast<int>(len), 0);
    }

    int TcpSocket::sendV(const IoVec *iov, int iovcnt)
    {
        if (sock_ == kInvalidSocket)
            return -1;
        constexpr int kMaxBufs = 8;
        WSABUF bufs[kMaxBufs];
        int cnt = (iovcnt < kMaxBufs) ? iovcnt : kMaxBufs;
        for (int i = 0; i < cnt; ++i)
        {
            bufs[i].buf = const_cast<char *>(static_cast<const char *>(iov[i].base));
            bufs[i].len = static_cast<ULONG>(iov[i].len);
        }
        DWORD bytesSent = 0;
        int rc = ::WSASend(static_cast<SOCKET>(sock_), bufs, static_cast<DWORD>(cnt),
                           &bytesSent, 0, nullptr, nullptr);
        return (rc == 0) ? static_cast<int>(bytesSent) : -1;
    }

    int TcpSocket::recv(void *buf, size_t max_len)
    {
        if (sock_ == kInvalidSocket)
            return -1;
        int n = ::recv(static_cast<SOCKET>(sock_),
                       static_cast<char *>(buf), static_cast<int>(max_len), 0);
        if (n < 0)
        {
            int err = ::WSAGetLastError();
            if (err == WSAEWOULDBLOCK)
                return -1; // would-block
            return -1;
        }
        return n; // 0 = peer closed
    }

    bool TcpSocket::sendAll(const void *data, size_t len)
    {
        auto p = static_cast<const char *>(data);
        size_t sent = 0;
        while (sent < len)
        {
            int n = ::send(static_cast<SOCKET>(sock_), p + sent,
                           static_cast<int>(len - sent), 0);
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
            int n = ::recv(static_cast<SOCKET>(sock_), p + got,
                           static_cast<int>(len - got), 0);
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
            ::closesocket(static_cast<SOCKET>(sock_));
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
        SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET)
            return false;

        // Enable address reuse
        BOOL reuse = TRUE;
        ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char *>(&reuse), sizeof(reuse));

        auto sa = makeAddr(addr, port);
        if (::bind(s, reinterpret_cast<const sockaddr *>(&sa), sizeof(sa)) != 0)
        {
            ::closesocket(s);
            return false;
        }
        if (::listen(s, backlog) != 0)
        {
            ::closesocket(s);
            return false;
        }
        sock_ = static_cast<socket_t>(s);
        return true;
    }

    bool TcpListener::listenAny(uint16_t port, int backlog)
    {
        return listen("0.0.0.0", port, backlog);
    }

    bool TcpListener::setNonBlocking(bool on)
    {
        return sock_ != kInvalidSocket && setNonBlockingImpl(static_cast<SOCKET>(sock_), on);
    }

    TcpSocket TcpListener::accept(std::string &remote_addr, uint16_t &remote_port)
    {
        if (sock_ == kInvalidSocket)
            return TcpSocket{};

        sockaddr_in sa{};
        int sa_len = sizeof(sa);
        SOCKET client = ::accept(static_cast<SOCKET>(sock_),
                                 reinterpret_cast<sockaddr *>(&sa), &sa_len);
        if (client == INVALID_SOCKET)
            return TcpSocket{};

        char ip_str[INET_ADDRSTRLEN]{};
        ::inet_ntop(AF_INET, &sa.sin_addr, ip_str, sizeof(ip_str));
        remote_addr = ip_str;
        remote_port = ntohs(sa.sin_port);

        TcpSocket ts(static_cast<socket_t>(client));
        return ts;
    }

    uint16_t TcpListener::localPort() const
    {
        if (sock_ == kInvalidSocket)
            return 0;
        return getLocalPort(static_cast<SOCKET>(sock_));
    }

    void TcpListener::close()
    {
        if (sock_ != kInvalidSocket)
        {
            ::closesocket(static_cast<SOCKET>(sock_));
            sock_ = kInvalidSocket;
        }
    }

} // namespace lux::communication::platform
