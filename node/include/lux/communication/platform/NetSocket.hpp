#pragma once
#include <cstdint>
#include <string>
#include <lux/communication/visibility.h>

namespace lux::communication::platform
{
    // ── Cross-platform socket handle ──────────────────────────────────────────────
#ifdef _WIN32
    using socket_t = uintptr_t; // SOCKET = UINT_PTR on Windows
    static constexpr socket_t kInvalidSocket = static_cast<socket_t>(~0);
#else
    using socket_t = int;
    static constexpr socket_t kInvalidSocket = -1;
#endif

    /// Platform network initialisation / cleanup.
    /// Windows: WSAStartup / WSACleanup (reference-counted).
    /// POSIX:   no-op.
    LUX_COMMUNICATION_PUBLIC void netInit();
    LUX_COMMUNICATION_PUBLIC void netCleanup();

    /// RAII guard that calls netInit() on construction and netCleanup() on destruction.
    struct NetInitGuard
    {
        NetInitGuard() { netInit(); }
        ~NetInitGuard() { netCleanup(); }
        NetInitGuard(const NetInitGuard &) = delete;
        NetInitGuard &operator=(const NetInitGuard &) = delete;
    };

    // ── Scatter-gather I/O vector ─────────────────────────────────────────────────

    struct IoVec
    {
        const void *base;
        size_t len;
    };

    // ── UdpSocket ─────────────────────────────────────────────────────────────────

    /// Low-level cross-platform UDP socket.
    /// Supports unicast / multicast, non-blocking mode, scatter-gather send,
    /// and exposes the native fd for Reactor integration.
    class LUX_COMMUNICATION_PUBLIC UdpSocket
    {
    public:
        UdpSocket();
        ~UdpSocket();

        UdpSocket(UdpSocket &&o) noexcept;
        UdpSocket &operator=(UdpSocket &&o) noexcept;

        UdpSocket(const UdpSocket &) = delete;
        UdpSocket &operator=(const UdpSocket &) = delete;

        /// Bind to a specific local address and port.  port == 0 → OS-assigned.
        bool bind(const std::string &addr, uint16_t port);

        /// Bind to INADDR_ANY on the given port.
        bool bindAny(uint16_t port);

        // ── socket options ──

        bool setNonBlocking(bool on);
        bool setReuseAddr(bool on);
        bool setRecvBufferSize(int bytes);
        bool setSendBufferSize(int bytes);
        bool setBroadcast(bool on);

        /// Join a multicast group for receiving.
        bool joinMulticastGroup(const std::string &group_addr);

        // ── I/O ──

        /// Send a datagram.  Returns bytes sent, or -1 on error.
        int sendTo(const void *data, size_t len,
                   const std::string &dest_addr, uint16_t dest_port);

        /// Scatter-gather send (e.g. FrameHeader + payload without memcpy).
        int sendToV(const IoVec *iov, int iovcnt,
                    const std::string &dest_addr, uint16_t dest_port);

        /// Receive a datagram.
        /// Returns bytes received, 0 = would-block (non-blocking), -1 = error.
        int recvFrom(void *buf, size_t max_len,
                     std::string &src_addr, uint16_t &src_port);

        // ── accessors ──

        socket_t nativeFd() const { return sock_; }
        uint16_t localPort() const;
        bool isValid() const { return sock_ != kInvalidSocket; }
        void close();

    private:
        socket_t sock_ = kInvalidSocket;

        /// Create the underlying OS socket (AF_INET, SOCK_DGRAM).
        bool ensureSocket();
    };

    // ── TcpSocket ─────────────────────────────────────────────────────────────────

    /// TCP connected socket: send / recv / connect.
    class LUX_COMMUNICATION_PUBLIC TcpSocket
    {
    public:
        TcpSocket();
        explicit TcpSocket(socket_t fd);
        ~TcpSocket();

        TcpSocket(TcpSocket &&o) noexcept;
        TcpSocket &operator=(TcpSocket &&o) noexcept;

        TcpSocket(const TcpSocket &) = delete;
        TcpSocket &operator=(const TcpSocket &) = delete;

        /// Connect to a remote endpoint (blocking).
        bool connect(const std::string &addr, uint16_t port);

        // ── socket options ──

        bool setNonBlocking(bool on);
        bool setNoDelay(bool on);  // TCP_NODELAY
        bool setQuickAck(bool on); // TCP_QUICKACK  (Linux only; no-op on Windows)
        bool setSendBufferSize(int bytes);
        bool setRecvBufferSize(int bytes);

        // ── I/O ──

        /// Send data.  Returns bytes sent (may be partial in non-blocking mode), -1 on error.
        int send(const void *data, size_t len);

        /// Scatter-gather send.
        int sendV(const IoVec *iov, int iovcnt);

        /// Receive data.  Returns bytes received, 0 = peer closed, -1 = error/would-block.
        int recv(void *buf, size_t max_len);

        /// Blocking send that loops until all bytes are written.  Returns true on success.
        bool sendAll(const void *data, size_t len);

        /// Blocking recv that loops until exactly @p len bytes are read.
        /// Returns true on success, false on error/disconnect.
        bool recvAll(void *buf, size_t len);

        // ── accessors ──

        socket_t nativeFd() const { return sock_; }
        const std::string &remoteAddr() const { return remote_addr_; }
        uint16_t remotePort() const { return remote_port_; }
        bool isValid() const { return sock_ != kInvalidSocket; }
        void close();

    private:
        socket_t sock_ = kInvalidSocket;
        std::string remote_addr_;
        uint16_t remote_port_ = 0;
    };

    // ── TcpListener ───────────────────────────────────────────────────────────────

    /// TCP server socket: listen + accept.
    class LUX_COMMUNICATION_PUBLIC TcpListener
    {
    public:
        TcpListener();
        ~TcpListener();

        TcpListener(TcpListener &&o) noexcept;
        TcpListener &operator=(TcpListener &&o) noexcept;

        TcpListener(const TcpListener &) = delete;
        TcpListener &operator=(const TcpListener &) = delete;

        /// Bind and start listening.  port == 0 → OS-assigned.
        bool listen(const std::string &addr, uint16_t port, int backlog = 64);

        /// Listen on all interfaces (INADDR_ANY).
        bool listenAny(uint16_t port, int backlog = 64);

        bool setNonBlocking(bool on);

        /// Accept one pending connection.  Fills remote_addr/remote_port.
        /// Returns invalid TcpSocket if no pending connection (non-blocking) or error.
        TcpSocket accept(std::string &remote_addr, uint16_t &remote_port);

        // ── accessors ──

        socket_t nativeFd() const { return sock_; }
        uint16_t localPort() const;
        bool isValid() const { return sock_ != kInvalidSocket; }
        void close();

    private:
        socket_t sock_ = kInvalidSocket;
    };

} // namespace lux::communication::platform
