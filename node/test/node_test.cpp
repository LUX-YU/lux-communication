#include <lux/communication/introprocess/Publisher.hpp>
#include <lux/communication/introprocess/Subscriber.hpp>
#include <iostream>
#include <thread>

using Core		 = lux::communication::introprocess::Core;
using Node		 = lux::communication::introprocess::Node;
using Publisher  = lux::communication::introprocess::Publisher<std::string>;
using Subscriber = lux::communication::introprocess::Subscriber<std::string>;

class PublisherNode : public Node
{
public:
	PublisherNode(std::shared_ptr<Core> core)
		: Node(std::move(core), "Publish Node"), count_(0){
		publisher_ = createPublisher<std::string>("string_topic", 10);
	}

	~PublisherNode() {
		exit_ = true;
		std::cout << "PublisherNode exit" << std::endl;
	}

	void start() {
		std::jthread(
			[this]() {
				loop();
			}
		).swap(thread_);
	}

	const size_t count() const {
		return count_;
	}

private:
	
	void loop() {
		while (core().ok() && !exit_) {
			publisher_->pub("hello world");
			count_++;
		}
	}

	std::atomic<bool>			exit_{false};
	std::size_t					count_;
	std::shared_ptr<Publisher>	publisher_;
	std::jthread				thread_;
};

class SubscriberNode : public Node
{
public:
	SubscriberNode(std::shared_ptr<Core> core)
		: Node(std::move(core), "Publish Node"){
		subscriber_ = createSubscriber<std::string>(
			"string_topic",
			[this](std::unique_ptr<std::string> message) {
				if (count_++ % 100000 == 0) {
					std::cout << count_ << std::endl;
					count_++;
				}
			},
			10
		);
		count_ = 0;
	}

	~SubscriberNode() {
		exit_ = true;
		std::cout << "SubscriberNode exit" << std::endl;
	}

	void start() {
		std::jthread(
			[this]() {
				while (!exit_ && core().ok()) {
					spinOnce();
				}
			}
		).swap(thread_);
	}

	size_t count() const {
		return count_;
	}

private:

	std::size_t					count_;
	bool						exit_{false};
	std::shared_ptr<Subscriber>	subscriber_;
	std::jthread				thread_;
};


int main(int argc, char* argv[])
{
	auto core = Core::create(argc, argv);
	core->init();
	
	SubscriberNode	subscriber_node(core);
	PublisherNode	publish_node_1(core);
	PublisherNode	publish_node_2(core);

	subscriber_node.start();
	publish_node_1.start();
	publish_node_2.start();

	std::this_thread::sleep_for(std::chrono::seconds(10));

	std::cout << subscriber_node.count() << std::endl;
	// shutdown() will set ok to false, so every node will stop spining
	core->shutdown();

	return 0;
}
