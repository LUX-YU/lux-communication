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
    interprocess::Node nodePub("pub");
    interprocess::Node nodeSub("sub");

    std::atomic<int> count{0};
    auto exec = std::make_shared<SingleThreadedExecutor>();
    exec->addCallbackGroup(nodeSub.getDefaultCallbackGroup());
    std::thread th([&]{ exec->spin(); });

    auto sub = nodeSub.createSubscriber<IntMsg>("topic", [&](const IntMsg&m){count++;});
    auto pub = nodePub.createPublisher<IntMsg>("topic");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    for(int i=0;i<10;i++){ pub->publish(IntMsg{i}); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    exec->stop();
    th.join();
    std::cout << "received=" << count.load() << std::endl;
}

int main(){ runExecutorInterprocess(); return 0; }
