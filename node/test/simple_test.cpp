#include <iostream>
#include <lux/communication/intraprocess/Node.hpp>
#include <lux/communication/intraprocess/Publisher.hpp>
#include <lux/communication/intraprocess/Subscriber.hpp>
#include <lux/communication/ExecutorBase.hpp>

int main()
{
	using namespace lux::communication::intraprocess;
	std::atomic<int> count;

	Node node("default node");
	Publisher<int> publisher("/whatever/topic", &node);
	Subscriber<int> subscriber(
		"/whatever/topic", &node,
		[&count](const std::shared_ptr<int> data)
		{
			count++;
			std::cout << *data << std::endl;
		}
	);

	lux::communication::SingleThreadedExecutor executor;
		
	std::thread t(
		[&publisher]()
		{
			for(int i = 0; i < 10; i++)
				publisher.publish(i);
		}
	);

	executor.addNode(&node);
	while(count != 10) {
		executor.spinSome();
	}
	executor.removeNode(&node);

	t.join();

	return 0;
}