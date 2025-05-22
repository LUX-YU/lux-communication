#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <memory>

#include <lux/communication/introprocess/Node.hpp>
#include <lux/communication/interprocess/Node.hpp>
#include <lux/communication/Executor.hpp>

struct Msg { int value; };

// Test RAII lifecycle for interprocess publisher/subscriber
void testLifecycle()
{
    using namespace lux::communication;
    std::cout << "\n=== lifecycle test ===\n";
    {
        interprocess::Node nodeA("A");
        interprocess::Node nodeB("B");

        std::atomic<int> count{0};
        auto sub = nodeB.createSubscriber<Msg>("ping", [&](const Msg&m){count++;});
        auto pub = nodeA.createPublisher<Msg>("ping");
        pub->publish(Msg{1});
        nodeA.stop();
        nodeB.stop();
    }
    // destructors should stop without hang
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::cout << "[OK] lifecycle completed" << std::endl;
}

// Test intra and interprocess in same executor
void testIntraInter()
{
    using namespace lux::communication;
    std::cout << "\n=== intra & inter mixed ===\n";
    auto domain = std::make_shared<introprocess::Domain>(1);
    auto inode = std::make_shared<introprocess::Node>("inode", domain);
    interprocess::Node pnode("pnode");

    std::atomic<int> intraCount{0}, interCount{0};

    auto ipub = inode->createPublisher<Msg>("ch");
    auto isub = inode->createSubscriber<Msg>("ch", [&](const Msg&m){intraCount++;});

    auto psub = pnode.createSubscriber<Msg>("ch", [&](const Msg&m){interCount++;});
    auto ppub = pnode.createPublisher<Msg>("ch");

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    ipub->publish(Msg{0});
    ppub->publish(Msg{1});
    pnode.stop();
    std::cout << "intra="<<intraCount.load()<<" inter="<<interCount.load()<<"\n";
}

int main()
{
    testLifecycle();
    testIntraInter();
    return 0;
}
