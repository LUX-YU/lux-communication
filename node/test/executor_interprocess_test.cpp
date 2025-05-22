#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <memory>

#include <lux/communication/interprocess/Node.hpp>
#include <lux/communication/Executor.hpp>
#include <lux/communication/introprocess/Node.hpp> // for Executor::addNode definition

struct IntMsg { int value; };

void runExecutorInterprocess()
{
    using namespace lux::communication;

    // Publisher node and subscriber node
    interprocess::Node nodePub("pub");
    interprocess::Node nodeSub("sub");

    std::atomic<int> subCount1{0};
    std::atomic<int> subCount2{0};

    // Executor to drive callbacks from nodeSub
    auto exec = std::make_shared<SingleThreadedExecutor>();
    exec->addCallbackGroup(nodeSub.getDefaultCallbackGroup());

    // Two subscribers on the same topic
    auto sub1 = nodeSub.createSubscriber<IntMsg>("topic", [&](const IntMsg&m){ subCount1++; });
    auto sub2 = nodeSub.createSubscriber<IntMsg>("topic", [&](const IntMsg&m){ subCount2++; });

    std::thread spinThread([&]{ exec->spin(); });

    auto pub = nodePub.createPublisher<IntMsg>("topic");

    // Give ZMQ some time to set up
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    for(int i=0;i<5;i++)
    {
        pub->publish(IntMsg{i});
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Allow some time for delivery
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    exec->stop();
    spinThread.join();

    std::cout << "sub1=" << subCount1.load() << " sub2=" << subCount2.load() << std::endl;
}

int main()
{
    runExecutorInterprocess();
    return 0;
}
