#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <memory>
#include <array>

#include <lux/communication/interprocess/Node.hpp>
#include <lux/communication/Executor.hpp>
#include <lux/communication/introprocess/Node.hpp> // Executor::addNode

// Generic throughput test template for different message sizes

template<size_t SIZE>
void throughputTest(int messageCount = 100000)
{
    using namespace lux::communication;
    auto domain = std::make_shared<introprocess::Domain>(1);
    interprocess::Node nodePub("pub", domain);
    interprocess::Node nodeSub("sub", domain);

    struct Msg { std::array<uint8_t, SIZE> data; };

    std::atomic<int> recvCount{0};
    auto exec = std::make_shared<SingleThreadedExecutor>();
    exec->addCallbackGroup(nodeSub.getDefaultCallbackGroup());
    auto sub = nodeSub.createSubscriber<Msg>("topic", [&](const Msg&){ recvCount++; });
    std::thread spinThread([&]{ exec->spin(); });

    auto pub = nodePub.createPublisher<Msg>("topic");
    Msg msg{}; // zero-filled

    auto t1 = std::chrono::steady_clock::now();
    for(int i=0;i<messageCount;++i){
        pub->publish(msg);
    }
    auto tPubDone = std::chrono::steady_clock::now();

    while(recvCount.load(std::memory_order_acquire) < messageCount){
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto t2 = std::chrono::steady_clock::now();

    exec->stop();
    spinThread.join();

    auto sendMs = std::chrono::duration_cast<std::chrono::milliseconds>(tPubDone - t1).count();
    auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();

    std::cout << "[size=" << SIZE << "] sent=" << messageCount
              << " recv=" << recvCount.load()
              << " send_time=" << sendMs << "ms total_time=" << totalMs << "ms"
              << std::endl;
}

int main()
{
    throughputTest<64>();
    throughputTest<512>();
    throughputTest<4096>();
    return 0;
}
