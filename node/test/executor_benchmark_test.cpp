/**
 * @file executor_benchmark_test.cpp
 * @brief Comprehensive throughput benchmark for all 4 executor types.
 *
 * Tests:
 *  1. SingleThreadedExecutor  — spin()    — 1 pub, 1 sub
 *  2. SingleThreadedExecutor  — spinSome  — 1 pub, 1 sub
 *  3. MultiThreadedExecutor   — spin()    — 1 pub, 1 sub
 *  4. MultiThreadedExecutor   — spin()    — 1 pub, 1 sub (Reentrant)
 *  5. SeqOrderedExecutor      — spinSome  — 2 pubs, 2 subs
 *  6. TimeOrderedExecutor     — spin()    — 1 pub, 1 sub
 *  7. SingleThreadedExecutor  — spin()    — 1 pub, N subs (fan-out scaling)
 *  8. SingleThreadedExecutor  — spin()    — N pubs, 1 sub (multi-publisher)
 *
 * Message type: trivially-copyable 8-byte double (SmallValueMsg fast path).
 */

#include <iostream>
#include <iomanip>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <string>

#include <lux/communication/Node.hpp>
#include <lux/communication/CallbackGroupBase.hpp>
#include <lux/communication/executor/SingleThreadedExecutor.hpp>
#include <lux/communication/executor/MultiThreadedExecutor.hpp>
#include <lux/communication/executor/SeqOrderedExecutor.hpp>
#include <lux/communication/executor/TimeOrderedExecutor.hpp>

namespace comm = lux::communication;

static comm::NodeOptions intraOpts()
{
    return { .enable_discovery = false, .enable_shm = false, .enable_net = false };
}

struct BenchResult {
    std::string name;
    int message_count;
    double duration_ms;
    double throughput;     // msg/s
};

static void printResult(const BenchResult& r)
{
    std::cout << std::left << std::setw(52) << r.name
              << std::right << std::fixed << std::setprecision(2)
              << std::setw(12) << r.throughput / 1e6 << " M msg/s  "
              << std::setw(8) << r.duration_ms << " ms"
              << std::endl;
}

// ────────────────────────────────────────────────────────────
// Benchmark 1: SingleThreadedExecutor — spin()
// ────────────────────────────────────────────────────────────
static BenchResult benchSingleThreadedSpin(int N)
{
    comm::Domain domain(1);
    comm::Node node("st_spin", domain, intraOpts());

    std::atomic<int> count{0};
    auto sub = node.createSubscriber<double>("/bench",
        [&](const double&) { count.fetch_add(1, std::memory_order_relaxed); });
    auto pub = node.createPublisher<double>("/bench");

    comm::SingleThreadedExecutor exec;
    exec.addNode(&node);
    std::thread spin_th([&] { exec.spin(); });

    auto t1 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i) pub->emplace(1.0);
    while (count.load(std::memory_order_relaxed) < N)
        std::this_thread::yield();
    auto t2 = std::chrono::steady_clock::now();

    exec.stop(); spin_th.join();

    double ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
    return {"SingleThreaded spin() [1pub 1sub]", N, ms, N / (ms / 1000.0)};
}

// ────────────────────────────────────────────────────────────
// Benchmark 2: SingleThreadedExecutor — spinSome()
// ────────────────────────────────────────────────────────────
static BenchResult benchSingleThreadedSpinSome(int N)
{
    comm::Domain domain(1);
    comm::Node node("st_some", domain, intraOpts());

    std::atomic<int> count{0};
    auto sub = node.createSubscriber<double>("/bench",
        [&](const double&) { count.fetch_add(1, std::memory_order_relaxed); });
    auto pub = node.createPublisher<double>("/bench");

    comm::SingleThreadedExecutor exec;
    exec.addNode(&node);

    // Publisher thread
    std::thread pub_th([&] {
        for (int i = 0; i < N; ++i) pub->emplace(1.0);
    });

    auto t1 = std::chrono::steady_clock::now();
    while (count.load(std::memory_order_relaxed) < N)
        exec.spinSome();
    auto t2 = std::chrono::steady_clock::now();

    pub_th.join(); exec.stop();

    double ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
    return {"SingleThreaded spinSome() [1pub 1sub]", N, ms, N / (ms / 1000.0)};
}

// ────────────────────────────────────────────────────────────
// Benchmark 3: MultiThreadedExecutor — spin() — MutuallyExclusive
// ────────────────────────────────────────────────────────────
static BenchResult benchMultiThreadedSpin(int N, size_t threads)
{
    comm::Domain domain(1);
    comm::Node node("mt_spin", domain, intraOpts());

    std::atomic<int> count{0};
    auto sub = node.createSubscriber<double>("/bench",
        [&](const double&) { count.fetch_add(1, std::memory_order_relaxed); });
    auto pub = node.createPublisher<double>("/bench");

    comm::MultiThreadedExecutor exec(threads);
    exec.addNode(&node);
    std::thread spin_th([&] { exec.spin(); });

    auto t1 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i) pub->emplace(1.0);
    while (count.load(std::memory_order_relaxed) < N)
        std::this_thread::yield();
    auto t2 = std::chrono::steady_clock::now();

    exec.stop(); spin_th.join();

    double ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
    std::string name = "MultiThreaded spin() [" + std::to_string(threads) + "T, 1pub 1sub, MutExcl]";
    return {name, N, ms, N / (ms / 1000.0)};
}

// ────────────────────────────────────────────────────────────
// Benchmark 4: MultiThreadedExecutor — spin() — Reentrant group, N subs
// ────────────────────────────────────────────────────────────
static BenchResult benchMultiThreadedReentrant(int N, size_t threads, int num_subs)
{
    comm::Domain domain(1);
    comm::Node node("mt_re", domain, intraOpts());

    // Create a Reentrant callback group
    comm::CallbackGroupBase group_r(&node, comm::CallbackGroupType::Reentrant);

    std::atomic<int> count{0};
    std::vector<std::shared_ptr<comm::Subscriber<double>>> subs;
    for (int s = 0; s < num_subs; ++s)
    {
        auto sub = node.createSubscriber<double>("/bench",
            [&](const double&) { count.fetch_add(1, std::memory_order_relaxed); },
            &group_r);
        subs.push_back(std::move(sub));
    }
    auto pub = node.createPublisher<double>("/bench");

    const int total = N * num_subs;

    comm::MultiThreadedExecutor exec(threads);
    exec.addNode(&node);
    std::thread spin_th([&] { exec.spin(); });

    auto t1 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i) pub->emplace(1.0);
    while (count.load(std::memory_order_relaxed) < total)
        std::this_thread::yield();
    auto t2 = std::chrono::steady_clock::now();

    exec.stop(); spin_th.join();

    double ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
    std::string name = "MultiThreaded spin() [" + std::to_string(threads) + "T, 1pub "
                       + std::to_string(num_subs) + "sub, Reentrant]";
    return {name, total, ms, total / (ms / 1000.0)};
}

// ────────────────────────────────────────────────────────────
// Benchmark 5: SeqOrderedExecutor — spinSome()
// ────────────────────────────────────────────────────────────
static BenchResult benchSeqOrdered(int N)
{
    comm::Domain domain(1);
    comm::Node node("seq", domain, intraOpts());

    std::atomic<int> count{0};
    auto sub_a = node.createSubscriber<double>("/bench_a",
        [&](const double&) { count.fetch_add(1, std::memory_order_relaxed); });
    auto sub_b = node.createSubscriber<double>("/bench_b",
        [&](const double&) { count.fetch_add(1, std::memory_order_relaxed); });
    auto pub_a = node.createPublisher<double>("/bench_a");
    auto pub_b = node.createPublisher<double>("/bench_b");

    comm::SeqOrderedExecutor exec;
    exec.addNode(&node);

    const int total = N * 2;
    std::thread pub_th([&] {
        for (int i = 0; i < N; ++i) {
            pub_a->emplace(1.0);
            pub_b->emplace(1.0);
        }
    });

    auto t1 = std::chrono::steady_clock::now();
    while (count.load(std::memory_order_relaxed) < total)
        exec.spinSome();
    auto t2 = std::chrono::steady_clock::now();

    pub_th.join(); exec.stop();

    double ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
    return {"SeqOrdered spinSome() [2pub 2sub]", total, ms, total / (ms / 1000.0)};
}

// ────────────────────────────────────────────────────────────
// Benchmark 6: TimeOrderedExecutor — spin()
// ────────────────────────────────────────────────────────────
static BenchResult benchTimeOrdered(int N)
{
    comm::Domain domain(1);
    comm::Node node("time", domain, intraOpts());

    std::atomic<int> count{0};
    auto sub = node.createSubscriber<double>("/bench",
        [&](const double&) { count.fetch_add(1, std::memory_order_relaxed); });
    auto pub = node.createPublisher<double>("/bench");

    // time_offset=0 → execute immediately (no reorder delay)
    comm::TimeOrderedExecutor exec(std::chrono::nanoseconds{0});
    exec.addNode(&node);
    std::thread spin_th([&] { exec.spin(); });

    auto t1 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i) pub->emplace(1.0);
    while (count.load(std::memory_order_relaxed) < N)
        std::this_thread::yield();
    auto t2 = std::chrono::steady_clock::now();

    exec.stop(); spin_th.join();

    double ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
    return {"TimeOrdered spin() [1pub 1sub, offset=0]", N, ms, N / (ms / 1000.0)};
}

// ────────────────────────────────────────────────────────────
// Benchmark 7: Fan-out scaling (1 pub → N subs)
// ────────────────────────────────────────────────────────────
static BenchResult benchFanOut(int N, int num_subs)
{
    comm::Domain domain(1);
    comm::Node node("fanout", domain, intraOpts());

    std::atomic<int> count{0};
    std::vector<std::shared_ptr<comm::Subscriber<double>>> subs;
    for (int s = 0; s < num_subs; ++s)
    {
        auto sub = node.createSubscriber<double>("/bench",
            [&](const double&) { count.fetch_add(1, std::memory_order_relaxed); });
        subs.push_back(std::move(sub));
    }
    auto pub = node.createPublisher<double>("/bench");

    const int total = N * num_subs;

    comm::SingleThreadedExecutor exec;
    exec.addNode(&node);
    std::thread spin_th([&] { exec.spin(); });

    auto t1 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i) pub->emplace(1.0);
    while (count.load(std::memory_order_relaxed) < total)
        std::this_thread::yield();
    auto t2 = std::chrono::steady_clock::now();

    exec.stop(); spin_th.join();

    double ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
    std::string name = "FanOut SingleThreaded [1pub " + std::to_string(num_subs) + "sub]";
    return {name, total, ms, total / (ms / 1000.0)};
}

// ────────────────────────────────────────────────────────────
// Benchmark 8: Multi-publisher contention (N pubs → 1 sub)
// ────────────────────────────────────────────────────────────
static BenchResult benchMultiPublisher(int msgs_per_pub, int num_pubs)
{
    comm::Domain domain(1);
    comm::Node node("mpub", domain, intraOpts());

    std::atomic<int> count{0};
    auto sub = node.createSubscriber<double>("/bench",
        [&](const double&) { count.fetch_add(1, std::memory_order_relaxed); });

    std::vector<std::shared_ptr<comm::Publisher<double>>> pubs;
    for (int p = 0; p < num_pubs; ++p)
        pubs.push_back(node.createPublisher<double>("/bench"));

    const int total = msgs_per_pub * num_pubs;

    comm::SingleThreadedExecutor exec;
    exec.addNode(&node);
    std::thread spin_th([&] { exec.spin(); });

    auto t1 = std::chrono::steady_clock::now();

    // Spawn publisher threads
    std::vector<std::thread> pub_threads;
    for (int p = 0; p < num_pubs; ++p)
    {
        pub_threads.emplace_back([&, p] {
            for (int i = 0; i < msgs_per_pub; ++i)
                pubs[p]->emplace(1.0);
        });
    }
    for (auto& t : pub_threads) t.join();

    while (count.load(std::memory_order_relaxed) < total)
        std::this_thread::yield();
    auto t2 = std::chrono::steady_clock::now();

    exec.stop(); spin_th.join();

    double ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
    std::string name = "MultiPublisher [" + std::to_string(num_pubs) + "pub 1sub]";
    return {name, total, ms, total / (ms / 1000.0)};
}

// ────────────────────────────────────────────────────────────
int main()
{
    const int N = 5'000'000;  // 5M messages per benchmark

    std::cout << "═══════════════════════════════════════════════════════════════════════════\n";
    std::cout << "  Executor Benchmark  (" << N/1e6 << "M msgs per test, intra-process only)\n";
    std::cout << "═══════════════════════════════════════════════════════════════════════════\n";
    std::cout << std::left << std::setw(52) << "Test"
              << std::right << std::setw(12) << "Throughput"
              << "    " << std::setw(8) << "Time" << "\n";
    std::cout << std::string(78, '─') << "\n";

    std::vector<BenchResult> results;

    // 1. SingleThreaded spin()
    results.push_back(benchSingleThreadedSpin(N));
    printResult(results.back());

    // 2. SingleThreaded spinSome()
    results.push_back(benchSingleThreadedSpinSome(N));
    printResult(results.back());

    // 3. MultiThreaded spin() — MutExcl, 4 threads
    results.push_back(benchMultiThreadedSpin(N, 4));
    printResult(results.back());

    // 4. MultiThreaded spin() — Reentrant, 4 threads, 4 subs
    results.push_back(benchMultiThreadedReentrant(N, 4, 4));
    printResult(results.back());

    // 5. SeqOrdered spinSome()
    results.push_back(benchSeqOrdered(N));
    printResult(results.back());

    // 6. TimeOrdered spin()
    results.push_back(benchTimeOrdered(N));
    printResult(results.back());

    std::cout << std::string(78, '─') << "\n";
    std::cout << "  Fan-out scaling (1 publisher → N subscribers)\n";
    std::cout << std::string(78, '─') << "\n";

    // 7. Fan-out: 1, 2, 4, 8, 16 subscribers
    for (int subs : {1, 2, 4, 8, 16})
    {
        int per = std::max(N / subs, 500000);  // scale down per-pub count for large fan-out
        results.push_back(benchFanOut(per, subs));
        printResult(results.back());
    }

    std::cout << std::string(78, '─') << "\n";
    std::cout << "  Multi-publisher contention (N publishers → 1 subscriber)\n";
    std::cout << std::string(78, '─') << "\n";

    // 8. Multi-publisher: 1, 2, 4, 8 publishers
    for (int pubs : {1, 2, 4, 8})
    {
        int per = N / pubs;
        results.push_back(benchMultiPublisher(per, pubs));
        printResult(results.back());
    }

    std::cout << "═══════════════════════════════════════════════════════════════════════════\n";
    std::cout << "Done.\n";
    return 0;
}
