#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <memory>

#include <lux/communication/interprocess/Node.hpp>
#include <lux/communication/intraprocess/Node.hpp> // for Executor::addNode definition

struct Msg { int value; };

static void runSpinTest()
{
    using namespace lux::communication;
    interprocess::Node nodePub("pub");
    interprocess::Node nodeSub("sub");

    auto group2 = std::make_shared<CallbackGroup>(CallbackGroupType::MutuallyExclusive);
    std::atomic<int> count1{0};
    std::atomic<int> count2{0};

    auto sub1 = nodeSub.createSubscriber<Msg>("topic", [&](const Msg&){ count1++; });
    auto sub2 = nodeSub.createSubscriber<Msg>("topic", [&](const Msg&){ count2++; }, group2);

    std::thread spinThread([&]{ nodeSub.spin(); });

    auto pub = nodePub.createPublisher<Msg>("topic");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    for(int i=0;i<5;++i)
    {
        pub->publish(Msg{i});
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    nodeSub.stop();
    spinThread.join();
    nodePub.stop();

    std::cout << "sub1=" << count1.load() << " sub2=" << count2.load() << std::endl;
}

int main()
{
    runSpinTest();
    return 0;
}
