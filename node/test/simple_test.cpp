#include <iostream>
#include <lux/communication/Node.hpp>
#include <lux/communication/executor/SingleThreadedExecutor.hpp>

int main()
{
	namespace comm = lux::communication;
	std::atomic<int> count{0};

	comm::Node node("default node");
	auto publisher = node.createPublisher<int>("/whatever/topic");
	auto subscriber = node.createSubscriber<int>(
		"/whatever/topic",
		[&count](const int& data)
		{
			count++;
			std::cout << data << std::endl;
		}
	);

	comm::SingleThreadedExecutor executor;
		
	std::thread t(
		[&publisher]()
		{
			for(int i = 0; i < 10; i++)
				publisher->publish(i);
		}
	);

	executor.addNode(&node);
	while(count != 10) {
		executor.spinSome();
	}
	executor.removeNode(&node);

	t.join();
	node.stop();

	return 0;
}