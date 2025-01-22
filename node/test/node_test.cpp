#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <functional>
#include <cassert>

// 你的头文件路径根据项目结构自行调整
#include <lux/communication/introprocess/Node.hpp>
#include <lux/communication/introprocess/Executor.hpp>

struct StringMsg
{
    std::string text;
};

struct ComplexMsg
{
    std::vector<int> data;
    int id;
};

// =============== 功能测试用例 ===============

/**
 * @brief 测试多域隔离：
 *  - domain1 和 domain2 分别创建 Node
 *  - 在 domain1 的 NodeA 发布消息
 *  - 在 domain2 的 NodeB 订阅相同的话题名称，却收不到消息
 */
void testDomainIsolation()
{
    using namespace lux::communication::introprocess;

    std::cout << "\n=== testDomainIsolation ===\n";

    auto domain1 = std::make_shared<Domain>(1);
    auto domain2 = std::make_shared<Domain>(2);

    // NodeA 和 NodeB 分别在不同 Domain
    auto nodeA = std::make_shared<Node>("NodeA", domain1);
    auto nodeB = std::make_shared<Node>("NodeB", domain2);

    // 准备一个标志，用于检查 NodeB 是否意外收到消息
    std::atomic<int> bReceivedCount{0};

    // 创建一个Publisher在 domain1
    auto pubA = nodeA->createPublisher<StringMsg>("chatter");

    // 在 domain2 上订阅相同 "chatter"
    auto subB = nodeB->createSubscriber<StringMsg>(
        "chatter", 
        [&](const StringMsg &msg){
            // 如果进到这里，就说明收到了
            ++bReceivedCount;
        }
    );

    // 创建一个执行器，专门跑 nodeB
    auto execB = std::make_shared<SingleThreadedExecutor>();
    // 把 nodeB 加入执行器（默认回调组）
    execB->addNode(nodeB);

    // 启动 spin
    std::thread spinThreadB([&]{
        execB->spin();
    });

    // 发布几条消息 (在 NodeA 中)
    for(int i = 0; i < 3; ++i) {
        pubA->publish(StringMsg{"Hello from domain1"});
    }

    // 等待一会
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 停止 execB
    execB->stop();
    spinThreadB.join();

    // 检查 bReceivedCount
    if (bReceivedCount.load() == 0) {
        std::cout << "[OK] domain isolation works, NodeB got 0 messages.\n";
    } else {
        std::cout << "[FAIL] domain isolation failed, NodeB got " 
                  << bReceivedCount.load() << " messages.\n";
    }
}

/**
 * @brief 测试在同一个Domain内，多节点互通：
 *  - 同一个 domain 下的 NodeX、NodeY 分别创建 pub/sub
 *  - 验证 Subscriber 是否收到了消息
 *  - 验证 RAII：离开作用域后 Topic 是否自动清理
 */
void testSingleDomainMultiNode()
{
    using namespace lux::communication::introprocess;

    std::cout << "\n=== testSingleDomainMultiNode ===\n";

    auto domain = std::make_shared<Domain>(1);
    {
        auto nodeA = std::make_shared<Node>("NodeA", domain);
        auto nodeB = std::make_shared<Node>("NodeB", domain);

        // 在 nodeA 上创建 publisher
        auto pub = nodeA->createPublisher<StringMsg>("chat");

        // 在 nodeB 上创建 subscriber
        std::atomic<int> subCount{0};
        auto sub = nodeB->createSubscriber<StringMsg>("chat",
            [&](const StringMsg &msg){
                // 收到消息
                ++subCount;
                std::cout << "[NodeB] got: " << msg.text << "\n";
            });

        // 为 nodeB 创建执行器
        auto execB = std::make_shared<SingleThreadedExecutor>();
        execB->addNode(nodeB);

        // 启动一个线程 spin nodeB
        std::thread th([&]{ execB->spin(); });

        // 发布消息
        for(int i = 0; i < 5; ++i) {
            pub->publish(StringMsg{"Hello " + std::to_string(i)});
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // 停止
        execB->stop();
        th.join();

        // 检查 subCount
        if(subCount.load() == 5) {
            std::cout << "[OK] NodeB received all 5 messages.\n";
        } else {
            std::cout << "[FAIL] NodeB only got " << subCount.load() << " messages.\n";
        }

        // 出这个大花括号时，Node/Publisher/Subscriber 都析构
        // Topic 没有人引用，会被 Domain 移除
    }

    // 这里 domain 还活着，但其 Topic 应该已被移除
    std::cout << "[INFO] Left the scope, Node/Publisher/Subscriber destroyed.\n";
}

/**
 * @brief 测试多个 Subscriber 订阅同一话题，看是否都能收到
 *        同时在另一个节点有另一个话题，不互相干扰
 */
void testMultiSubscriber()
{
    using namespace lux::communication::introprocess;

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

    // NodeB: subscriber2 -> topic "chat" (第二个订阅者)
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

    // 为 nodeB 创建执行器
    auto execB = std::make_shared<SingleThreadedExecutor>();
    execB->addNode(nodeB);
    std::thread spinTh([&]{ execB->spin(); });

    // 发布
    pubChat->publish(StringMsg{"Hello 1"});
    pubChat->publish(StringMsg{"Hello 2"});
    pubData->publish(ComplexMsg{{1,2,3,4}, 99});

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 停止
    execB->stop();
    spinTh.join();

    // 检查
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
 * @brief 零拷贝简单测试: 检查一下对象地址是否一致
 * (当然，在真正工程中，还需结合 RcBuffer 的引用计数检查。这里仅做简单演示)
 */
void testZeroCopyCheck()
{
    using namespace lux::communication::introprocess;

    std::cout << "\n=== testZeroCopyCheck ===\n";

    auto domain = std::make_shared<Domain>(1);
    auto node = std::make_shared<Node>("SingleNode", domain);

    // 记录一下最后收到的指针地址
    std::atomic<const void*> lastPtr{nullptr};

    // 一个 subscriber
    auto sub = node->createSubscriber<StringMsg>("nocopy", [&](const StringMsg &msg){
        lastPtr.store(static_cast<const void*>(&msg), std::memory_order_relaxed);
        std::cout << "[Subscriber] address=" << &msg 
                  << " text=" << msg.text << "\n";
    });

    // publisher
    auto pub = node->createPublisher<StringMsg>("nocopy");

    // 执行器
    auto exec = std::make_shared<SingleThreadedExecutor>();
    exec->addNode(node);

    std::thread th([&]{ exec->spin(); });

    // 发布一条
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


// =============== 性能测试用例 ===============

/**
 * @brief 在同一个Node下，单Publisher / 单Subscriber，发送N条消息
 *        测量耗时并计算吞吐量
 */
void testPerformanceSinglePubSub(int messageCount = 100000)
{
    using namespace lux::communication::introprocess;

    std::cout << "\n=== testPerformanceSinglePubSub ===\n";

    auto domain = std::make_shared<Domain>(1);
    auto node = std::make_shared<Node>("PerfNode", domain);

    // 统计收到消息数
    std::atomic<int> recvCount{0};

    // Subscriber
    auto sub = node->createSubscriber<StringMsg>("perf_topic", [&](const StringMsg &msg){
        recvCount++;
    });

    // Publisher
    auto pub = node->createPublisher<StringMsg>("perf_topic");

    // Executor
    auto exec = std::make_shared<SingleThreadedExecutor>();
    exec->addNode(node);

    std::thread spinTh([&]{
        exec->spin();
    });

    // start time
    auto t1 = std::chrono::steady_clock::now();

    // 发布 N 条消息
    for(int i = 0; i < messageCount; ++i) {
        pub->publish(StringMsg{"some text"});
    }

    // 等待收完
    while(recvCount.load() < messageCount) {
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
    auto t2 = std::chrono::steady_clock::now();

    // stop
    exec->stop();
    spinTh.join();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
    double seconds = elapsed / 1000.0;
    double rate = messageCount / (seconds ? seconds : 1e-9);

    std::cout << "[Perf] " << messageCount << " messages took " 
              << elapsed << " ms => " << rate << " msg/s\n";
}

/**
 * @brief 多Subscriber性能测试：1个Publisher + N个Subscriber
 *        每个Subscriber都需要处理全部消息
 */
void testPerformanceMultiSubscriber(int subscriberCount = 5, int messageCount = 50000)
{
    using namespace lux::communication::introprocess;

    std::cout << "\n=== testPerformanceMultiSubscriber ===\n";

    auto domain = std::make_shared<Domain>(1);
    auto node = std::make_shared<Node>("PerfMultiSub", domain);

    // 为了统计结果，需要每个 subscriber 单独记录它收到的消息数
    std::vector<std::atomic<int>> counters(subscriberCount);
    for(auto &c : counters) {
        c.store(0);
    }

    // 创建 N 个订阅者
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
    auto exec = std::make_shared<SingleThreadedExecutor>();
    exec->addNode(node);
    std::thread spinTh([&]{ exec->spin(); });

    // 开始计时
    auto t1 = std::chrono::steady_clock::now();

    // 发布 messageCount 条消息
    for(int i = 0; i < messageCount; ++i) {
        pub->publish(StringMsg{"hello"});
    }

    // 等待直到所有订阅者都收到所有消息
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
    std::chrono::steady_clock::time_point sendTime; // 发送时间
    std::string payload; // 可附加其它数据
};

/**
 * @brief 测试单 Publisher / 单 Subscriber 的端到端延迟
 *
 * 思路：Publisher 发布 N 条带时间戳的消息，Subscriber 在回调中计算当前时间 - 发送时间
 *       统计最小、最大和平均延迟。
 */
void testLatencySinglePubSub(int messageCount = 1000)
{
    using namespace lux::communication::introprocess;

    std::cout << "\n=== testLatencySinglePubSub ===\n";

    auto domain = std::make_shared<Domain>(1);
    auto node   = std::make_shared<Node>("LatencyNode", domain);

    // 1) 预分配数组，这样就不需要 push_back
    std::vector<long long> latencies(messageCount);

    // 2) 用原子索引来在回调中写入 latencies[i]
    std::atomic<int> writeIndex{0};  
    
    // 3) 创建订阅者回调
    auto sub = node->createSubscriber<TimeStampedMsg>("latency_topic",
        [&](const TimeStampedMsg &msg)
        {
            auto now   = std::chrono::steady_clock::now();
            auto delta = std::chrono::duration_cast<std::chrono::microseconds>(now - msg.sendTime).count();

            // 原子地获取一个写入位置
            int i = writeIndex.fetch_add(1, std::memory_order_relaxed);
            if (i < messageCount) {
                latencies[i] = delta;
            }
        }
    );

    // 4) 创建 Publisher
    auto pub = node->createPublisher<TimeStampedMsg>("latency_topic");

    // 5) 启动 Executor
    auto exec = std::make_shared<SingleThreadedExecutor>();
    exec->addNode(node);

    std::thread spinTh([&]{
        exec->spin();
    });

    // 6) 发布 N 条消息
    for (int i = 0; i < messageCount; ++i)
    {
        TimeStampedMsg msg;
        msg.sendTime = std::chrono::steady_clock::now();
        msg.payload  = "test " + std::to_string(i);

        pub->publish(std::move(msg));

        // std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    // 7) 等待所有消息被订阅 (直到 writeIndex == messageCount)
    while (writeIndex.load(std::memory_order_acquire) < messageCount)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // 8) 统计延迟
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
    using namespace lux::communication::introprocess;

    std::cout << "\n=== testMultiThreadedExecutorBasic ===\n";

    // 1. 创建 Domain、Node
    auto domain = std::make_shared<Domain>(1);
    auto node   = std::make_shared<Node>("MultiThreadNode", domain);

    // 2. 创建 Publisher
    auto pub = node->createPublisher<StringMsg>("multi_thread_topic");

    // 3. 创建多个 Subscriber (都在 node 默认回调组)
    //    每个订阅者都统计自己收到的消息数
    const int subCount = 3;
    std::vector<std::atomic<int>> counters(subCount);
    for (auto &c : counters) c.store(0);

    std::vector<std::shared_ptr<Subscriber<StringMsg>>> subs;
    subs.reserve(subCount);
    for (int i = 0; i < subCount; ++i)
    {
        auto s = node->createSubscriber<StringMsg>("multi_thread_topic",
            [&, idx = i](const StringMsg &msg){
                // 做一些模拟开销
                std::this_thread::sleep_for(std::chrono::microseconds(50));
                counters[idx].fetch_add(1, std::memory_order_relaxed);
            }
        );
        subs.push_back(s);
    }

    // 4. 创建 MultiThreadedExecutor 并添加 Node
    auto exec = std::make_shared<MultiThreadedExecutor>(threadCount);
    exec->addNode(node);

    // 5. 启动 spin (多个线程并发处理)
    std::thread spinThread([&]{
        exec->spin();
    });

    // 6. 发布若干消息
    auto t1 = std::chrono::steady_clock::now();
    for (int i = 0; i < messageCount; ++i) {
        pub->publish(StringMsg{"Hello from multi-thread"});
    }

    // 7. 等待所有订阅者都收完消息
    //    这里简单做法：等待 counters 之和 == subCount*messageCount
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
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto t2 = std::chrono::steady_clock::now();

    exec->stop();
    spinThread.join();

    // 8. 统计结果
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
    std::cout << "[MultiThreadedExecutorBasic] " << threadCount << " threads, "
              << subCount << " subscribers x " << messageCount << " msgs => total "
              << totalNeeded << " deliveries.\n"
              << "Time = " << elapsed << " ms\n";
}

/**
 * @brief 测试多线程执行器 + 不同类型的回调组 (Reentrant / MutuallyExclusive)
 *        - 同一个 Node ，同一个话题
 *        - 有两个分组 groupR (可重入)，groupM (互斥)
 *        - groupR 里有2个subscriber；groupM 里也有2个subscriber
 *        - 大量发布消息后，观察回调执行的并发度，验证 Reentrant 组确实能并发，MutuallyExclusive 组是串行。
 */
void testMultiThreadedExecutorWithCallbackGroups(int threadCount = 4, int messageCount = 10000)
{
    using namespace lux::communication::introprocess;

    std::cout << "\n=== testMultiThreadedExecutorWithCallbackGroups ===\n";

    auto domain = std::make_shared<Domain>(1);
    auto node   = std::make_shared<Node>("NodeWithGroups", domain);

    // 1) 创建两个回调组: Reentrant 和 MutuallyExclusive
    auto groupR = std::make_shared<CallbackGroup>(CallbackGroupType::Reentrant);
    auto groupM = std::make_shared<CallbackGroup>(CallbackGroupType::MutuallyExclusive);

    // 2) 准备统计并发度的辅助数据
    //    - 对 Reentrant 分组的 "当前活跃数"、"峰值并发数"
    //    - 对 MutuallyExclusive 分组的同理
    struct GroupStats {
        std::atomic<int> activeCount{0};  // 当前活跃的回调数
        std::atomic<int> peakCount{0};    // 记录曾出现的最大并发数
    };
    GroupStats statsR, statsM;

    // 帮助函数: 在回调开始时 activeCount++，结束时 activeCount--，
    //            并记录 peakCount。
    auto makeCallback = [&](GroupStats&st, std::string name) {
        return [&, name](const StringMsg &msg) {
            int val = st.activeCount.fetch_add(1, std::memory_order_acq_rel) + 1;
            // 更新 peak
            int oldPeak = st.peakCount.load(std::memory_order_relaxed);
            while (val > oldPeak && !st.peakCount.compare_exchange_weak(oldPeak, val)) {
                // retry
            }

            // 模拟一点处理开销
            std::this_thread::sleep_for(std::chrono::microseconds(200));

            // done
            st.activeCount.fetch_sub(1, std::memory_order_acq_rel);
        };
    };

    // 3) 分别在 Reentrant 分组里创建2个 Subscriber
    auto subR1 = node->createSubscriber<StringMsg>(
        "group_topic", makeCallback(statsR, "R1"), groupR
    );
    auto subR2 = node->createSubscriber<StringMsg>(
        "group_topic", makeCallback(statsR, "R2"), groupR
    );

    // 4) 分别在 MutuallyExclusive 分组里创建2个 Subscriber
    auto subM1 = node->createSubscriber<StringMsg>(
        "group_topic", makeCallback(statsM, "M1"), groupM
    );
    auto subM2 = node->createSubscriber<StringMsg>(
        "group_topic", makeCallback(statsM, "M2"), groupM
    );

    // 5) 创建 Publisher
    auto pub = node->createPublisher<StringMsg>("group_topic");

    // 6) 创建 MultiThreadedExecutor 并添加这两个分组
    //    注意：Executor::addNode(node) 会把 node 的默认回调组加进来，
    //    我们这边还有 groupR/groupM，要么手动 addCallbackGroup()，
    //    要么通过 node->createSubscriber(..., group) 后，group->setExecutor(...) 会自动关联。
    //    简化起见，直接 addNode(node) 也可行，因为 node->createSubscriber() 内部会 addSubscriber(...)，
    //    CallbackGroup 一旦被 Executor add 之后，就会执行 spinSome() 时调度它们。
    //    这里演示一下手动加：
    auto exec = std::make_shared<MultiThreadedExecutor>(4);
    //exec->addNode(node); // 也可以这样把整个 node 直接加进来
    // 或者手动把 groupR/groupM 都加进 executor
    exec->addCallbackGroup(groupR);
    exec->addCallbackGroup(groupM);

    // 7) 启动 spin
    std::thread spinTh([&]{
        exec->spin();
    });

    // 8) 发布若干消息
    for (int i = 0; i < 5; ++i) {
        pub->publish(StringMsg{"Msg #" + std::to_string(i)});
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // 9) 这里简单等待一会儿，让回调都处理完 (也可做更严格的计数等待)
    //    因为 each subscriber 处理 10k msgs * 2 subscribers * 2 groups = 4 * 10000 total deliveries
    //    处理完成需要一点时间
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    exec->stop();
    spinTh.join();

    // 10) 查看两个分组的并发峰值
    int peakR = statsR.peakCount.load(std::memory_order_relaxed);
    int peakM = statsM.peakCount.load(std::memory_order_relaxed);

    //    Reentrant 组如果有充足线程，应出现 peak > 1 (表示并发执行过)
    //    MutuallyExclusive 组理应一直是 1
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

// =============== 主测试入口 ===============

int main()
{
    // 1. 功能测试
    testDomainIsolation();
    testSingleDomainMultiNode();
    testMultiSubscriber();
    testZeroCopyCheck();

    // 2. 性能测试
    testPerformanceSinglePubSub(100000);       // 10万条
    testPerformanceMultiSubscriber(5, 50000);  // 5个订阅者，5万条

    testLatencySinglePubSub(1e5); // 1e6条，示例

    testMultiThreadedExecutorBasic(/*threadCount*/ 4, /*messageCount*/ 50000);
    testMultiThreadedExecutorWithCallbackGroups(/*threadCount*/ 4, /*messageCount*/ 10000);

    std::cout << "\nAll tests done.\n";
    return 0;
}
