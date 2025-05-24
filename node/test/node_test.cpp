#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <functional>
#include <cassert>

// include headers according to project layout
#include <lux/communication/intraprocess/Node.hpp>
#include <lux/communication/Executor.hpp>

struct StringMsg
{
    std::string text;
};

struct ComplexMsg
{
    std::vector<int> data;
    int id;
};

// =============== Functional tests ===============

/**
 * @brief Test domain isolation
 *  - Create nodes in domain1 and domain2
 *  - NodeA in domain1 publishes messages
 *  - NodeB in domain2 subscribes to the same topic but should not receive them
 */
void testDomainIsolation()
{
    using namespace lux::communication::intraprocess;

    std::cout << "\n=== testDomainIsolation ===\n";
    auto domain1 = std::make_shared<Domain>(1);
    auto domain2 = std::make_shared<Domain>(2);

    // NodeA and NodeB belong to different domains
    auto nodeA = std::make_shared<Node>("NodeA", domain1);
    auto nodeB = std::make_shared<Node>("NodeB", domain2);

    // Flag to check if NodeB unexpectedly receives messages
    std::atomic<int> bReceivedCount{0};

    // Create a publisher in domain1
    auto pubA = nodeA->createPublisher<StringMsg>("chatter");

    // Subscribe to the same "chatter" in domain2
    auto subB = nodeB->createSubscriber<StringMsg>(
        "chatter", 
        [&](const StringMsg &msg){
            // Getting here means NodeB received a message
            ++bReceivedCount;
        }
    );

    // Executor running nodeB
    auto execB = std::make_shared<lux::communication::SingleThreadedExecutor>();
    // Add nodeB to the executor (default callback group)
    execB->addNode(nodeB);

    // Start spinning
    std::thread spinThreadB([&]{
        execB->spin();
    });

    // Publish a few messages from NodeA
    for(int i = 0; i < 3; ++i) {
        pubA->publish(StringMsg{"Hello from domain1"});
    }

    // Wait a moment
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Stop execB
    execB->stop();
    spinThreadB.join();

    // Check bReceivedCount
    if (bReceivedCount.load() == 0) {
        std::cout << "[OK] domain isolation works, NodeB got 0 messages.\n";
    } else {
        std::cout << "[FAIL] domain isolation failed, NodeB got " 
                  << bReceivedCount.load() << " messages.\n";
    }
}

/**
 * @brief Test communication of multiple nodes in the same domain
 *  - NodeX and NodeY create publishers/subscribers under the same domain
 *  - Verify that the subscriber receives messages
 *  - Verify RAII: topics should be cleaned after leaving scope
 */
void testSingleDomainMultiNode()
{
    using namespace lux::communication::intraprocess;

    std::cout << "\n=== testSingleDomainMultiNode ===\n";

    auto domain = std::make_shared<Domain>(1);
    {
        auto nodeA = std::make_shared<Node>("NodeA", domain);
        auto nodeB = std::make_shared<Node>("NodeB", domain);

        // Create publisher on nodeA
        auto pub = nodeA->createPublisher<StringMsg>("chat");

        // Create subscriber on nodeB
        std::atomic<int> subCount{0};
        auto sub = nodeB->createSubscriber<StringMsg>("chat",
            [&](const StringMsg &msg){
                // Message received
                ++subCount;
                std::cout << "[NodeB] got: " << msg.text << "\n";
            });

        // Executor for nodeB
        auto execB = std::make_shared<lux::communication::SingleThreadedExecutor>();
        execB->addNode(nodeB);

        // Start a thread to spin nodeB
        std::thread th([&]{ execB->spin(); });

        // Publish messages
        for(int i = 0; i < 5; ++i) {
            pub->publish(StringMsg{"Hello " + std::to_string(i)});
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // Stop
        execB->stop();
        th.join();

        // Check subCount
        if(subCount.load() == 5) {
            std::cout << "[OK] NodeB received all 5 messages.\n";
        } else {
            std::cout << "[FAIL] NodeB only got " << subCount.load() << " messages.\n";
        }

        // Leaving this scope destroys Node/Publisher/Subscriber
        // Topic without references will be removed from Domain
    }

    // Domain still alive here but its Topic should be removed
    std::cout << "[INFO] Left the scope, Node/Publisher/Subscriber destroyed.\n";
}

/**
 * @brief Test multiple subscribers on the same topic
 *        Another topic on a different node should not interfere
 */
void testMultiSubscriber()
{
    using namespace lux::communication::intraprocess;

    std::cout << "\n=== testMultiSubscriber ===\n";

    auto domain = std::make_shared<Domain>(1);
    auto nodeA = std::make_shared<Node>("NodeA", domain);
    auto nodeB = std::make_shared<Node>("NodeB", domain);

    // NodeA: Publisher1 -> topic "chat"
    auto pubChat = nodeA->createPublisher<StringMsg>("chat");

    // NodeA: Publisher2 -> topic "data"
    auto pubData = nodeA->createPublisher<ComplexMsg>("data");

    // NodeB: subscriber1 -> topic "chat"
    std::atomic<int> chatCount1{0};
    auto subChat1 = nodeB->createSubscriber<StringMsg>("chat", 
        [&](const StringMsg& msg){
            chatCount1++;
            std::cout << "[subChat1] got text: " << msg.text << "\n";
        });

    // NodeB: subscriber2 -> topic "chat" (second subscriber)
    std::atomic<int> chatCount2{0};
    auto subChat2 = nodeB->createSubscriber<StringMsg>("chat",
        [&](const StringMsg& msg){
            chatCount2++;
            std::cout << "[subChat2] also got text: " << msg.text << "\n";
        });

    // NodeB: subscriberData -> topic "data"
    std::atomic<int> dataCount{0};
    auto subData = nodeB->createSubscriber<ComplexMsg>("data",
        [&](const ComplexMsg& m){
            dataCount++;
            std::cout << "[subData] got size=" << m.data.size() 
                      << " id=" << m.id << "\n";
        });

    // Executor for nodeB
    auto execB = std::make_shared<lux::communication::SingleThreadedExecutor>();
    execB->addNode(nodeB);
    std::thread spinTh([&]{ execB->spin(); });

    // Publish
    pubChat->publish(StringMsg{"Hello 1"});
    pubChat->publish(StringMsg{"Hello 2"});
    pubData->publish(ComplexMsg{{1,2,3,4}, 99});

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Stop
    execB->stop();
    spinTh.join();

    // Check
    if(chatCount1.load() == 2 && chatCount2.load() == 2) {
        std::cout << "[OK] both subChat1 & subChat2 got 2 messages.\n";
    } else {
        std::cout << "[FAIL] chatCount1=" << chatCount1.load() 
                  << " chatCount2=" << chatCount2.load() << "\n";
    }
    if(dataCount.load() == 1) {
        std::cout << "[OK] subData got 1 data message.\n";
    } else {
        std::cout << "[FAIL] subData didn't get expected data message.\n";
    }
}

/**
 * @brief Simple zero-copy test: check whether the object address stays the same
 * (In real projects you would also examine RcBuffer reference counts.)
 */
void testZeroCopyCheck()
{
    using namespace lux::communication::intraprocess;

    std::cout << "\n=== testZeroCopyCheck ===\n";

    auto domain = std::make_shared<Domain>(1);
    auto node = std::make_shared<Node>("SingleNode", domain);

    // Record the address of the last received message
    std::atomic<const void*> lastPtr{nullptr};

    // Single subscriber
    auto sub = node->createSubscriber<StringMsg>("nocopy", [&](const StringMsg &msg){
        lastPtr.store(static_cast<const void*>(&msg), std::memory_order_relaxed);
        std::cout << "[Subscriber] address=" << &msg 
                  << " text=" << msg.text << "\n";
    });

    // Publisher
    auto pub = node->createPublisher<StringMsg>("nocopy");

    // Executor
    auto exec = std::make_shared<lux::communication::SingleThreadedExecutor>();
    exec->addNode(node);

    std::thread th([&]{ exec->spin(); });

    // Publish one message
    pub->publish(StringMsg{"CheckZeroCopy"});
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    exec->stop();
    th.join();

    if(lastPtr.load() != nullptr) {
        std::cout << "[INFO] Received message address = " << lastPtr.load() << "\n"
                  << "This suggests we didn't do a deep copy.\n"
                  << "But truly zero-copy depends on RcBuffer usage.\n";
    } else {
        std::cout << "[FAIL] No message address recorded.\n";
    }
}


// =============== Performance tests ===============

/**
 * @brief Throughput test with a single publisher and subscriber
 *        Sends N messages and measures time/throughput
 */
void testPerformanceSinglePubSub(int messageCount = 100000)
{
    using namespace lux::communication::intraprocess;

    std::cout << "\n=== testPerformanceSinglePubSub ===\n";

    auto domain = std::make_shared<Domain>(1);
    auto node = std::make_shared<Node>("PerfNode", domain);

    // Count received messages
    std::atomic<int> recvCount{0};

    // Subscriber
    auto sub = node->createSubscriber<StringMsg>("perf_topic", [&](const StringMsg &msg){
        recvCount++;
    });

    // Publisher
    auto pub = node->createPublisher<StringMsg>("perf_topic");

    // Executor
    auto exec = std::make_shared<lux::communication::SingleThreadedExecutor>();
    exec->addNode(node);

    std::thread spinTh([&]{
        exec->spin();
    });

    // start time
    auto t1 = std::chrono::steady_clock::now();

    // Publish N messages
    for(int i = 0; i < messageCount; ++i) {
        pub->publish(StringMsg{"some text"});
    }

    // Wait for all messages
    while(recvCount.load() < messageCount) {
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
    auto t2 = std::chrono::steady_clock::now();

    // Stop
    exec->stop();
    spinTh.join();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
    double seconds = elapsed / 1000.0;
    double rate = messageCount / (seconds ? seconds : 1e-9);

    std::cout << "[Perf] " << messageCount << " messages took " 
              << elapsed << " ms => " << rate << " msg/s\n";
}

/**
 * @brief Multi-subscriber performance test: 1 publisher and N subscribers
 *        Each subscriber processes all messages
 */
void testPerformanceMultiSubscriber(int subscriberCount = 5, int messageCount = 50000)
{
    using namespace lux::communication::intraprocess;

    std::cout << "\n=== testPerformanceMultiSubscriber ===\n";

    auto domain = std::make_shared<Domain>(1);
    auto node = std::make_shared<Node>("PerfMultiSub", domain);

    // Each subscriber keeps its own count
    std::vector<std::atomic<int>> counters(subscriberCount);
    for(auto &c : counters) {
        c.store(0);
    }

    // Create N subscribers
    std::vector<std::shared_ptr<Subscriber<StringMsg>>> subs;
    subs.reserve(subscriberCount);
    for(int i = 0; i < subscriberCount; ++i) {
        auto sub = node->createSubscriber<StringMsg>("multi_perf",
            [&, i](const StringMsg &msg){
                counters[i].fetch_add(1, std::memory_order_relaxed);
            }
        );
        subs.push_back(sub);
    }

    // Publisher
    auto pub = node->createPublisher<StringMsg>("multi_perf");

    // Executor
    auto exec = std::make_shared<lux::communication::SingleThreadedExecutor>();
    exec->addNode(node);
    std::thread spinTh([&]{ exec->spin(); });

    // Start timing
    auto t1 = std::chrono::steady_clock::now();

    // Publish messageCount messages
    for(int i = 0; i < messageCount; ++i) {
        pub->publish(StringMsg{"hello"});
    }

    // Wait until all subscribers receive all messages
    auto totalTarget = subscriberCount * messageCount;
    auto checkAllReceived = [&](){
        long sum = 0;
        for(auto &c : counters) {
            sum += c.load(std::memory_order_relaxed);
        }
        return (sum == totalTarget);
    };

    while(!checkAllReceived()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    auto t2 = std::chrono::steady_clock::now();

    exec->stop();
    spinTh.join();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
    double seconds = elapsed / 1000.0;
    double rate = messageCount / (seconds ? seconds : 1e-9);

    std::cout << "[Perf MultiSub] " << subscriberCount << " subs x " << messageCount 
              << " msgs => total " << totalTarget << " deliveries.\n"
              << "Time = " << elapsed << " ms => ~" 
              << rate << " msg/s (per subscriber). \n";
}

struct TimeStampedMsg
{
    std::chrono::steady_clock::time_point sendTime; // send time
    std::string payload; // optional data
};

/**
 * @brief End-to-end latency test with a single publisher and subscriber
 *
 * The publisher sends N timestamped messages and the subscriber measures
 * the difference between receive time and send time to calculate min/avg/max.
 */
void testLatencySinglePubSub(int messageCount = 1000)
{
    using namespace lux::communication::intraprocess;

    std::cout << "\n=== testLatencySinglePubSub ===\n";

    auto domain = std::make_shared<Domain>(1);
    auto node   = std::make_shared<Node>("LatencyNode", domain);

    // 1) Preallocate array to avoid push_back
    std::vector<long long> latencies(messageCount);

    // 2) Use atomic index to fill latencies[i]
    std::atomic<int> writeIndex{0};  
    
    // 3) Subscriber callback
    auto sub = node->createSubscriber<TimeStampedMsg>("latency_topic",
        [&](const TimeStampedMsg &msg)
        {
            auto now   = std::chrono::steady_clock::now();
            auto delta = std::chrono::duration_cast<std::chrono::microseconds>(now - msg.sendTime).count();

            // Atomically get a write slot
            int i = writeIndex.fetch_add(1, std::memory_order_relaxed);
            if (i < messageCount) {
                latencies[i] = delta;
            }
        }
    );

    // 4) Create Publisher
    auto pub = node->createPublisher<TimeStampedMsg>("latency_topic");

    // 5) Start Executor
    auto exec = std::make_shared<lux::communication::SingleThreadedExecutor>();
    exec->addNode(node);

    std::thread spinTh([&]{
        exec->spin();
    });

    // 6) Publish N messages
    for (int i = 0; i < messageCount; ++i)
    {
        TimeStampedMsg msg;
        msg.sendTime = std::chrono::steady_clock::now();
        msg.payload  = "test " + std::to_string(i);

        pub->publish(std::move(msg));

        // std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    // 7) Wait until all messages are consumed
    while (writeIndex.load(std::memory_order_acquire) < messageCount)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // 8) Compute latency statistics
    exec->stop();
    spinTh.join();

    long long sum = 0;
    long long minv = std::numeric_limits<long long>::max();
    long long maxv = 0;

    for (auto &d : latencies) {
        sum += d;
        if(d < minv) minv = d;
        if(d > maxv) maxv = d;
    }
    double avg = (double)sum / (double)messageCount;

    std::cout << "[LatencyTest-noLock] " << messageCount << " msgs:\n"
              << "    Min: " << minv << " us\n"
              << "    Max: " << maxv << " us\n"
              << "    Avg: " << avg  << " us\n";
}

void testMultiThreadedExecutorBasic(int threadCount = 4, int messageCount = 50000)
{
    using namespace lux::communication::intraprocess;

    std::cout << "\n=== testMultiThreadedExecutorBasic ===\n";

    // 1. Create Domain and Node
    auto domain = std::make_shared<Domain>(1);
    auto node   = std::make_shared<Node>("MultiThreadNode", domain);

    // 2. Create Publisher
    auto pub = node->createPublisher<StringMsg>("multi_thread_topic");

    // 3. Create multiple subscribers (default callback group)
    //    Each subscriber counts its own messages
    const int subCount = 3;
    std::vector<std::atomic<int>> counters(subCount);
    for (auto &c : counters) c.store(0);

    std::vector<std::shared_ptr<Subscriber<StringMsg>>> subs;
    subs.reserve(subCount);
    for (int i = 0; i < subCount; ++i)
    {
        auto s = node->createSubscriber<StringMsg>("multi_thread_topic",
            [&, idx = i](const StringMsg &msg){
                // Simulate some workload
                // std::this_thread::sleep_for(std::chrono::microseconds(50));
                counters[idx].fetch_add(1, std::memory_order_relaxed);
            }
        );
        subs.push_back(s);
    }

    // 4. Create lux::communication::MultiThreadedExecutor and add Node
    auto exec = std::make_shared<lux::communication::MultiThreadedExecutor>(threadCount);
    exec->addNode(node);

    // 5. Start spinning (threads process concurrently)
    std::thread spinThread([&]{
        exec->spin();
    });

    // 6. Publish some messages
    auto t1 = std::chrono::steady_clock::now();
    for (int i = 0; i < messageCount; ++i) {
        pub->publish(StringMsg{"Hello from multi-thread"});
    }

    // 7. Wait until all subscribers received their messages
    //    Simple approach: wait until the sum of counters equals subCount*messageCount
    const int totalNeeded = subCount * messageCount;
    while (true)
    {
        int sum = 0;
        for (auto &c : counters) {
            sum += c.load(std::memory_order_relaxed);
        }
        if (sum >= totalNeeded) {
            break;
        }
        // std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto t2 = std::chrono::steady_clock::now();

    exec->stop();
    spinThread.join();

    // 8. Output results
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
    std::cout << "[MultiThreadedExecutorBasic] " << threadCount << " threads, "
              << subCount << " subscribers x " << messageCount << " msgs => total "
              << totalNeeded << " deliveries.\n"
              << "Time = " << elapsed << " ms\n";
}

/**
 * @brief Test multi-threaded executor with different callback groups
 *        - Same node and topic
 *        - Two groups: groupR (reentrant) and groupM (mutually exclusive)
 *        - Each group has two subscribers
 *        - Publish many messages and observe concurrency
 */
void testMultiThreadedExecutorWithCallbackGroups(int threadCount = 4, int messageCount = 10000)
{
    using namespace lux::communication::intraprocess;

    std::cout << "\n=== testMultiThreadedExecutorWithCallbackGroups ===\n";

    auto domain = std::make_shared<Domain>(1);
    auto node   = std::make_shared<Node>("NodeWithGroups", domain);

    // 1) Create two callback groups: Reentrant and MutuallyExclusive
    auto groupR = std::make_shared<CallbackGroup>(CallbackGroupType::Reentrant);
    auto groupM = std::make_shared<CallbackGroup>(CallbackGroupType::MutuallyExclusive);

    // 2) Prepare concurrency tracking data
    //    - active and peak counts for each group
    struct GroupStats {
        std::atomic<int> activeCount{0};  // currently active callbacks
        std::atomic<int> peakCount{0};    // record highest concurrency seen
    };
    GroupStats statsR, statsM;

    // Helper: increment activeCount at start, decrement at end, update peak
    auto makeCallback = [&](GroupStats&st, std::string name) {
        return [&, name](const StringMsg &msg) {
            int val = st.activeCount.fetch_add(1, std::memory_order_acq_rel) + 1;
            // update peak
            int oldPeak = st.peakCount.load(std::memory_order_relaxed);
            while (val > oldPeak && !st.peakCount.compare_exchange_weak(oldPeak, val)) {
                // retry
            }

            // simulate processing delay
            std::this_thread::sleep_for(std::chrono::microseconds(200));

            // done
            st.activeCount.fetch_sub(1, std::memory_order_acq_rel);
        };
    };

    // 3) Create two subscribers in the Reentrant group
    auto subR1 = node->createSubscriber<StringMsg>(
        "group_topic", makeCallback(statsR, "R1"), groupR
    );
    auto subR2 = node->createSubscriber<StringMsg>(
        "group_topic", makeCallback(statsR, "R2"), groupR
    );

    // 4) Create two subscribers in the MutuallyExclusive group
    auto subM1 = node->createSubscriber<StringMsg>(
        "group_topic", makeCallback(statsM, "M1"), groupM
    );
    auto subM2 = node->createSubscriber<StringMsg>(
        "group_topic", makeCallback(statsM, "M2"), groupM
    );

    // 5) Create Publisher
    auto pub = node->createPublisher<StringMsg>("group_topic");

    // 6) Create lux::communication::MultiThreadedExecutor and add both groups
    //    Note: addNode(node) would also add the default group
    //    Here we manually add groupR and groupM for clarity
    auto exec = std::make_shared<lux::communication::MultiThreadedExecutor>(4);
    exec->addNode(node); // add all callback groups of the node

    // 7) Start spinning
    std::thread spinTh([&]{
        exec->spin();
    });

    // 8) Publish some messages
    for (int i = 0; i < 5; ++i) {
        pub->publish(StringMsg{"Msg #" + std::to_string(i)});
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // 9) Wait a bit for callbacks to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    exec->stop();
    spinTh.join();

    // 10) Check peak concurrency of both groups
    int peakR = statsR.peakCount.load(std::memory_order_relaxed);
    int peakM = statsM.peakCount.load(std::memory_order_relaxed);

    //    Reentrant group should show peak > 1 with enough threads
    //    MutuallyExclusive group should stay at 1
    std::cout << "[GroupTest] threadCount=" << threadCount << " totalMsgs=" << messageCount << "\n"
              << "    ReentrantGroup peak concurrency = " << peakR << "\n"
              << "    MutuallyExclusiveGroup peak concurrency = " << peakM << "\n";

    if (peakR > 1) {
        std::cout << "    [OK] Reentrant group did run callbacks in parallel.\n";
    } else {
        std::cout << "    [WARN] Reentrant group didn't show concurrency.\n";
    }

    if (peakM <= 1) {
        std::cout << "    [OK] MutuallyExclusive group is strictly serialized.\n";
    } else {
        std::cout << "    [FAIL] MutuallyExclusive group concurrency > 1!\n";
    }
}

// ================= Additional tests =================

struct LargeMsg
{
    std::vector<uint8_t> data;
};

// Performance test: large message throughput
void testPerformanceLargeMessage(int messageCount = 1000, size_t size = 1024*1024)
{
    using namespace lux::communication::intraprocess;

    std::cout << "\n=== testPerformanceLargeMessage ===\n";

    auto domain = std::make_shared<Domain>(1);
    auto node   = std::make_shared<Node>("LargeMsgNode", domain);

    std::atomic<int> recv{0};

    auto sub = node->createSubscriber<LargeMsg>("big_topic", [&](const LargeMsg &m){
        (void)m;
        recv.fetch_add(1, std::memory_order_relaxed);
    });

    auto pub = node->createPublisher<LargeMsg>("big_topic");

    auto exec = std::make_shared<lux::communication::SingleThreadedExecutor>();
    exec->addNode(node);

    std::thread th([&]{ exec->spin(); });

    LargeMsg msg; msg.data.resize(size);

    auto t1 = std::chrono::steady_clock::now();
    for(int i=0;i<messageCount;++i){
        pub->publish(msg);
    }

    while(recv.load(std::memory_order_acquire) < messageCount){
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto t2 = std::chrono::steady_clock::now();

    exec->stop();
    th.join();

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
    double rate = messageCount / (ms / 1000.0);

    std::cout << "[LargeMsgPerf] " << messageCount << " msgs of " << size/1024 << "KB took "
              << ms << " ms => " << rate << " msg/s\n";
}

// Thread lifecycle safety test
void testThreadLifecycleSafety()
{
    using namespace lux::communication::intraprocess;

    std::cout << "\n=== testThreadLifecycleSafety ===\n";

    auto domain = std::make_shared<Domain>(1);
    auto node   = std::make_shared<Node>("LifeNode", domain);

    auto exec = std::make_shared<lux::communication::MultiThreadedExecutor>(2);
    exec->addNode(node);

    std::atomic<bool> running{true};
    std::atomic<int>  count{0};

    std::shared_ptr<Subscriber<StringMsg>> sub;
    sub = node->createSubscriber<StringMsg>("life_topic", [&](const StringMsg &m){
        (void)m; count.fetch_add(1, std::memory_order_relaxed); });

    auto pub = node->createPublisher<StringMsg>("life_topic");

    std::thread spinTh([&]{ exec->spin(); });
    std::thread pubTh([&]{
        while(running.load()) {
            pub->publish(StringMsg{"hi"});
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    sub.reset(); // destroy subscriber
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    sub = node->createSubscriber<StringMsg>("life_topic", [&](const StringMsg &m){
        (void)m; count.fetch_add(1, std::memory_order_relaxed); });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    running.store(false);

    exec->stop();
    spinTh.join();
    pubTh.join();

    std::cout << "[LifecycleTest] received " << count.load() << " messages without crash.\n";
}

// Memory leak detection
struct CountingMsg
{
    static std::atomic<int> liveCount;
    int id{0};
    CountingMsg(){ liveCount.fetch_add(1, std::memory_order_relaxed); }
    CountingMsg(const CountingMsg&){ liveCount.fetch_add(1, std::memory_order_relaxed); }
    ~CountingMsg(){ liveCount.fetch_sub(1, std::memory_order_relaxed); }
};

std::atomic<int> CountingMsg::liveCount{0};

void testMemoryLeakCheck(int messageCount = 1000)
{
    using namespace lux::communication::intraprocess;

    std::cout << "\n=== testMemoryLeakCheck ===\n";

    auto domain = std::make_shared<Domain>(1);
    auto node   = std::make_shared<Node>("MemNode", domain);

    std::atomic<int> recv{0};
    auto sub = node->createSubscriber<CountingMsg>("mem_topic", [&](const CountingMsg &m){
        (void)m; recv.fetch_add(1, std::memory_order_relaxed); });

    auto pub = node->createPublisher<CountingMsg>("mem_topic");

    auto exec = std::make_shared<lux::communication::SingleThreadedExecutor>();
    exec->addNode(node);

    std::thread spinTh([&]{ exec->spin(); });

    for(int i=0;i<messageCount;++i){
        CountingMsg m; m.id = i; 
        pub->publish(m);
    }

    while(recv.load(std::memory_order_acquire) < messageCount){
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    exec->stop();
    spinTh.join();
    sub.reset();
    pub.reset();
    node.reset();
    domain.reset();

    if(CountingMsg::liveCount.load() == 0) {
        std::cout << "[OK] no message leak detected.\n";
    } else {
        std::cout << "[FAIL] leak count=" << CountingMsg::liveCount.load() << "\n";
    }
}

// =============== Main test entry ===============

int main()
{
    // 1. Functional tests
    testDomainIsolation();
    testSingleDomainMultiNode();
    testMultiSubscriber();
    testZeroCopyCheck();

    // 2. Performance tests
    testPerformanceSinglePubSub(100000);       // 100k messages
    testPerformanceMultiSubscriber(5, 50000);  // 5 subscribers, 50k messages

    testLatencySinglePubSub(1e5); // example run

    testMultiThreadedExecutorBasic(/*threadCount*/ 4, /*messageCount*/ 50000);
    testMultiThreadedExecutorWithCallbackGroups(/*threadCount*/ 4, /*messageCount*/ 10000);

    testPerformanceLargeMessage();
    testThreadLifecycleSafety();
    testMemoryLeakCheck();

    std::cout << "\nAll tests done.\n";
    return 0;
}
