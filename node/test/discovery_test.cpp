#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <memory>

#include <lux/communication/interprocess/Node.hpp>
#include <lux/communication/Executor.hpp>
#include <lux/communication/intraprocess/Node.hpp> // for Executor::addNode

struct IntMsg { int value; };

static void runDiscoveryTest()
{
    using namespace lux::communication;
    interprocess::Node node_sub("sub");
    std::atomic<int> count{0};

    auto exec = std::make_shared<SingleThreadedExecutor>();
    exec->addCallbackGroup(node_sub.getDefaultCallbackGroup());
    auto sub = node_sub.createSubscriber<IntMsg>("topic", [&](const IntMsg&){count++;});
    std::thread th([&]{ exec->spin(); });

    // Start publisher a bit later to test discovery
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    interprocess::Node nodePub("pub");
    auto pub = nodePub.createPublisher<IntMsg>("topic");

    // Give subscriber time to connect to the new publisher
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    for(int i=0;i<5;++i){
        pub->publish(IntMsg{i});
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    exec->stop();
    th.join();
    std::cout << "discovery_count=" << count.load() << std::endl;
}

int main()
{
    runDiscoveryTest();
    return 0;
}
