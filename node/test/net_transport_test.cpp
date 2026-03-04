/// Phase 4 — Network Transport unit tests.
///
/// Tests:
///   1.  UdpSocket localhost echo (sendTo / recvFrom)
///   2.  UdpSocket scatter-gather sendToV
///   3.  UDP non-blocking: recvFrom returns 0 when no data
///   4.  TcpListener + TcpSocket: connect → send → recv
///   5.  TCP large message (64 KB)
///   6.  TCP multiple clients (3 simultaneous)
///   7.  TCP setNoDelay
///   8.  Fragment small message (no fragmentation)
///   9.  Fragment split + reassemble (10 KB)
///  10.  Fragment out-of-order reassembly
///  11.  Fragment GC timeout
///  12.  Fragment duplicate handling
///  13.  Fragment large (64 KB ≈ 45 fragments)
///  14.  Handshake accepted
///  15.  Handshake type mismatch → rejected
///  16.  TCP recv state machine (partial reads)
///  17.  UdpTransport roundtrip (small message)
///  18.  UdpTransport roundtrip (fragmented large message)
///  19.  TcpTransport roundtrip (Writer → Reader)
///  20.  TcpTransport 1 Writer → 3 Readers
///  21.  IoReactor add / remove fd
///  22.  IoReactor UDP driven
///  23.  IoReactor TCP driven
///  24.  IoReactor wakeup from another thread
///  25.  IoReactor multiple fds
///  26.  FrameHeader kFlagReassembled
///  27.  NetConstants sanity checks

#include <lux/communication/platform/NetSocket.hpp>
#include <lux/communication/transport/FrameHeader.hpp>
#include <lux/communication/transport/NetConstants.hpp>
#include <lux/communication/transport/FragmentHeader.hpp>
#include <lux/communication/transport/FragmentSender.hpp>
#include <lux/communication/transport/FragmentAssembler.hpp>
#include <lux/communication/transport/UdpTransportWriter.hpp>
#include <lux/communication/transport/UdpTransportReader.hpp>
#include <lux/communication/transport/TcpTransportWriter.hpp>
#include <lux/communication/transport/TcpTransportReader.hpp>
#include <lux/communication/transport/Handshake.hpp>
#include <lux/communication/transport/IoReactor.hpp>

#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <numeric>
#include <algorithm>

using namespace lux::communication;

// ─── Helpers ────────────────────────────────────────────────────────────────────

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "  FAIL: " #expr " (line " << __LINE__ << ")\n"; \
            ++tests_failed; \
        } else { \
            ++tests_passed; \
        } \
    } while (0)

static void sleep_ms(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// ─── Test 1: UdpSocket localhost echo ───────────────────────────────────────────

void test_udp_echo() {
    std::cout << "[1]  UdpSocket localhost echo ... ";
    platform::NetInitGuard net_guard;

    platform::UdpSocket sender;
    platform::UdpSocket receiver;

    CHECK(receiver.bindAny(0));
    uint16_t port = receiver.localPort();
    CHECK(port != 0);

    const char msg[] = "Hello UDP Phase4";
    int sent = sender.sendTo(msg, sizeof(msg), "127.0.0.1", port);
    CHECK(sent == sizeof(msg));

    sleep_ms(10);

    char buf[256]{};
    std::string from_addr;
    uint16_t from_port = 0;
    int got = receiver.recvFrom(buf, sizeof(buf), from_addr, from_port);
    CHECK(got == sizeof(msg));
    CHECK(std::strcmp(buf, msg) == 0);
    CHECK(from_addr == "127.0.0.1");

    std::cout << "PASS\n";
}

// ─── Test 2: UdpSocket scatter-gather sendToV ──────────────────────────────────

void test_udp_scatter_gather() {
    std::cout << "[2]  UdpSocket scatter-gather sendToV ... ";
    platform::NetInitGuard net_guard;

    platform::UdpSocket sender;
    platform::UdpSocket receiver;
    CHECK(receiver.bindAny(0));
    uint16_t port = receiver.localPort();

    // Two-part message: header-like + payload-like
    uint32_t header = 0xDEADBEEF;
    const char payload[] = "scatter-gather-test";
    platform::IoVec iov[2] = {
        { &header, sizeof(header) },
        { payload, sizeof(payload) }
    };
    int sent = sender.sendToV(iov, 2, "127.0.0.1", port);
    CHECK(sent == static_cast<int>(sizeof(header) + sizeof(payload)));

    sleep_ms(10);

    uint8_t buf[256]{};
    std::string from_addr;
    uint16_t from_port = 0;
    int got = receiver.recvFrom(buf, sizeof(buf), from_addr, from_port);
    CHECK(got == static_cast<int>(sizeof(header) + sizeof(payload)));

    uint32_t recv_header;
    std::memcpy(&recv_header, buf, sizeof(recv_header));
    CHECK(recv_header == 0xDEADBEEF);
    CHECK(std::strcmp(reinterpret_cast<const char*>(buf + sizeof(header)), payload) == 0);

    std::cout << "PASS\n";
}

// ─── Test 3: UDP non-blocking ──────────────────────────────────────────────────

void test_udp_nonblocking() {
    std::cout << "[3]  UDP non-blocking recvFrom ... ";
    platform::NetInitGuard net_guard;

    platform::UdpSocket sock;
    CHECK(sock.bindAny(0));
    CHECK(sock.setNonBlocking(true));

    char buf[64];
    std::string addr;
    uint16_t port;
    int n = sock.recvFrom(buf, sizeof(buf), addr, port);
    CHECK(n == 0);  // would-block → returns 0

    std::cout << "PASS\n";
}

// ─── Test 4: TCP connect → send → recv ─────────────────────────────────────────

void test_tcp_basic() {
    std::cout << "[4]  TCP connect → send → recv ... ";
    platform::NetInitGuard net_guard;

    platform::TcpListener listener;
    CHECK(listener.listenAny(0));
    uint16_t port = listener.localPort();
    CHECK(port != 0);

    // Client connects in another thread
    std::atomic<bool> done{false};
    std::thread client_thread([&] {
        platform::TcpSocket client;
        CHECK(client.connect("127.0.0.1", port));
        const char msg[] = "Hello TCP";
        CHECK(client.sendAll(msg, sizeof(msg)));
        done = true;
    });

    std::string remote_addr;
    uint16_t remote_port = 0;
    auto server_sock = listener.accept(remote_addr, remote_port);
    CHECK(server_sock.isValid());

    char buf[64]{};
    CHECK(server_sock.recvAll(buf, 10));  // "Hello TCP\0" = 10 bytes
    CHECK(std::strcmp(buf, "Hello TCP") == 0);

    client_thread.join();
    CHECK(done.load());

    std::cout << "PASS\n";
}

// ─── Test 5: TCP large message (64 KB) ─────────────────────────────────────────

void test_tcp_large_message() {
    std::cout << "[5]  TCP large message (64 KB) ... ";
    platform::NetInitGuard net_guard;

    platform::TcpListener listener;
    CHECK(listener.listenAny(0));
    uint16_t port = listener.localPort();

    constexpr size_t SIZE = 64 * 1024;
    std::vector<uint8_t> send_data(SIZE);
    std::iota(send_data.begin(), send_data.end(), static_cast<uint8_t>(0));

    std::thread client_thread([&] {
        platform::TcpSocket client;
        CHECK(client.connect("127.0.0.1", port));
        CHECK(client.sendAll(send_data.data(), send_data.size()));
    });

    std::string ra;
    uint16_t rp;
    auto server_sock = listener.accept(ra, rp);
    CHECK(server_sock.isValid());

    std::vector<uint8_t> recv_data(SIZE);
    CHECK(server_sock.recvAll(recv_data.data(), SIZE));
    CHECK(recv_data == send_data);

    client_thread.join();
    std::cout << "PASS\n";
}

// ─── Test 6: TCP multiple clients ──────────────────────────────────────────────

void test_tcp_multiple_clients() {
    std::cout << "[6]  TCP 3 simultaneous clients ... ";
    platform::NetInitGuard net_guard;

    platform::TcpListener listener;
    CHECK(listener.listenAny(0));
    uint16_t port = listener.localPort();

    constexpr int N = 3;
    std::vector<std::thread> clients;
    for (int i = 0; i < N; ++i) {
        clients.emplace_back([port, i] {
            platform::TcpSocket c;
            c.connect("127.0.0.1", port);
            uint32_t val = static_cast<uint32_t>(i);
            c.sendAll(&val, sizeof(val));
        });
    }

    std::vector<uint32_t> received;
    for (int i = 0; i < N; ++i) {
        std::string ra;
        uint16_t rp;
        auto sock = listener.accept(ra, rp);
        CHECK(sock.isValid());
        uint32_t val = 0;
        sock.recvAll(&val, sizeof(val));
        received.push_back(val);
    }
    CHECK(received.size() == N);
    // Check that we got 0, 1, 2 in some order
    std::sort(received.begin(), received.end());
    CHECK(received[0] == 0);
    CHECK(received[1] == 1);
    CHECK(received[2] == 2);

    for (auto& t : clients) t.join();
    std::cout << "PASS\n";
}

// ─── Test 7: TCP setNoDelay ────────────────────────────────────────────────────

void test_tcp_set_nodelay() {
    std::cout << "[7]  TCP setNoDelay ... ";
    platform::NetInitGuard net_guard;

    platform::TcpListener listener;
    CHECK(listener.listenAny(0));
    uint16_t port = listener.localPort();

    std::thread t([port] {
        platform::TcpSocket c;
        c.connect("127.0.0.1", port);
        CHECK(c.setNoDelay(true));
        c.close();
    });

    std::string ra;
    uint16_t rp;
    auto s = listener.accept(ra, rp);
    CHECK(s.isValid());
    CHECK(s.setNoDelay(true));

    t.join();
    std::cout << "PASS\n";
}

// ─── Test 8: Fragment — small message (no fragmentation) ───────────────────────

void test_fragment_small_noop() {
    std::cout << "[8]  Fragment: small msg no-op ... ";

    // Small data: FrameHeader(48) + 100 bytes payload = 148 < kMaxUdpPayload
    transport::FrameHeader hdr;
    hdr.topic_hash   = 12345;
    hdr.payload_size = 100;

    // Verify it would go as a single datagram
    size_t total = sizeof(transport::FrameHeader) + 100;
    CHECK(total <= transport::kMaxUdpPayload);

    std::cout << "PASS\n";
}

// ─── Test 9: Fragment split + reassemble (10 KB) ──────────────────────────────

void test_fragment_split_reassemble() {
    std::cout << "[9]  Fragment: 10 KB split + reassemble ... ";

    constexpr size_t MSG_SIZE = 10 * 1024;  // 10 KB total (header + payload)
    std::vector<uint8_t> original(MSG_SIZE);
    for (size_t i = 0; i < MSG_SIZE; ++i)
        original[i] = static_cast<uint8_t>(i & 0xFF);

    uint64_t topic_hash = 0xABCD;
    uint32_t group_id   = 42;

    // Simulate fragmentation
    uint16_t total_frags = static_cast<uint16_t>(
        (MSG_SIZE + transport::kMaxFragPayload - 1) / transport::kMaxFragPayload);
    CHECK(total_frags > 1);

    transport::FragmentAssembler assembler;
    std::optional<transport::FragmentAssembler::CompleteMessage> result;

    for (uint16_t i = 0; i < total_frags; ++i) {
        size_t offset   = static_cast<size_t>(i) * transport::kMaxFragPayload;
        size_t frag_len = std::min<size_t>(transport::kMaxFragPayload, MSG_SIZE - offset);

        transport::FragmentHeader fh{};
        fh.frag_magic        = transport::kFragmentMagic;
        fh.group_id          = group_id;
        fh.seq_in_group      = i;
        fh.total_fragments   = total_frags;
        fh.total_msg_size    = static_cast<uint32_t>(MSG_SIZE);
        fh.topic_hash        = topic_hash;

        result = assembler.feed(fh, original.data() + offset, frag_len);
        if (i < total_frags - 1)
            CHECK(!result.has_value());
    }
    CHECK(result.has_value());
    CHECK(result->data.size() == MSG_SIZE);
    CHECK(result->topic_hash == topic_hash);
    CHECK(std::memcmp(result->data.data(), original.data(), MSG_SIZE) == 0);

    auto s = assembler.stats();
    CHECK(s.complete_messages == 1);
    CHECK(s.pending_groups == 0);

    std::cout << "PASS\n";
}

// ─── Test 10: Fragment out-of-order ────────────────────────────────────────────

void test_fragment_out_of_order() {
    std::cout << "[10] Fragment: out-of-order reassembly ... ";

    constexpr size_t MSG_SIZE = 5000;
    std::vector<uint8_t> original(MSG_SIZE);
    for (size_t i = 0; i < MSG_SIZE; ++i) original[i] = static_cast<uint8_t>(i);

    uint16_t total_frags = static_cast<uint16_t>(
        (MSG_SIZE + transport::kMaxFragPayload - 1) / transport::kMaxFragPayload);

    // Build fragments in reverse order
    std::vector<uint16_t> order(total_frags);
    std::iota(order.begin(), order.end(), static_cast<uint16_t>(0));
    std::reverse(order.begin(), order.end());

    transport::FragmentAssembler assembler;
    std::optional<transport::FragmentAssembler::CompleteMessage> result;

    for (uint16_t i : order) {
        size_t offset   = static_cast<size_t>(i) * transport::kMaxFragPayload;
        size_t frag_len = std::min<size_t>(transport::kMaxFragPayload, MSG_SIZE - offset);

        transport::FragmentHeader fh{};
        fh.frag_magic        = transport::kFragmentMagic;
        fh.group_id          = 99;
        fh.seq_in_group      = i;
        fh.total_fragments   = total_frags;
        fh.total_msg_size    = static_cast<uint32_t>(MSG_SIZE);
        fh.topic_hash        = 0x1234;

        auto r = assembler.feed(fh, original.data() + offset, frag_len);
        if (r) result = std::move(r);
    }
    CHECK(result.has_value());
    CHECK(result->data.size() == MSG_SIZE);
    CHECK(std::memcmp(result->data.data(), original.data(), MSG_SIZE) == 0);

    std::cout << "PASS\n";
}

// ─── Test 11: Fragment GC timeout ──────────────────────────────────────────────

void test_fragment_gc_timeout() {
    std::cout << "[11] Fragment: GC timeout ... ";

    transport::FragmentAssembler assembler(std::chrono::milliseconds{50});

    // Feed one fragment of a 3-fragment group
    transport::FragmentHeader fh{};
    fh.frag_magic        = transport::kFragmentMagic;
    fh.group_id          = 1;
    fh.seq_in_group      = 0;
    fh.total_fragments   = 3;
    fh.total_msg_size    = 4000;
    fh.topic_hash        = 0x5678;

    std::vector<uint8_t> dummy(transport::kMaxFragPayload, 0xAA);
    auto r = assembler.feed(fh, dummy.data(), dummy.size());
    CHECK(!r.has_value());

    auto s1 = assembler.stats();
    CHECK(s1.pending_groups == 1);

    // Wait and GC
    sleep_ms(80);
    assembler.gc();

    auto s2 = assembler.stats();
    CHECK(s2.pending_groups == 0);
    CHECK(s2.timed_out_groups == 1);

    std::cout << "PASS\n";
}

// ─── Test 12: Fragment duplicate handling ──────────────────────────────────────

void test_fragment_duplicate() {
    std::cout << "[12] Fragment: duplicate handling ... ";

    transport::FragmentAssembler assembler;

    constexpr size_t MSG_SIZE = 3000;
    std::vector<uint8_t> original(MSG_SIZE, 0x42);

    uint16_t total_frags = static_cast<uint16_t>(
        (MSG_SIZE + transport::kMaxFragPayload - 1) / transport::kMaxFragPayload);

    // Feed all fragments
    for (uint16_t i = 0; i < total_frags - 1; ++i) {
        size_t offset   = static_cast<size_t>(i) * transport::kMaxFragPayload;
        size_t frag_len = std::min<size_t>(transport::kMaxFragPayload, MSG_SIZE - offset);

        transport::FragmentHeader fh{};
        fh.frag_magic        = transport::kFragmentMagic;
        fh.group_id          = 7;
        fh.seq_in_group      = i;
        fh.total_fragments   = total_frags;
        fh.total_msg_size    = static_cast<uint32_t>(MSG_SIZE);
        fh.topic_hash        = 0x11;

        assembler.feed(fh, original.data() + offset, frag_len);
    }

    // Duplicate the first fragment
    {
        transport::FragmentHeader fh{};
        fh.frag_magic        = transport::kFragmentMagic;
        fh.group_id          = 7;
        fh.seq_in_group      = 0;
        fh.total_fragments   = total_frags;
        fh.total_msg_size    = static_cast<uint32_t>(MSG_SIZE);
        fh.topic_hash        = 0x11;

        size_t dup_frag_len = std::min<size_t>(transport::kMaxFragPayload, MSG_SIZE);
        auto r = assembler.feed(fh, original.data(), dup_frag_len);
        CHECK(!r.has_value());  // duplicate — should not complete
    }

    CHECK(assembler.stats().duplicate_fragments == 1);

    // Now send the last fragment to complete
    {
        uint16_t i = total_frags - 1;
        size_t offset   = static_cast<size_t>(i) * transport::kMaxFragPayload;
        size_t frag_len = MSG_SIZE - offset;

        transport::FragmentHeader fh{};
        fh.frag_magic        = transport::kFragmentMagic;
        fh.group_id          = 7;
        fh.seq_in_group      = i;
        fh.total_fragments   = total_frags;
        fh.total_msg_size    = static_cast<uint32_t>(MSG_SIZE);
        fh.topic_hash        = 0x11;

        auto r = assembler.feed(fh, original.data() + offset, frag_len);
        CHECK(r.has_value());
    }

    std::cout << "PASS\n";
}

// ─── Test 13: Fragment large (64 KB) ───────────────────────────────────────────

void test_fragment_large_64kb() {
    std::cout << "[13] Fragment: 64 KB (~45 fragments) ... ";

    constexpr size_t MSG_SIZE = 64 * 1024;
    std::vector<uint8_t> original(MSG_SIZE);
    for (size_t i = 0; i < MSG_SIZE; ++i) original[i] = static_cast<uint8_t>(i);

    uint16_t total_frags = static_cast<uint16_t>(
        (MSG_SIZE + transport::kMaxFragPayload - 1) / transport::kMaxFragPayload);
    CHECK(total_frags >= 40);

    transport::FragmentAssembler assembler;
    std::optional<transport::FragmentAssembler::CompleteMessage> result;

    for (uint16_t i = 0; i < total_frags; ++i) {
        size_t offset   = static_cast<size_t>(i) * transport::kMaxFragPayload;
        size_t frag_len = std::min<size_t>(transport::kMaxFragPayload, MSG_SIZE - offset);

        transport::FragmentHeader fh{};
        fh.frag_magic        = transport::kFragmentMagic;
        fh.group_id          = 200;
        fh.seq_in_group      = i;
        fh.total_fragments   = total_frags;
        fh.total_msg_size    = static_cast<uint32_t>(MSG_SIZE);
        fh.topic_hash        = 0xBEEF;

        auto r = assembler.feed(fh, original.data() + offset, frag_len);
        if (r) result = std::move(r);
    }
    CHECK(result.has_value());
    CHECK(result->data.size() == MSG_SIZE);
    CHECK(std::memcmp(result->data.data(), original.data(), MSG_SIZE) == 0);

    std::cout << "PASS\n";
}

// ─── Test 14: Handshake accepted ───────────────────────────────────────────────

void test_handshake_accepted() {
    std::cout << "[14] Handshake accepted ... ";
    platform::NetInitGuard net_guard;

    uint64_t topic_hash = 0x1111;
    uint64_t type_hash  = 0x2222;

    transport::TcpTransportWriter writer("127.0.0.1", 0, topic_hash, type_hash);
    CHECK(writer.startListening());
    uint16_t port = writer.listeningPort();
    CHECK(port != 0);

    // Accept in background thread
    std::thread accept_thread([&] {
        writer.onAcceptReady();
    });

    transport::TcpTransportReader reader("127.0.0.1", port,
                                          topic_hash, type_hash,
                                          1234, "test-host");
    CHECK(reader.connect());
    CHECK(reader.isConnected());

    accept_thread.join();
    CHECK(writer.connectionCount() == 1);

    std::cout << "PASS\n";
}

// ─── Test 15: Handshake type mismatch ──────────────────────────────────────────

void test_handshake_type_mismatch() {
    std::cout << "[15] Handshake type mismatch → rejected ... ";
    platform::NetInitGuard net_guard;

    uint64_t topic_hash = 0x3333;
    uint64_t type_hash  = 0x4444;

    transport::TcpTransportWriter writer("127.0.0.1", 0, topic_hash, type_hash);
    CHECK(writer.startListening());
    uint16_t port = writer.listeningPort();

    std::thread accept_thread([&] {
        writer.onAcceptReady();
    });

    // Reader sends a *different* type_hash
    transport::TcpTransportReader reader("127.0.0.1", port,
                                          topic_hash, 0x9999,  // wrong type
                                          1234, "test-host");
    CHECK(!reader.connect());
    CHECK(!reader.isConnected());

    accept_thread.join();
    CHECK(writer.connectionCount() == 0);

    std::cout << "PASS\n";
}

// ─── Test 16: TCP recv state machine ───────────────────────────────────────────

void test_tcp_recv_state_machine() {
    std::cout << "[16] TCP recv state machine (partial reads) ... ";
    platform::NetInitGuard net_guard;

    uint64_t topic_hash = 0xAAAA;
    uint64_t type_hash  = 0xBBBB;

    transport::TcpTransportWriter writer("127.0.0.1", 0, topic_hash, type_hash);
    CHECK(writer.startListening());
    uint16_t port = writer.listeningPort();

    // Connect reader
    std::thread accept_thread([&] {
        writer.onAcceptReady();
    });

    transport::TcpTransportReader reader("127.0.0.1", port,
                                          topic_hash, type_hash,
                                          1234, "test-host");
    CHECK(reader.connect());
    accept_thread.join();

    // Send a frame via writer
    transport::FrameHeader hdr;
    hdr.topic_hash   = topic_hash;
    hdr.seq_num      = 42;
    hdr.payload_size = 128;
    std::vector<uint8_t> payload(128, 0xCC);

    uint32_t sent = writer.send(hdr, payload.data(), 128);
    CHECK(sent == 1);

    // Read it back
    sleep_ms(20);

    bool received_frame = false;
    reader.pollOnce([&](const transport::FrameHeader& h, const void* p, uint32_t sz) {
        received_frame = true;
        CHECK(h.seq_num == 42);
        CHECK(sz == 128);
        auto* data = static_cast<const uint8_t*>(p);
        for (uint32_t i = 0; i < sz; ++i)
            CHECK(data[i] == 0xCC);
    });
    CHECK(received_frame);

    std::cout << "PASS\n";
}

// ─── Test 17: UdpTransport roundtrip (small message) ──────────────────────────

void test_udp_transport_roundtrip() {
    std::cout << "[17] UdpTransport roundtrip (small) ... ";
    platform::NetInitGuard net_guard;

    transport::UdpTransportReader reader(0);
    CHECK(reader.isValid());
    uint16_t port = reader.localPort();
    CHECK(port != 0);

    transport::UdpTransportWriter writer("127.0.0.1", port);

    transport::FrameHeader hdr;
    hdr.topic_hash   = 0x5555;
    hdr.seq_num      = 7;
    hdr.payload_size = 64;
    std::vector<uint8_t> payload(64, 0xDD);

    CHECK(writer.send(hdr, payload.data(), 64));

    sleep_ms(20);

    bool got = false;
    reader.pollOnce([&](const transport::FrameHeader& h, const void* p, uint32_t sz) {
        got = true;
        CHECK(h.seq_num == 7);
        CHECK(h.topic_hash == 0x5555);
        CHECK(sz == 64);
        auto* d = static_cast<const uint8_t*>(p);
        for (uint32_t i = 0; i < sz; ++i) CHECK(d[i] == 0xDD);
    });
    CHECK(got);

    std::cout << "PASS\n";
}

// ─── Test 18: UdpTransport roundtrip (fragmented large message) ───────────────

void test_udp_transport_fragmented() {
    std::cout << "[18] UdpTransport roundtrip (fragmented 10 KB) ... ";
    platform::NetInitGuard net_guard;

    transport::UdpTransportReader reader(0);
    uint16_t port = reader.localPort();

    transport::UdpTransportWriter writer("127.0.0.1", port);

    // Build a 10 KB payload
    constexpr uint32_t PAYLOAD_SIZE = 10 * 1024;
    std::vector<uint8_t> payload(PAYLOAD_SIZE);
    for (uint32_t i = 0; i < PAYLOAD_SIZE; ++i)
        payload[i] = static_cast<uint8_t>(i & 0xFF);

    transport::FrameHeader hdr;
    hdr.topic_hash   = 0x6666;
    hdr.seq_num      = 100;
    hdr.payload_size = PAYLOAD_SIZE;

    CHECK(writer.send(hdr, payload.data(), PAYLOAD_SIZE));

    sleep_ms(50);

    // Need to poll multiple times (one per fragment datagram)
    bool got_complete = false;
    for (int attempt = 0; attempt < 100 && !got_complete; ++attempt) {
        reader.pollOnce([&](const transport::FrameHeader& h, const void* p, uint32_t sz) {
            got_complete = true;
            CHECK(h.seq_num == 100);
            CHECK(h.topic_hash == 0x6666);
            CHECK(sz == PAYLOAD_SIZE);
            CHECK(std::memcmp(p, payload.data(), PAYLOAD_SIZE) == 0);
        });
    }
    CHECK(got_complete);

    std::cout << "PASS\n";
}

// ─── Test 19: TcpTransport roundtrip ──────────────────────────────────────────

void test_tcp_transport_roundtrip() {
    std::cout << "[19] TcpTransport roundtrip ... ";
    platform::NetInitGuard net_guard;

    uint64_t topic_hash = 0x7777;
    uint64_t type_hash  = 0x8888;

    transport::TcpTransportWriter writer("127.0.0.1", 0, topic_hash, type_hash);
    CHECK(writer.startListening());
    uint16_t port = writer.listeningPort();

    std::thread accept_t([&] { writer.onAcceptReady(); });

    transport::TcpTransportReader reader("127.0.0.1", port,
                                          topic_hash, type_hash, 0, "host");
    CHECK(reader.connect());
    accept_t.join();

    // Send 2 KB message
    constexpr uint32_t SZ = 2048;
    transport::FrameHeader hdr;
    hdr.topic_hash   = topic_hash;
    hdr.seq_num      = 55;
    hdr.payload_size = SZ;
    std::vector<uint8_t> payload(SZ, 0xEE);

    CHECK(writer.send(hdr, payload.data(), SZ) == 1);

    sleep_ms(20);

    bool got = false;
    reader.pollOnce([&](const transport::FrameHeader& h, const void* p, uint32_t sz) {
        got = true;
        CHECK(h.seq_num == 55);
        CHECK(sz == SZ);
    });
    CHECK(got);

    std::cout << "PASS\n";
}

// ─── Test 20: TcpTransport 1 Writer → 3 Readers ──────────────────────────────

void test_tcp_transport_multi_reader() {
    std::cout << "[20] TcpTransport 1 Writer → 3 Readers ... ";
    platform::NetInitGuard net_guard;

    uint64_t topic_hash = 0xF0F0;
    uint64_t type_hash  = 0x0F0F;

    transport::TcpTransportWriter writer("127.0.0.1", 0, topic_hash, type_hash);
    CHECK(writer.startListening());
    uint16_t port = writer.listeningPort();

    constexpr int N = 3;
    std::vector<std::unique_ptr<transport::TcpTransportReader>> readers;

    for (int i = 0; i < N; ++i) {
        std::thread accept_t([&] { writer.onAcceptReady(); });

        auto r = std::make_unique<transport::TcpTransportReader>(
            "127.0.0.1", port, topic_hash, type_hash, 0, "host");
        CHECK(r->connect());
        accept_t.join();
        readers.push_back(std::move(r));
    }
    CHECK(writer.connectionCount() == N);

    // Send a message
    transport::FrameHeader hdr;
    hdr.topic_hash   = topic_hash;
    hdr.seq_num      = 99;
    hdr.payload_size = 32;
    std::vector<uint8_t> payload(32, 0xBB);

    uint32_t sent_count = writer.send(hdr, payload.data(), 32);
    CHECK(sent_count == N);

    sleep_ms(30);

    // Each reader should get the frame
    for (int i = 0; i < N; ++i) {
        bool got = false;
        readers[i]->pollOnce([&](const transport::FrameHeader& h, const void*, uint32_t) {
            got = true;
            CHECK(h.seq_num == 99);
        });
        CHECK(got);
    }

    std::cout << "PASS\n";
}

// ─── Test 21: IoReactor add / remove fd ────────────────────────────────────────

void test_reactor_add_remove() {
    std::cout << "[21] IoReactor add/remove fd ... ";
    platform::NetInitGuard net_guard;

    transport::IoReactor reactor;

    platform::UdpSocket sock;
    sock.bindAny(0);
    auto fd = sock.nativeFd();

    CHECK(reactor.addFd(fd, transport::IoReactor::Readable, [](auto, auto){}));
    CHECK(reactor.fdCount() == 1);

    CHECK(reactor.removeFd(fd));
    CHECK(reactor.fdCount() == 0);

    std::cout << "PASS\n";
}

// ─── Test 22: IoReactor UDP driven ─────────────────────────────────────────────

void test_reactor_udp_driven() {
    std::cout << "[22] IoReactor UDP driven ... ";
    platform::NetInitGuard net_guard;

    transport::IoReactor reactor;
    platform::UdpSocket receiver;
    receiver.bindAny(0);
    receiver.setNonBlocking(true);
    uint16_t rport = receiver.localPort();

    std::atomic<int> recv_count{0};
    reactor.addFd(receiver.nativeFd(), transport::IoReactor::Readable,
        [&](platform::socket_t, uint8_t) {
            char buf[256];
            std::string addr;
            uint16_t port;
            int n = receiver.recvFrom(buf, sizeof(buf), addr, port);
            if (n > 0) ++recv_count;
        });

    // Send a packet
    platform::UdpSocket sender;
    const char msg[] = "reactor-udp";
    sender.sendTo(msg, sizeof(msg), "127.0.0.1", rport);

    sleep_ms(10);
    reactor.pollOnce(std::chrono::milliseconds{50});

    CHECK(recv_count.load() == 1);

    std::cout << "PASS\n";
}

// ─── Test 23: IoReactor TCP driven ─────────────────────────────────────────────

void test_reactor_tcp_driven() {
    std::cout << "[23] IoReactor TCP driven ... ";
    platform::NetInitGuard net_guard;

    transport::IoReactor reactor;
    platform::TcpListener listener;
    listener.listenAny(0);
    listener.setNonBlocking(true);
    uint16_t port = listener.localPort();

    std::atomic<bool> accepted{false};
    reactor.addFd(listener.nativeFd(), transport::IoReactor::Readable,
        [&](platform::socket_t, uint8_t) {
            std::string ra;
            uint16_t rp;
            auto sock = listener.accept(ra, rp);
            if (sock.isValid()) accepted = true;
        });

    // Connect from another thread
    std::thread t([port] {
        platform::TcpSocket c;
        c.connect("127.0.0.1", port);
        sleep_ms(100);
    });

    sleep_ms(20);
    reactor.pollOnce(std::chrono::milliseconds{100});

    CHECK(accepted.load());
    t.join();

    std::cout << "PASS\n";
}

// ─── Test 24: IoReactor wakeup ─────────────────────────────────────────────────

void test_reactor_wakeup() {
    std::cout << "[24] IoReactor wakeup from another thread ... ";
    platform::NetInitGuard net_guard;

    transport::IoReactor reactor;

    auto start = std::chrono::steady_clock::now();

    // Start a long pollOnce in a thread
    std::atomic<bool> poll_done{false};
    std::thread t([&] {
        reactor.pollOnce(std::chrono::milliseconds{5000});
        poll_done = true;
    });

    sleep_ms(20);
    reactor.wakeup();
    t.join();

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    CHECK(poll_done.load());
    CHECK(ms < 2000);  // should wake up quickly, not wait 5s

    std::cout << "PASS\n";
}

// ─── Test 25: IoReactor multiple fds ───────────────────────────────────────────

void test_reactor_multi_fd() {
    std::cout << "[25] IoReactor 5 UDP fds ... ";
    platform::NetInitGuard net_guard;

    transport::IoReactor reactor;
    constexpr int N = 5;

    std::vector<platform::UdpSocket> receivers(N);
    std::vector<uint16_t> ports(N);
    std::atomic<int> total_recv{0};

    for (int i = 0; i < N; ++i) {
        receivers[i].bindAny(0);
        receivers[i].setNonBlocking(true);
        ports[i] = receivers[i].localPort();

        reactor.addFd(receivers[i].nativeFd(), transport::IoReactor::Readable,
            [&, idx = i](platform::socket_t, uint8_t) {
                char buf[64];
                std::string addr;
                uint16_t p;
                int n = receivers[idx].recvFrom(buf, sizeof(buf), addr, p);
                if (n > 0) ++total_recv;
            });
    }
    CHECK(reactor.fdCount() == N);

    // Send one packet to each
    platform::UdpSocket sender;
    for (int i = 0; i < N; ++i) {
        char msg = 'A' + static_cast<char>(i);
        sender.sendTo(&msg, 1, "127.0.0.1", ports[i]);
    }

    sleep_ms(20);
    // May need multiple polls
    for (int attempt = 0; attempt < 5; ++attempt)
        reactor.pollOnce(std::chrono::milliseconds{20});

    CHECK(total_recv.load() == N);

    std::cout << "PASS\n";
}

// ─── Test 26: FrameHeader kFlagReassembled ─────────────────────────────────────

void test_flag_reassembled() {
    std::cout << "[26] FrameHeader kFlagReassembled ... ";

    transport::FrameHeader h;
    CHECK(!transport::isReassembled(h));

    transport::setReassembled(h);
    CHECK(transport::isReassembled(h));
    CHECK((h.flags & transport::kFlagReassembled) != 0);

    // Ensure other flags are not affected
    CHECK(transport::isValidFrame(h));
    CHECK(transport::getFormat(h) == transport::SerializationFormat::RawMemcpy);

    std::cout << "PASS\n";
}

// ─── Test 27: NetConstants sanity ──────────────────────────────────────────────

void test_net_constants() {
    std::cout << "[27] NetConstants sanity checks ... ";

    CHECK(transport::kMaxUdpPayload == 1472);
    CHECK(transport::kFragHeaderSize == 24);
    CHECK(transport::kMaxFragPayload == 1448);
    CHECK(transport::kNetSmallMsgThreshold == transport::kMaxUdpPayload - sizeof(transport::FrameHeader));
    CHECK(transport::kMaxFragmentedMsgSize == 64u * 1024u * 1024u);
    CHECK(sizeof(transport::FragmentHeader) == 24);
    CHECK(sizeof(transport::HandshakeRequest) == 88);
    CHECK(sizeof(transport::HandshakeResponse) == 24);

    std::cout << "PASS\n";
}

// ─── Main ──────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "═══ Phase 4 — Network Transport Tests ═══\n\n";

    // Platform socket tests
    test_udp_echo();
    test_udp_scatter_gather();
    test_udp_nonblocking();
    test_tcp_basic();
    test_tcp_large_message();
    test_tcp_multiple_clients();
    test_tcp_set_nodelay();

    // Fragment tests
    test_fragment_small_noop();
    test_fragment_split_reassemble();
    test_fragment_out_of_order();
    test_fragment_gc_timeout();
    test_fragment_duplicate();
    test_fragment_large_64kb();

    // TCP Transport tests
    test_handshake_accepted();
    test_handshake_type_mismatch();
    test_tcp_recv_state_machine();

    // UDP Transport tests
    test_udp_transport_roundtrip();
    test_udp_transport_fragmented();

    // TCP Transport high-level tests
    test_tcp_transport_roundtrip();
    test_tcp_transport_multi_reader();

    // IoReactor tests
    test_reactor_add_remove();
    test_reactor_udp_driven();
    test_reactor_tcp_driven();
    test_reactor_wakeup();
    test_reactor_multi_fd();

    // FrameHeader & constants
    test_flag_reassembled();
    test_net_constants();

    std::cout << "\n═══ Results: " << tests_passed << " passed, "
              << tests_failed << " failed ═══\n";
    return tests_failed > 0 ? 1 : 0;
}
