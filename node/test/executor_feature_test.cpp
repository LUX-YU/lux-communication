/**
 * @file executor_feature_test.cpp
 * @brief Executor feature tests covering undertested scenarios:
 *
 *  1. Cross-executor communication (same topic, different executors)
 *  2. Executor lifecycle (stop/restart, idempotent stop, destroy during spin)
 *  3. spinSome vs spin consistency (both consume all messages)
 *  4. Callback group mixing (Reentrant + MutuallyExclusive in same executor)
 *  5. Chain/pipeline topology (A→B→C message forwarding)
 *  6. Empty queue spinSome (returns immediately)
 *  7. Multi-node sharing one executor
 *  8. Late subscriber (subscriber created after messages published)
 */

#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <mutex>
#include <cassert>
#include <set>

#include <lux/communication/Node.hpp>
#include <lux/communication/CallbackGroupBase.hpp>
#include <lux/communication/executor/SingleThreadedExecutor.hpp>
#include <lux/communication/executor/MultiThreadedExecutor.hpp>
#include <lux/communication/executor/SeqOrderedExecutor.hpp>
#include <lux/communication/executor/TimeOrderedExecutor.hpp>

namespace comm = lux::communication;

static comm::NodeOptions intraOpts()
{
    return {.enable_discovery = false, .enable_shm = false, .enable_net = false};
}

static int g_pass = 0;
static int g_fail = 0;

static void check(bool cond, const char* name, const char* detail = nullptr)
{
    if (cond)
    {
        ++g_pass;
        std::cout << "  [PASS] " << name;
    }
    else
    {
        ++g_fail;
        std::cout << "  [FAIL] " << name;
    }
    if (detail)
        std::cout << " — " << detail;
    std::cout << "\n";
}

// ═══════════════════════════════════════════════════════════════
// Test 1: Cross-Executor Communication
//   Two nodes in same domain, each on a different executor,
//   subscribe to the same topic published by a third node.
// ═══════════════════════════════════════════════════════════════
static void testCrossExecutor()
{
    std::cout << "\n=== Test 1: Cross-Executor Communication ===\n";

    comm::Domain domain(100);
    comm::Node pub_node("pub", domain, intraOpts());
    comm::Node sub_node1("sub1", domain, intraOpts());
    comm::Node sub_node2("sub2", domain, intraOpts());

    constexpr int N = 10000;
    std::atomic<int> count1{0}, count2{0};

    auto pub = pub_node.createPublisher<int>("/cross_exec");
    auto sub1 = sub_node1.createSubscriber<int>("/cross_exec",
        [&](const int&) { count1.fetch_add(1, std::memory_order_relaxed); });
    auto sub2 = sub_node2.createSubscriber<int>("/cross_exec",
        [&](const int&) { count2.fetch_add(1, std::memory_order_relaxed); });

    // Executor 1: SingleThreaded for sub_node1
    comm::SingleThreadedExecutor exec1;
    exec1.addNode(&sub_node1);

    // Executor 2: MultiThreaded for sub_node2
    comm::MultiThreadedExecutor exec2(2);
    exec2.addNode(&sub_node2);

    std::thread t1([&] { exec1.spin(); });
    std::thread t2([&] { exec2.spin(); });

    for (int i = 0; i < N; ++i)
        pub->publish(i);

    // Wait for both to finish
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while ((count1.load() < N || count2.load() < N) &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();

    exec1.stop(); exec2.stop();
    t1.join(); t2.join();

    check(count1.load() == N, "SingleThreaded subscriber got all msgs",
          (std::to_string(count1.load()) + "/" + std::to_string(N)).c_str());
    check(count2.load() == N, "MultiThreaded subscriber got all msgs",
          (std::to_string(count2.load()) + "/" + std::to_string(N)).c_str());

    pub_node.stop(); sub_node1.stop(); sub_node2.stop();
}

// ═══════════════════════════════════════════════════════════════
// Test 2: Executor Lifecycle
//   2a. Stop is idempotent — calling stop() twice should not crash
//   2b. Destroy executor during spin() — destructor stops cleanly
//   2c. Reuse node with new executor after old executor stopped
// ═══════════════════════════════════════════════════════════════
static void testExecutorLifecycle()
{
    std::cout << "\n=== Test 2: Executor Lifecycle ===\n";

    comm::Domain domain(101);
    comm::Node node("life", domain, intraOpts());

    std::atomic<int> count{0};
    auto pub = node.createPublisher<double>("/lifecycle");
    auto sub = node.createSubscriber<double>("/lifecycle",
        [&](const double&) { count.fetch_add(1, std::memory_order_relaxed); });

    // 2a: Idempotent stop
    {
        comm::SingleThreadedExecutor exec;
        exec.addNode(&node);
        std::thread t([&] { exec.spin(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        exec.stop();
        exec.stop(); // second stop — should not crash
        t.join();
        exec.removeNode(&node);
        check(true, "Idempotent stop() — no crash");
    }

    // 2b: Destroy executor while spin() is running
    count.store(0);
    {
        auto exec = std::make_unique<comm::SingleThreadedExecutor>();
        exec->addNode(&node);
        std::thread t([&] { exec->spin(); });
        pub->publish(1.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        exec.reset(); // destructor calls stop()
        t.join();
        check(count.load() >= 1, "Destroy during spin() — no crash, msgs delivered",
              (std::to_string(count.load()) + " received").c_str());
    }

    // 2c: Reuse node with new executor
    count.store(0);
    {
        comm::SingleThreadedExecutor exec2;
        exec2.addNode(&node);
        std::thread t([&] { exec2.spin(); });

        for (int i = 0; i < 100; ++i) pub->publish(2.0);

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (count.load() < 100 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();

        exec2.stop(); t.join();
        exec2.removeNode(&node);
        check(count.load() == 100, "Reuse node with new executor",
              (std::to_string(count.load()) + "/100").c_str());
    }

    node.stop();
}

// ═══════════════════════════════════════════════════════════════
// Test 3: spinSome vs spin Consistency
//   Both modes should deliver all messages eventually.
// ═══════════════════════════════════════════════════════════════
static void testSpinSomeVsSpin()
{
    std::cout << "\n=== Test 3: spinSome vs spin Consistency ===\n";
    constexpr int N = 50000;

    // 3a: spin() delivers all
    {
        comm::Domain domain(102);
        comm::Node node("spin_full", domain, intraOpts());
        std::atomic<int> count{0};
        auto pub = node.createPublisher<int>("/spin_test");
        auto sub = node.createSubscriber<int>("/spin_test",
            [&](const int&) { count.fetch_add(1, std::memory_order_relaxed); });

        comm::SingleThreadedExecutor exec;
        exec.addNode(&node);
        std::thread t([&] { exec.spin(); });

        for (int i = 0; i < N; ++i) pub->publish(i);

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (count.load() < N && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();

        exec.stop(); t.join();
        check(count.load() == N, "spin() delivers all msgs",
              (std::to_string(count.load()) + "/" + std::to_string(N)).c_str());
        node.stop();
    }

    // 3b: spinSome() delivers all (caller loops)
    {
        comm::Domain domain(103);
        comm::Node node("spin_some", domain, intraOpts());
        std::atomic<int> count{0};
        auto pub = node.createPublisher<int>("/spin_test");
        auto sub = node.createSubscriber<int>("/spin_test",
            [&](const int&) { count.fetch_add(1, std::memory_order_relaxed); });

        comm::SingleThreadedExecutor exec;
        exec.addNode(&node);

        std::thread pub_th([&] {
            for (int i = 0; i < N; ++i) pub->publish(i);
        });

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (count.load() < N && std::chrono::steady_clock::now() < deadline)
            exec.spinSome();

        pub_th.join();
        check(count.load() == N, "spinSome() delivers all msgs",
              (std::to_string(count.load()) + "/" + std::to_string(N)).c_str());
        node.stop();
    }

    // 3c: spinSome() on all 4 executor types
    auto testSpinSomeAll = [&](const char* label, auto& exec)
    {
        comm::Domain domain(104);
        comm::Node node("ss_all", domain, intraOpts());
        std::atomic<int> count{0};
        auto pub = node.createPublisher<int>("/spinsome_all");
        auto sub = node.createSubscriber<int>("/spinsome_all",
            [&](const int&) { count.fetch_add(1, std::memory_order_relaxed); });

        exec.addNode(&node);

        constexpr int M = 5000;
        std::thread pub_th([&] {
            for (int i = 0; i < M; ++i) pub->publish(i);
        });

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (count.load() < M && std::chrono::steady_clock::now() < deadline)
            exec.spinSome();

        pub_th.join();
        exec.removeNode(&node);
        std::string detail = std::to_string(count.load()) + "/" + std::to_string(M);
        check(count.load() == M, label, detail.c_str());
        node.stop();
    };

    {
        comm::MultiThreadedExecutor exec(2);
        testSpinSomeAll("spinSome() MultiThreaded", exec);
    }
    {
        comm::SeqOrderedExecutor exec;
        testSpinSomeAll("spinSome() SeqOrdered", exec);
    }
    {
        comm::TimeOrderedExecutor exec(std::chrono::nanoseconds{0});
        testSpinSomeAll("spinSome() TimeOrdered", exec);
    }
}

// ═══════════════════════════════════════════════════════════════
// Test 4: Callback Group Mixing
//   Reentrant + MutuallyExclusive groups in the same executor.
//   Reentrant callbacks can run concurrently;
//   MutuallyExclusive callbacks must not overlap.
// ═══════════════════════════════════════════════════════════════
static void testCallbackGroupMixing()
{
    std::cout << "\n=== Test 4: Callback Group Mixing ===\n";
    constexpr int N = 5000;

    comm::Domain domain(105);
    comm::Node node("cbg_mix", domain, intraOpts());

    // Reentrant group: allow concurrent access
    comm::CallbackGroupBase group_r(&node, comm::CallbackGroupType::Reentrant);
    // MutuallyExclusive group: must serialize
    comm::CallbackGroupBase group_me(&node, comm::CallbackGroupType::MutuallyExclusive);

    std::atomic<int> reentrant_count{0};
    std::atomic<int> max_concurrent{0};
    std::atomic<int> current_concurrent{0};

    auto sub_r1 = node.createSubscriber<int>("/cbg_mix",
        [&](const int&) {
            int c = current_concurrent.fetch_add(1, std::memory_order_relaxed) + 1;
            int prev = max_concurrent.load(std::memory_order_relaxed);
            while (c > prev && !max_concurrent.compare_exchange_weak(prev, c))
                ;
            reentrant_count.fetch_add(1, std::memory_order_relaxed);
            current_concurrent.fetch_sub(1, std::memory_order_relaxed);
        }, &group_r);

    auto sub_r2 = node.createSubscriber<int>("/cbg_mix",
        [&](const int&) {
            int c = current_concurrent.fetch_add(1, std::memory_order_relaxed) + 1;
            int prev = max_concurrent.load(std::memory_order_relaxed);
            while (c > prev && !max_concurrent.compare_exchange_weak(prev, c))
                ;
            reentrant_count.fetch_add(1, std::memory_order_relaxed);
            current_concurrent.fetch_sub(1, std::memory_order_relaxed);
        }, &group_r);

    std::atomic<int> me_count{0};
    std::atomic<bool> me_overlap{false};
    std::atomic<int> me_in_use{0};

    auto sub_me = node.createSubscriber<int>("/cbg_mix",
        [&](const int&) {
            int prev = me_in_use.fetch_add(1, std::memory_order_seq_cst);
            if (prev != 0)
                me_overlap.store(true, std::memory_order_relaxed);
            me_count.fetch_add(1, std::memory_order_relaxed);
            me_in_use.fetch_sub(1, std::memory_order_seq_cst);
        }, &group_me);

    auto pub = node.createPublisher<int>("/cbg_mix");

    comm::MultiThreadedExecutor exec(4);
    exec.addNode(&node);
    std::thread t([&] { exec.spin(); });

    for (int i = 0; i < N; ++i) pub->publish(i);

    int expected_r = N * 2; // two reentrant subscribers
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while ((reentrant_count.load() < expected_r || me_count.load() < N) &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();

    exec.stop(); t.join();

    check(reentrant_count.load() == expected_r, "Reentrant subs got all msgs",
          (std::to_string(reentrant_count.load()) + "/" + std::to_string(expected_r)).c_str());
    check(me_count.load() == N, "MutExcl sub got all msgs",
          (std::to_string(me_count.load()) + "/" + std::to_string(N)).c_str());
    check(!me_overlap.load(), "MutExcl callbacks never overlapped");

    node.stop();
}

// ═══════════════════════════════════════════════════════════════
// Test 5: Chain/Pipeline Topology (A → B → C)
//   NodeA publishes to topic1; NodeB subscribes topic1, transforms,
//   publishes to topic2; NodeC subscribes topic2.
// ═══════════════════════════════════════════════════════════════
static void testChainTopology()
{
    std::cout << "\n=== Test 5: Chain/Pipeline Topology (A→B→C) ===\n";
    constexpr int N = 1000;

    comm::Domain domain(106);
    comm::Node nodeA("A", domain, intraOpts());
    comm::Node nodeB("B", domain, intraOpts());
    comm::Node nodeC("C", domain, intraOpts());

    auto pubA = nodeA.createPublisher<int>("/topic_ab");
    auto pubB = nodeB.createPublisher<int>("/topic_bc");

    std::atomic<int> final_count{0};
    std::vector<int> final_values;
    std::mutex final_mutex;

    auto subB = nodeB.createSubscriber<int>("/topic_ab",
        [&](const int& v) {
            pubB->publish(v * 2); // transform: double the value
        });

    auto subC = nodeC.createSubscriber<int>("/topic_bc",
        [&](const int& v) {
            {
                std::lock_guard lock(final_mutex);
                final_values.push_back(v);
            }
            final_count.fetch_add(1, std::memory_order_relaxed);
        });

    // All nodes share one executor
    comm::SingleThreadedExecutor exec;
    exec.addNode(&nodeB);
    exec.addNode(&nodeC);
    std::thread t([&] { exec.spin(); });

    for (int i = 0; i < N; ++i)
        pubA->publish(i);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (final_count.load() < N && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();

    exec.stop(); t.join();

    check(final_count.load() == N, "Pipeline delivered all msgs",
          (std::to_string(final_count.load()) + "/" + std::to_string(N)).c_str());

    // Verify transformation: all values should be even (original × 2)
    bool all_even = true;
    {
        std::lock_guard lock(final_mutex);
        for (int v : final_values)
            if (v % 2 != 0) { all_even = false; break; }
    }
    check(all_even, "Pipeline transformation correct (all values doubled)");

    nodeA.stop(); nodeB.stop(); nodeC.stop();
}

// ═══════════════════════════════════════════════════════════════
// Test 6: Empty Queue spinSome
//   Calling spinSome() with no pending messages should return
//   quickly (sub-millisecond).
// ═══════════════════════════════════════════════════════════════
static void testEmptySpinSome()
{
    std::cout << "\n=== Test 6: Empty Queue spinSome ===\n";

    comm::Domain domain(107);
    comm::Node node("empty", domain, intraOpts());
    auto sub = node.createSubscriber<int>("/empty_topic",
        [](const int&) { /* never called */ });

    auto testExec = [&](const char* label, auto& exec)
    {
        exec.addNode(&node);
        auto t1 = std::chrono::steady_clock::now();
        for (int i = 0; i < 1000; ++i) exec.spinSome();
        auto t2 = std::chrono::steady_clock::now();
        exec.removeNode(&node);

        double us = std::chrono::duration<double, std::micro>(t2 - t1).count();
        std::string detail = std::to_string(us / 1000.0) + " us/call";
        check(us < 100000, label, detail.c_str()); // 1000 calls < 100ms total
    };

    {
        comm::SingleThreadedExecutor exec;
        testExec("Empty spinSome() SingleThreaded", exec);
    }
    {
        comm::MultiThreadedExecutor exec(2);
        testExec("Empty spinSome() MultiThreaded", exec);
    }
    {
        comm::SeqOrderedExecutor exec;
        testExec("Empty spinSome() SeqOrdered", exec);
    }
    {
        comm::TimeOrderedExecutor exec(std::chrono::nanoseconds{0});
        testExec("Empty spinSome() TimeOrdered", exec);
    }

    node.stop();
}

// ═══════════════════════════════════════════════════════════════
// Test 7: Multi-Node Single Executor
//   Multiple nodes share the same executor. Messages on
//   different topics should all be delivered.
// ═══════════════════════════════════════════════════════════════
static void testMultiNodeSingleExecutor()
{
    std::cout << "\n=== Test 7: Multi-Node Single Executor ===\n";
    constexpr int N = 5000;

    comm::Domain domain(108);
    comm::Node nodeA("A", domain, intraOpts());
    comm::Node nodeB("B", domain, intraOpts());
    comm::Node nodeC("C", domain, intraOpts());

    std::atomic<int> countA{0}, countB{0}, countC{0};

    auto pubA = nodeA.createPublisher<int>("/topicA");
    auto subA = nodeA.createSubscriber<int>("/topicA",
        [&](const int&) { countA.fetch_add(1, std::memory_order_relaxed); });

    auto pubB = nodeB.createPublisher<int>("/topicB");
    auto subB = nodeB.createSubscriber<int>("/topicB",
        [&](const int&) { countB.fetch_add(1, std::memory_order_relaxed); });

    auto pubC = nodeC.createPublisher<int>("/topicC");
    auto subC = nodeC.createSubscriber<int>("/topicC",
        [&](const int&) { countC.fetch_add(1, std::memory_order_relaxed); });

    comm::SingleThreadedExecutor exec;
    exec.addNode(&nodeA);
    exec.addNode(&nodeB);
    exec.addNode(&nodeC);
    std::thread t([&] { exec.spin(); });

    for (int i = 0; i < N; ++i)
    {
        pubA->publish(i);
        pubB->publish(i);
        pubC->publish(i);
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while ((countA.load() < N || countB.load() < N || countC.load() < N) &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();

    exec.stop(); t.join();

    check(countA.load() == N, "NodeA received all",
          (std::to_string(countA.load()) + "/" + std::to_string(N)).c_str());
    check(countB.load() == N, "NodeB received all",
          (std::to_string(countB.load()) + "/" + std::to_string(N)).c_str());
    check(countC.load() == N, "NodeC received all",
          (std::to_string(countC.load()) + "/" + std::to_string(N)).c_str());

    nodeA.stop(); nodeB.stop(); nodeC.stop();
}

// ═══════════════════════════════════════════════════════════════
// Test 8: All 4 Executor Types — Correctness
//   Verify each executor type delivers exactly N messages.
// ═══════════════════════════════════════════════════════════════
static void testAllExecutorCorrectness()
{
    std::cout << "\n=== Test 8: All 4 Executor Types Correctness ===\n";
    constexpr int N = 20000;

    auto runTest = [](const char* label, auto& exec) {
        comm::Domain domain(110);
        comm::Node node("correct", domain, intraOpts());
        std::atomic<int> count{0};
        auto pub = node.createPublisher<int>("/correct");
        auto sub = node.createSubscriber<int>("/correct",
            [&](const int&) { count.fetch_add(1, std::memory_order_relaxed); });

        exec.addNode(&node);
        std::thread t([&] { exec.spin(); });

        for (int i = 0; i < N; ++i) pub->publish(i);

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (count.load() < N && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();

        exec.stop(); t.join();
        exec.removeNode(&node);

        std::string detail = std::to_string(count.load()) + "/" + std::to_string(N);
        check(count.load() == N, label, detail.c_str());
        node.stop();
    };

    {
        comm::SingleThreadedExecutor exec;
        runTest("SingleThreaded delivers all", exec);
    }
    {
        comm::MultiThreadedExecutor exec(4);
        runTest("MultiThreaded(4T) delivers all", exec);
    }
    {
        comm::SeqOrderedExecutor exec;
        runTest("SeqOrdered delivers all", exec);
    }
    {
        comm::TimeOrderedExecutor exec(std::chrono::nanoseconds{0});
        runTest("TimeOrdered(offset=0) delivers all", exec);
    }
}

// ═══════════════════════════════════════════════════════════════
// Test 9: Cross-Executor with SeqOrdered + TimeOrdered
//   Verify the original deadlock scenario (per-executor seq)
//   works correctly with different executor types.
// ═══════════════════════════════════════════════════════════════
static void testCrossExecutorSeqTime()
{
    std::cout << "\n=== Test 9: Cross-Executor SeqOrdered + TimeOrdered ===\n";
    constexpr int N = 5000;

    comm::Domain domain(111);
    comm::Node pubNode("pub", domain, intraOpts());
    comm::Node seqNode("seq", domain, intraOpts());
    comm::Node timeNode("time", domain, intraOpts());

    std::atomic<int> seq_count{0}, time_count{0};

    auto pub = pubNode.createPublisher<int>("/cross_st");
    auto sub_seq = seqNode.createSubscriber<int>("/cross_st",
        [&](const int&) { seq_count.fetch_add(1, std::memory_order_relaxed); });
    auto sub_time = timeNode.createSubscriber<int>("/cross_st",
        [&](const int&) { time_count.fetch_add(1, std::memory_order_relaxed); });

    comm::SeqOrderedExecutor exec_seq;
    exec_seq.addNode(&seqNode);

    comm::TimeOrderedExecutor exec_time(std::chrono::nanoseconds{0});
    exec_time.addNode(&timeNode);

    std::thread t1([&] { exec_seq.spin(); });
    std::thread t2([&] { exec_time.spin(); });

    for (int i = 0; i < N; ++i) pub->publish(i);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while ((seq_count.load() < N || time_count.load() < N) &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();

    exec_seq.stop(); exec_time.stop();
    t1.join(); t2.join();

    check(seq_count.load() == N, "SeqOrdered subscriber",
          (std::to_string(seq_count.load()) + "/" + std::to_string(N)).c_str());
    check(time_count.load() == N, "TimeOrdered subscriber",
          (std::to_string(time_count.load()) + "/" + std::to_string(N)).c_str());

    pubNode.stop(); seqNode.stop(); timeNode.stop();
}

// ═══════════════════════════════════════════════════════════════
// Test 10: Stop from Callback
//   Executor.stop() called from within a subscriber callback.
// ═══════════════════════════════════════════════════════════════
static void testStopFromCallback()
{
    std::cout << "\n=== Test 10: Stop from Callback ===\n";

    comm::Domain domain(112);
    comm::Node node("stopcb", domain, intraOpts());

    std::atomic<int> count{0};
    comm::SingleThreadedExecutor exec;

    auto pub = node.createPublisher<int>("/stop_cb");
    auto sub = node.createSubscriber<int>("/stop_cb",
        [&](const int& v) {
            count.fetch_add(1, std::memory_order_relaxed);
            if (v == 42)
                exec.stop(); // stop from within callback
        });

    exec.addNode(&node);
    std::thread t([&] { exec.spin(); });

    pub->publish(1);
    pub->publish(42); // triggers stop
    pub->publish(99); // may or may not be delivered

    t.join(); // should complete after stop

    check(count.load() >= 2, "Stop from callback — no deadlock",
          (std::to_string(count.load()) + " msgs processed").c_str());

    node.stop();
}

// ═══════════════════════════════════════════════════════════════
int main()
{
    std::cout << "═══════════════════════════════════════════════════════════\n"
              << "  Executor Feature Tests\n"
              << "═══════════════════════════════════════════════════════════\n";

    testCrossExecutor();
    testExecutorLifecycle();
    testSpinSomeVsSpin();
    testCallbackGroupMixing();
    testChainTopology();
    testEmptySpinSome();
    testMultiNodeSingleExecutor();
    testAllExecutorCorrectness();
    testCrossExecutorSeqTime();
    testStopFromCallback();

    std::cout << "\n═══════════════════════════════════════════════════════════\n"
              << "  Results: " << g_pass << " passed, " << g_fail << " failed\n"
              << "═══════════════════════════════════════════════════════════\n";

    return g_fail > 0 ? 1 : 0;
}
