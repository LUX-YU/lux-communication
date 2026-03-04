/**
 * @file unified_transport_test.cpp
 * @brief Phase 5 — Unified Transport integration test.
 *
 * Tests:
 *  1. TransportSelector (same-pid, same-host, remote)
 *  2. IoThread register / unregister / poll callbacks
 *  3. Same-process pub/sub via unified Node (intra path)
 *  4. Multiple topics, multiple subscribers
 *  5. Executor integration (SingleThreadedExecutor + spinSome)
 *  6. Node stop() orderly shutdown
 */
#include <iostream>
#include <cassert>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include <string>

#include <lux/communication/ChannelKind.hpp>
#include <lux/communication/TransportSelector.hpp>
#include <lux/communication/NodeOptions.hpp>
#include <lux/communication/IoThread.hpp>
#include <lux/communication/transport/IoReactor.hpp>
#include <lux/communication/unified/Node.hpp>
#include <lux/communication/executor/SingleThreadedExecutor.hpp>
#include <lux/communication/platform/PlatformDefs.hpp>
#include <lux/communication/Domain.hpp>

#include <string>  // for HeapMsg

namespace comm = lux::communication;

static int  tests_passed = 0;
static int  tests_failed = 0;

#define CHECK(cond, msg) \
    do { \
        if (cond) { ++tests_passed; } \
        else { ++tests_failed; std::cerr << "  FAIL: " << msg << "\n"; } \
    } while(0)

// ─── Test 1: TransportSelector ──────────────────────────────────────────────

static void testTransportSelector()
{
    std::cout << "[TransportSelector] Testing selectTransport() ... ";

    comm::NodeOptions nopts;
    nopts.enable_shm = true;
    nopts.enable_net = true;
    nopts.enable_intra = true;

    auto my_pid  = comm::platform::currentPid();
    auto my_host = comm::platform::currentHostname();

    // Same PID + same hostname → Intra
    {
        comm::discovery::TopicEndpoint ep;
        ep.pid      = my_pid;
        ep.hostname = my_host;
        auto kind   = comm::selectTransport(ep, nopts);
        CHECK(kind == comm::ChannelKind::Intra,
              "same pid → Intra");
    }

    // Different PID + same hostname → Shm
    {
        comm::discovery::TopicEndpoint ep;
        ep.pid      = my_pid + 9999;
        ep.hostname = my_host;
        auto kind   = comm::selectTransport(ep, nopts);
        CHECK(kind == comm::ChannelKind::Shm,
              "different pid, same host → Shm");
    }

    // Different hostname → Net
    {
        comm::discovery::TopicEndpoint ep;
        ep.pid      = 42;
        ep.hostname = "remote-machine-xyz";
        ep.net_endpoint = "192.168.1.100:9000";
        auto kind   = comm::selectTransport(ep, nopts);
        CHECK(kind == comm::ChannelKind::Net,
              "different host → Net");
    }

    // Intra disabled → fall back to Shm (same pid, same host)
    {
        comm::NodeOptions n2 = nopts;
        n2.enable_intra = false;
        comm::discovery::TopicEndpoint ep;
        ep.pid      = my_pid;
        ep.hostname = my_host;
        auto kind   = comm::selectTransport(ep, n2);
        CHECK(kind == comm::ChannelKind::Shm,
              "intra disabled, same pid → Shm");
    }

    // SHM disabled → fall back to Net (same host, diff pid)
    {
        comm::NodeOptions n3 = nopts;
        n3.enable_shm = false;
        comm::discovery::TopicEndpoint ep;
        ep.pid      = my_pid + 9999;
        ep.hostname = my_host;
        ep.net_endpoint = "127.0.0.1:9000";
        auto kind   = comm::selectTransport(ep, n3);
        CHECK(kind == comm::ChannelKind::Net,
              "shm disabled, same host → Net");
    }

    std::cout << "OK (" << tests_passed << " checks)\n";
}

// ─── Test 2: IoThread register / unregister / fire ──────────────────────────

static void testIoThread()
{
    std::cout << "[IoThread] Testing register / unregister / poll ... ";
    int prior = tests_passed;

    comm::transport::IoReactor reactor;
    comm::NodeOptions nopts;
    nopts.shm_poll_interval_us  = 500;
    nopts.reactor_timeout_ms    = 1;

    comm::IoThread iot(reactor, nopts);

    std::atomic<int> poll_count{0};

    auto handle = iot.registerPoller([&poll_count]() {
        poll_count.fetch_add(1, std::memory_order_relaxed);
    });
    CHECK(handle != 0, "registerPoller returns nonzero handle");

    iot.start();
    CHECK(iot.isRunning(), "IoThread running after start()");

    // Wait a bit for the poller to fire.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    int c1 = poll_count.load();
    CHECK(c1 > 0, "Poll callback fired at least once");

    // Unregister and verify count stabilizes.
    iot.unregisterPoller(handle);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    int c2 = poll_count.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    int c3 = poll_count.load();
    CHECK(c3 == c2, "After unregister, count stabilized");

    iot.stop();
    CHECK(!iot.isRunning(), "IoThread stopped");

    std::cout << "OK (" << (tests_passed - prior) << " checks)\n";
}

// ─── Test 3: Unified Node — same-process pub/sub ───────────────────────────

struct SimpleMsg {
    int value;
};

static void testSameProcessPubSub()
{
    std::cout << "[UnifiedNode] Testing same-process pub/sub ... ";
    int prior = tests_passed;

    // Use a dedicated domain so we don't collide with other tests.
    comm::Domain domain(500);

    comm::NodeOptions nopts;
    nopts.enable_discovery = false; // Pure intra test.
    nopts.enable_shm       = false;
    nopts.enable_net       = false;

    comm::Node node("test_node", domain, nopts);

    std::atomic<int> received{0};
    int last_value = -1;

    auto pub = node.createPublisher<SimpleMsg>("test/topic");
    auto sub = node.createSubscriber<SimpleMsg>(
        "test/topic",
        [&](const SimpleMsg& msg) {
            last_value = msg.value;
            received.fetch_add(1, std::memory_order_relaxed);
        });

    CHECK(pub != nullptr, "Publisher created");
    CHECK(sub != nullptr, "Subscriber created");

    // Attach executor and spin in background thread (matching existing test pattern).
    comm::SingleThreadedExecutor executor;
    executor.addNode(&node);
    std::thread spin_th([&] { executor.spin(); });

    // Publish 10 messages.
    constexpr int N = 10;
    for (int i = 0; i < N; ++i) {
        SimpleMsg m{i};
        pub->publish(m);
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (received.load() < N &&
           std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    executor.stop();
    spin_th.join();

    CHECK(received.load() == N,
          "Received all messages (got " + std::to_string(received.load()) + "/" + std::to_string(N) + ")");
    CHECK(last_value == N - 1, "Last message correct");

    node.stop();
    std::cout << "OK (" << (tests_passed - prior) << " checks)\n";
}

// ─── Test 4: Multiple topics / subscribers ──────────────────────────────────

struct TopicA { int a; };
struct TopicB { int id; double value; };

static void testMultipleTopics()
{
    std::cout << "[UnifiedNode] Testing multiple topics ... ";
    int prior = tests_passed;

    comm::Domain domain(501);
    comm::NodeOptions nopts;
    nopts.enable_discovery = false;
    nopts.enable_shm       = false;
    nopts.enable_net       = false;

    comm::Node node("multi_test", domain, nopts);

    std::atomic<int> countA{0}, countB{0};

    auto pubA = node.createPublisher<TopicA>("topic/a");
    auto pubB = node.createPublisher<TopicB>("topic/b");

    auto subA = node.createSubscriber<TopicA>(
        "topic/a", [&](const TopicA&) { countA++; });
    auto subB = node.createSubscriber<TopicB>(
        "topic/b", [&](const TopicB&) { countB++; });

    // Two subscribers on same topic.
    std::atomic<int> countA2{0};
    auto subA2 = node.createSubscriber<TopicA>(
        "topic/a", [&](const TopicA&) { countA2++; });

    comm::SingleThreadedExecutor executor;
    executor.addNode(&node);
    std::thread spin_th([&] { executor.spin(); });

    for (int i = 0; i < 5; ++i)
        pubA->publish(TopicA{i});

    for (int i = 0; i < 3; ++i)
        pubB->publish(TopicB{1, 3.14});
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while ((countA.load() < 5 || countB.load() < 3 || countA2.load() < 5) &&
           std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    executor.stop();
    spin_th.join();

    CHECK(countA.load() == 5, "TopicA: 5 msgs received");
    CHECK(countA2.load() == 5, "TopicA subscriber 2: 5 msgs received");
    CHECK(countB.load() == 3, "TopicB: 3 msgs received");

    node.stop();
    std::cout << "OK (" << (tests_passed - prior) << " checks)\n";
}

// ─── Test 5: Publish with shared_ptr (zero-copy intra) ─────────────────────

/// Non-SmallValueMsg type — uses shared_ptr path, enabling zero-copy pointer test.
struct HeapMsg {
    std::string data;
    int value;
};

static void testZeroCopyPublish()
{
    std::cout << "[UnifiedNode] Testing shared_ptr publish (zero-copy) ... ";
    int prior = tests_passed;

    comm::Domain domain(502);
    comm::NodeOptions nopts;
    nopts.enable_discovery = false;
    nopts.enable_shm       = false;
    nopts.enable_net       = false;

    comm::Node node("zc_test", domain, nopts);

    const HeapMsg* received_ptr = nullptr;
    std::atomic<bool> got_it{false};

    auto pub = node.createPublisher<HeapMsg>("zc/topic");
    auto sub = node.createSubscriber<HeapMsg>(
        "zc/topic",
        [&](std::shared_ptr<HeapMsg> msg) {
            received_ptr = msg.get();
            got_it.store(true);
        });

    comm::SingleThreadedExecutor executor;
    executor.addNode(&node);
    std::thread spin_th([&] { executor.spin(); });

    auto msg = std::make_shared<HeapMsg>();
    msg->data = "hello";
    msg->value = 42;
    const HeapMsg* original_ptr = msg.get();
    pub->publish(msg);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!got_it.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    executor.stop();
    spin_th.join();

    CHECK(got_it.load(), "Message received");
    // With single subscriber sharing the same process, the pointer should be identical.
    CHECK(received_ptr == original_ptr, "Zero-copy: same pointer");

    node.stop();
    std::cout << "OK (" << (tests_passed - prior) << " checks)\n";
}

// ─── Test 6: Node stop() orderly shutdown ───────────────────────────────────

static void testNodeStop()
{
    std::cout << "[UnifiedNode] Testing orderly stop() ... ";
    int prior = tests_passed;

    comm::Domain domain(503);
    comm::NodeOptions nopts;
    nopts.enable_discovery = false;
    nopts.enable_shm       = false;
    nopts.enable_net       = false;

    {
        comm::Node node("stop_test", domain, nopts);

        auto pub = node.createPublisher<SimpleMsg>("stop/topic");
        auto sub = node.createSubscriber<SimpleMsg>(
            "stop/topic",
            [](const SimpleMsg&) {});

        pub->publish(SimpleMsg{1});

        node.stop();
        // After stop, publishing should still not crash (just no-op or queue silently).
        pub->publish(SimpleMsg{2});
        CHECK(true, "stop() completed without crash");
    }
    // Node destructor runs.
    CHECK(true, "Node destroyed without crash");

    std::cout << "OK (" << (tests_passed - prior) << " checks)\n";
}

// ─── Test 7: Emplace publish ────────────────────────────────────────────────

static void testEmplace()
{
    std::cout << "[UnifiedNode] Testing emplace publish ... ";
    int prior = tests_passed;

    comm::Domain domain(504);
    comm::NodeOptions nopts;
    nopts.enable_discovery = false;
    nopts.enable_shm       = false;
    nopts.enable_net       = false;

    comm::Node node("emplace_test", domain, nopts);

    std::atomic<int> received{0};
    int got_value = -1;

    auto pub = node.createPublisher<SimpleMsg>("emplace/topic");
    auto sub = node.createSubscriber<SimpleMsg>(
        "emplace/topic",
        [&](const SimpleMsg& msg) {
            got_value = msg.value;
            received++;
        });

    comm::SingleThreadedExecutor executor;
    executor.addNode(&node);
    std::thread spin_th([&] { executor.spin(); });

    pub->emplace(99);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (received.load() < 1 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    executor.stop();
    spin_th.join();

    CHECK(received.load() == 1, "Emplace message received");
    CHECK(got_value == 99, "Emplace message value correct");

    node.stop();
    std::cout << "OK (" << (tests_passed - prior) << " checks)\n";
}

// ─── main ───────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== Phase 5: Unified Transport Test ===\n\n";

    testTransportSelector();
    testIoThread();
    testSameProcessPubSub();
    testMultipleTopics();
    testZeroCopyPublish();
    testNodeStop();
    testEmplace();

    std::cout << "\n=== Results: "
              << tests_passed << " passed, "
              << tests_failed << " failed ===\n";

    return tests_failed > 0 ? 1 : 0;
}
