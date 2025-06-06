#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <functional>
#include <cassert>

// include headers according to project layout
#include <lux/communication/intraprocess/Node.hpp>
#include <lux/communication/Executor.hpp>

struct StringMsg
{
	std::string text;
};

struct ComplexMsg
{
	std::vector<int> data;
	int id;
};

// =============== Functional tests ===============

/**
 * @brief Test domain isolation
 *  - Create nodes in domain1 and domain2
 *  - NodeA in domain1 publishes messages
 *  - NodeB in domain2 subscribes to the same topic but should not receive them
 */
static void testDomainIsolation()
{
	using namespace lux::communication::intraprocess;

	std::cout << "\n=== testDomainIsolation ===\n";
	auto domain_1 = std::make_shared<Domain>(1);
	auto domain_2 = std::make_shared<Domain>(2);

	// NodeA and NodeB belong to different domains
	auto node_a = std::make_shared<Node>("NodeA", domain_1);
	auto node_b = std::make_shared<Node>("NodeB", domain_2);

	// Flag to check if NodeB unexpectedly receives messages
	std::atomic<int> b_received_count{ 0 };
	// Create a publisher in domain1
	auto pub_a = node_a->createPublisher<StringMsg>("chatter");

	// Subscribe to the same "chatter" in domain2
	auto sub_b = node_b->createSubscriber<StringMsg>(
		"chatter",
		[&](const StringMsg& msg) {
			// Getting here means NodeB received a message
			++b_received_count;
		}
	);

	// Executor running nodeB
	auto exec_b = std::make_shared<lux::communication::SingleThreadedExecutor>();
	// Add nodeB to the executor (default callback group)
	exec_b->addNode(node_b);

	// Start spinning
	std::thread spin_thread_b([&] {
		exec_b->spin();
		});

	// Publish a few messages from NodeA
	for (int i = 0; i < 3; ++i) {
		pub_a->publish(StringMsg{ "Hello from domain1" });
	}
	// Wait a moment
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	// Stop execB
	exec_b->stop();
	spin_thread_b.join();

	// Check b_received_count
	if (b_received_count.load() == 0) {
		std::cout << "[OK] domain isolation works, NodeB got 0 messages.\n";
	}
	else {
		std::cout << "[FAIL] domain isolation failed, NodeB got "
			<< b_received_count.load() << " messages.\n";
	}
}

/**
 * @brief Test communication of multiple nodes in the same domain
 *  - NodeX and NodeY create publishers/subscribers under the same domain
 *  - Verify that the subscriber receives messages
 *  - Verify RAII: topics should be cleaned after leaving scope
 */
static void testSingleDomainMultiNode()
{
	using namespace lux::communication::intraprocess;

	std::cout << "\n=== testSingleDomainMultiNode ===\n";
	auto domain = std::make_shared<Domain>(1);
	{
		auto node_a = std::make_shared<Node>("NodeA", domain);
		auto node_b = std::make_shared<Node>("NodeB", domain);

		// Create publisher on nodeA
		auto pub = node_a->createPublisher<StringMsg>("chat");

		// Create subscriber on nodeB
		std::atomic<int> sub_count{ 0 };
		auto sub = node_b->createSubscriber<StringMsg>("chat",
			[&](const StringMsg& msg) {
				// Message received
				++sub_count;
				std::cout << "[NodeB] got: " << msg.text << "\n";
			}
		);

		// Executor for nodeB
		auto exec_b = std::make_shared<lux::communication::SingleThreadedExecutor>();
		exec_b->addNode(node_b);

		// Start a thread to spin nodeB
		std::thread th([&] { exec_b->spin(); });

		// Publish messages
		for (int i = 0; i < 5; ++i) {
			pub->publish(StringMsg{ "Hello " + std::to_string(i) });
			std::this_thread::sleep_for(std::chrono::milliseconds(50));		}

		// Stop
		exec_b->stop();
		th.join();

		// Check sub_count
		if (sub_count.load() == 5) {
			std::cout << "[OK] NodeB received all 5 messages.\n";
		}
		else {
			std::cout << "[FAIL] NodeB only got " << sub_count.load() << " messages.\n";
		}

		// Leaving this scope destroys Node/Publisher/Subscriber
		// Topic without references will be removed from Domain
	}

	// Domain still alive here but its Topic should be removed
	std::cout << "[INFO] Left the scope, Node/Publisher/Subscriber destroyed.\n";
}

/**
 * @brief Test multiple subscribers on the same topic
 *        Another topic on a different node should not interfere
 */
static void testMultiSubscriber()
{
	using namespace lux::communication::intraprocess;

	std::cout << "\n=== testMultiSubscriber ===\n";
	auto domain = std::make_shared<Domain>(1);
	auto node_a = std::make_shared<Node>("NodeA", domain);
	auto node_b = std::make_shared<Node>("NodeB", domain);

	// NodeA: Publisher1 -> topic "chat"
	auto pub_chat = node_a->createPublisher<StringMsg>("chat");

	// NodeA: Publisher2 -> topic "data"
	auto pub_data = node_a->createPublisher<ComplexMsg>("data");

	// NodeB: subscriber1 -> topic "chat"
	std::atomic<int> chat_count_1{ 0 };
	auto sub_chat_1 = node_b->createSubscriber<StringMsg>("chat",
		[&](const StringMsg& msg) {
			chat_count_1++;
			std::cout << "[subChat1] got text: " << msg.text << "\n";
		});

	// NodeB: subscriber2 -> topic "chat" (second subscriber)
	std::atomic<int> chat_count_2{ 0 };
	auto sub_chat_2 = node_b->createSubscriber<StringMsg>("chat",
		[&](const StringMsg& msg) {
			chat_count_2++;
			std::cout << "[subChat2] also got text: " << msg.text << "\n";
		}
	);

	// NodeB: subscriberData -> topic "data"
	std::atomic<int> data_count{ 0 };
	auto sub_data = node_b->createSubscriber<ComplexMsg>("data",
		[&](const ComplexMsg& m) {
			data_count++;
			std::cout << "[subData] got size=" << m.data.size()
				<< " id=" << m.id << "\n";
		}
	);

	// Executor for nodeB
	auto exec_b = std::make_shared<lux::communication::SingleThreadedExecutor>();
	exec_b->addNode(node_b);
	std::thread spin_th([&] { exec_b->spin(); });

	// Publish
	pub_chat->publish(StringMsg{ "Hello 1" });
	pub_chat->publish(StringMsg{ "Hello 2" });
	pub_data->publish(ComplexMsg{ {1,2,3,4}, 99 });
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	// Stop
	exec_b->stop();
	spin_th.join();

	// Check
	if (chat_count_1.load() == 2 && chat_count_2.load() == 2) {
		std::cout << "[OK] both subChat1 & subChat2 got 2 messages.\n";
	}
	else {
		std::cout << "[FAIL] chat_count_1=" << chat_count_1.load()
			<< " chat_count_2=" << chat_count_2.load() << "\n";
	}
	if (data_count.load() == 1) {
		std::cout << "[OK] subData got 1 data message.\n";
	}
	else {
		std::cout << "[FAIL] subData didn't get expected data message.\n";
	}
}

/**
 * @brief Simple zero-copy test: check whether the object address stays the same
 * (In real projects you would also examine RcBuffer reference counts.)
 */
static void testZeroCopyCheck()
{
	using namespace lux::communication::intraprocess;

	std::cout << "\n=== testZeroCopyCheck ===\n";
	auto domain = std::make_shared<Domain>(1);
	auto node = std::make_shared<Node>("SingleNode", domain);

	// Record the address of the last received message
	std::atomic<const void*> last_ptr{ nullptr };

	// Single subscriber
	auto sub = node->createSubscriber<StringMsg>("nocopy", [&](const StringMsg& msg) {
		last_ptr.store(static_cast<const void*>(&msg), std::memory_order_relaxed);
		std::cout << "[Subscriber] address=" << &msg
			<< " text=" << msg.text << "\n";
		});

	// Publisher
	auto pub = node->createPublisher<StringMsg>("nocopy");

	// Executor
	auto exec = std::make_shared<lux::communication::SingleThreadedExecutor>();
	exec->addNode(node);

	std::thread th([&] { exec->spin(); });

	// Publish one message
	pub->publish(StringMsg{ "CheckZeroCopy" });
	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	exec->stop();
	th.join();

	if (last_ptr.load() != nullptr) {
		std::cout << "[INFO] Received message address = " << last_ptr.load() << "\n"
			<< "This suggests we didn't do a deep copy.\n"
			<< "But truly zero-copy depends on RcBuffer usage.\n";
	}
	else {
		std::cout << "[FAIL] No message address recorded.\n";
	}
}


// =============== Performance tests ===============

/**
 * @brief Throughput test with a single publisher and subscriber
 *        Sends N messages and measures time/throughput
 */
static void testPerformanceSinglePubSub(int message_count = 100000)
{
	using namespace lux::communication::intraprocess;

	std::cout << "\n=== testPerformanceSinglePubSub ===\n";

	auto domain = std::make_shared<Domain>(1);
	auto node = std::make_shared<Node>("PerfNode", domain);

	// Count received messages
	std::atomic<int> recv_count{ 0 };

	// Subscriber
	auto sub = node->createSubscriber<StringMsg>("perf_topic", [&](const StringMsg& msg) {
		recv_count++;
		}
	);

	// Publisher
	auto pub = node->createPublisher<StringMsg>("perf_topic");

	// Executor
	auto exec = std::make_shared<lux::communication::SingleThreadedExecutor>();
	exec->addNode(node);

	std::thread spin_th([&] {
		exec->spin();
		});

	// start time
	auto t1 = std::chrono::steady_clock::now();
	// Publish N messages
	for (int i = 0; i < message_count; ++i) {
		pub->emplacePublish("some text");
	}

	// Wait for all messages
	while (recv_count.load() < message_count) {
		std::this_thread::sleep_for(std::chrono::microseconds(50));
	}
	auto t2 = std::chrono::steady_clock::now();

	// Stop
	exec->stop();
	spin_th.join();
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
	double seconds = elapsed / 1000.0;
	double rate = message_count / (seconds ? seconds : 1e-9);

	std::cout << "[Perf] " << message_count << " messages took "
		<< elapsed << " ms => " << rate << " msg/s\n";
}

/**
 * @brief Multi-subscriber performance test: 1 publisher and N subscribers
 *        Each subscriber processes all messages
 */
static void testPerformanceMultiSubscriber(int subscriber_count = 5, int message_count = 50000)
{
	using namespace lux::communication::intraprocess;

	std::cout << "\n=== testPerformanceMultiSubscriber ===\n";

	auto domain = std::make_shared<Domain>(1);
	auto node = std::make_shared<Node>("PerfMultiSub", domain);
	// Each subscriber keeps its own count
	std::vector<std::atomic<int>> counters(subscriber_count);
	for (auto& c : counters) {
		c.store(0);
	}

	// Create N subscribers
	std::vector<std::shared_ptr<Subscriber<StringMsg>>> subs;
	subs.reserve(subscriber_count);
	for (int i = 0; i < subscriber_count; ++i) {
		auto sub = node->createSubscriber<StringMsg>("multi_perf",
			[&, i](const StringMsg& msg) {
				counters[i].fetch_add(1, std::memory_order_relaxed);
			}
		);
		subs.push_back(sub);
	}

	// Publisher
	auto pub = node->createPublisher<StringMsg>("multi_perf");

	// Executor
	auto exec = std::make_shared<lux::communication::SingleThreadedExecutor>();
	exec->addNode(node);
	std::thread spin_th([&] { exec->spin(); });

	// Start timing
	auto t1 = std::chrono::steady_clock::now();
	// Publish messageCount messages
	for (int i = 0; i < message_count; ++i) {
		pub->emplacePublish("hello");
	}

	// Wait until all subscribers receive all messages
	auto total_target = subscriber_count * message_count;
	auto check_all_received = [&]() {
		long sum = 0;
		for (auto& c : counters) {
			sum += c.load(std::memory_order_relaxed);
		}
		return (sum == total_target);
		};

	while (!check_all_received()) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	auto t2 = std::chrono::steady_clock::now();

	exec->stop();
	spin_th.join();
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
	double seconds = elapsed / 1000.0;
	double rate = message_count / (seconds ? seconds : 1e-9);
	std::cout << "[Perf MultiSub] " << subscriber_count << " subs x " << message_count
		<< " msgs => total " << total_target << " deliveries.\n"
		<< "Time = " << elapsed << " ms => ~"
		<< rate << " msg/s (per subscriber). \n";
}

struct TimeStampedMsg
{
	std::chrono::steady_clock::time_point send_time; // send time
	std::string payload; // optional data
};

/**
 * @brief End-to-end latency test with a single publisher and subscriber
 *
 * The publisher sends N timestamped messages and the subscriber measures
 * the difference between receive time and send time to calculate min/avg/max.
 */
static void testLatencySinglePubSub(int message_count = 1000)
{
	using namespace lux::communication::intraprocess;

	std::cout << "\n=== testLatencySinglePubSub ===\n";

	auto domain = std::make_shared<Domain>(1);
	auto node = std::make_shared<Node>("LatencyNode", domain);
	// 1) Preallocate array to avoid push_back
	std::vector<long long> latencies(message_count);

	// 2) Use atomic index to fill latencies[i]
	std::atomic<int> write_index{ 0 };

	// 3) Subscriber callback
	auto sub = node->createSubscriber<TimeStampedMsg>("latency_topic",
		[&](const TimeStampedMsg& msg)
		{
			auto now = std::chrono::steady_clock::now();
			auto delta = std::chrono::duration_cast<std::chrono::microseconds>(now - msg.send_time).count();

			// Atomically get a write slot
			int i = write_index.fetch_add(1, std::memory_order_relaxed);
			if (i < message_count) {
				latencies[i] = delta;
			}
		}
	);

	// 4) Create Publisher
	auto pub = node->createPublisher<TimeStampedMsg>("latency_topic");

	// 5) Start Executor
	auto exec = std::make_shared<lux::communication::SingleThreadedExecutor>();
	exec->addNode(node);

	std::thread spin_th([&] {
		exec->spin();
		});
	// 6) Publish N messages
	for (int i = 0; i < message_count; ++i)
	{
		TimeStampedMsg msg;
		msg.send_time = std::chrono::steady_clock::now();
		msg.payload = std::format("test {}", i); // C++20 format

		pub->publish(std::move(msg));

		// std::this_thread::sleep_for(std::chrono::microseconds(100));
	}

	// 7) Wait until all messages are consumed
	while (write_index.load(std::memory_order_acquire) < message_count)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	// 8) Compute latency statistics
	exec->stop();
	spin_th.join();
	long long sum = 0;
	long long min_v = std::numeric_limits<long long>::max();
	long long max_v = 0;

	for (auto& d : latencies) {
		sum += d;
		if (d < min_v) min_v = d;
		if (d > max_v) max_v = d;	}
	double avg = (double)sum / (double)message_count;

	std::cout << "[LatencyTest-noLock] " << message_count << " msgs:\n"
		<< "    Min: " << min_v << " us\n"
		<< "    Max: " << max_v << " us\n"
		<< "    Avg: " << avg << " us\n";
}

static void testMultiThreadedExecutorBasic(int thread_count = 4, int message_count = 50000)
{
	using namespace lux::communication::intraprocess;

	std::cout << "\n=== testMultiThreadedExecutorBasic ===\n";

	// 1. Create Domain and Node
	auto domain = std::make_shared<Domain>(1);
	auto node = std::make_shared<Node>("MultiThreadNode", domain);

	// 2. Create Publisher
	auto pub = node->createPublisher<StringMsg>("multi_thread_topic");

	// 3. Create multiple subscribers (default callback group)
	//    Each subscriber counts its own messages
	const int sub_count = 3;
	std::vector<std::atomic<int>> counters(sub_count);
	for (auto& c : counters) c.store(0);

	std::vector<std::shared_ptr<Subscriber<StringMsg>>> subs;
	subs.reserve(sub_count);
	for (int i = 0; i < sub_count; ++i)
	{
		auto s = node->createSubscriber<StringMsg>("multi_thread_topic",
			[&, idx = i](const StringMsg& msg) {
				// Simulate some workload
				// std::this_thread::sleep_for(std::chrono::microseconds(50));
				counters[idx].fetch_add(1, std::memory_order_relaxed);
			}
		);
		subs.push_back(s);
	}
	// 4. Create lux::communication::MultiThreadedExecutor and add Node
	auto exec = std::make_shared<lux::communication::MultiThreadedExecutor>(thread_count);
	exec->addNode(node);

	// 5. Start spinning (threads process concurrently)
	std::thread spin_thread([&] {
		exec->spin();
		});

	// 6. Publish some messages
	auto t1 = std::chrono::steady_clock::now();
	for (int i = 0; i < message_count; ++i) {
		pub->publish(StringMsg{ "Hello from multi-thread" });
	}
	// 7. Wait until all subscribers received their messages
	//    Simple approach: wait until the sum of counters equals sub_count*message_count
	const int total_needed = sub_count * message_count;
	while (true)
	{
		int sum = 0;
		for (auto& c : counters) {
			sum += c.load(std::memory_order_relaxed);
		}
		if (sum >= total_needed) {
			break;
		}
		// std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	auto t2 = std::chrono::steady_clock::now();

	exec->stop();
	spin_thread.join();

	// 8. Output results
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
	std::cout << "[MultiThreadedExecutorBasic] " << thread_count << " threads, "
		<< sub_count << " subscribers x " << message_count << " msgs => total "
		<< total_needed << " deliveries.\n"
		<< "Time = " << elapsed << " ms\n";
}

/**
 * @brief Test multi-threaded executor with different callback groups
 *        - Same node and topic
 *        - Two groups: groupR (reentrant) and groupM (mutually exclusive)
 *        - Each group has two subscribers
 *        - Publish many messages and observe concurrency
 */
static void testMultiThreadedExecutorWithCallbackGroups(int thread_count = 4, int message_count = 10000)
{
	using namespace lux::communication::intraprocess;

	std::cout << "\n=== testMultiThreadedExecutorWithCallbackGroups ===\n";

	auto domain = std::make_shared<Domain>(1);
	auto node   = std::make_shared<Node>("NodeWithGroups", domain);

	// 1) Create two callback groups: Reentrant and MutuallyExclusive
	auto group_r = node->createCallbackGroup(CallbackGroupType::Reentrant);
	auto group_m = node->createCallbackGroup(CallbackGroupType::MutuallyExclusive);

	// 2) Prepare concurrency tracking data
	//    - active and peak counts for each group
	struct GroupStats {
		std::atomic<int> active_count{ 0 };  // currently active callbacks
		std::atomic<int> peak_count{ 0 };    // record highest concurrency seen
	};
	GroupStats stats_r, stats_m;

	// Helper: increment active_count at start, decrement at end, update peak
	auto makeCallback = [&](GroupStats& st, std::string name) {
		return [&, name](const StringMsg& msg) {
			int val = st.active_count.fetch_add(1, std::memory_order_acq_rel) + 1;
			// update peak
			int old_peak = st.peak_count.load(std::memory_order_relaxed);
			while (val > old_peak && !st.peak_count.compare_exchange_weak(old_peak, val)) {
				// retry
			}

			// simulate processing delay
			std::this_thread::sleep_for(std::chrono::microseconds(200));

			// done
			st.active_count.fetch_sub(1, std::memory_order_acq_rel);
			};
		};

	// 3) Create two subscribers in the Reentrant group
	auto sub_r1 = node->createSubscriber<StringMsg>(
		"group_topic", makeCallback(stats_r, "R1"), group_r
	);
	auto sub_r2 = node->createSubscriber<StringMsg>(
		"group_topic", makeCallback(stats_r, "R2"), group_r
	);

	// 4) Create two subscribers in the MutuallyExclusive group
	auto sub_m1 = node->createSubscriber<StringMsg>(
		"group_topic", makeCallback(stats_m, "M1"), group_m
	);
	auto sub_m2 = node->createSubscriber<StringMsg>(
		"group_topic", makeCallback(stats_m, "M2"), group_m
	);

	// 5) Create Publisher
	auto pub = node->createPublisher<StringMsg>("group_topic");

	// 6) Create lux::communication::MultiThreadedExecutor and add both groups
	//    Note: addNode(node) would also add the default group
	//    Here we manually add group_r and group_m for clarity
	auto exec = std::make_shared<lux::communication::MultiThreadedExecutor>(4);
	exec->addNode(node); // add all callback groups of the node

	// 7) Start spinning
	std::thread spin_th([&] {
			exec->spin();
		}
	);

	// 8) Publish some messages
	for (int i = 0; i < 10; ++i) {
		pub->emplacePublish(std::format("Msg # {}", i));
	}

	// 9) Wait a bit for callbacks to complete
	std::this_thread::sleep_for(std::chrono::milliseconds(2000));

	exec->stop();
	spin_th.join();

	// 10) Check peak concurrency of both groups
	int peak_r = stats_r.peak_count.load(std::memory_order_relaxed);
	int peak_m = stats_m.peak_count.load(std::memory_order_relaxed);
	//    Reentrant group should show peak > 1 with enough threads
	//    MutuallyExclusive group should stay at 1
	std::cout << "[GroupTest] thread_count=" << thread_count << " total_msgs=" << message_count << "\n"
		<< "    ReentrantGroup peak concurrency = " << peak_r << "\n"
		<< "    MutuallyExclusiveGroup peak concurrency = " << peak_m << "\n";

	if (peak_r > 1) {
		std::cout << "    [OK] Reentrant group did run callbacks in parallel.\n";
	}
	else {
		std::cout << "    [WARN] Reentrant group didn't show concurrency.\n";
	}

	if (peak_m <= 1) {
		std::cout << "    [OK] MutuallyExclusive group is strictly serialized.\n";
	}
	else {
		std::cout << "    [FAIL] MutuallyExclusive group concurrency > 1!\n";
	}
}

// ================= Additional tests =================

struct LargeMsg
{
	std::vector<uint8_t> data;
};

// Performance test: large message throughput
void testPerformanceLargeMessage(int message_count = 1000, size_t size = 1024 * 1024)
{
	using namespace lux::communication::intraprocess;

	std::cout << "\n=== testPerformanceLargeMessage ===\n";

	auto domain = std::make_shared<Domain>(1);
	auto node = std::make_shared<Node>("LargeMsgNode", domain);

	std::atomic<int> recv{ 0 };

	auto sub = node->createSubscriber<LargeMsg>("big_topic", [&](const LargeMsg& m) {
		(void)m;
		recv.fetch_add(1, std::memory_order_relaxed);
		});

	auto pub = node->createPublisher<LargeMsg>("big_topic");

	auto exec = std::make_shared<lux::communication::SingleThreadedExecutor>();
	exec->addNode(node);

	std::thread th([&] { exec->spin(); });

	LargeMsg msg; msg.data.resize(size);
	auto t1 = std::chrono::steady_clock::now();
	for (int i = 0; i < message_count; ++i) {
		pub->emplacePublish(msg);
	}

	while (recv.load(std::memory_order_acquire) < message_count) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	auto t2 = std::chrono::steady_clock::now();
	exec->stop();
	th.join();

	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
	double rate = message_count / (ms / 1000.0);

	std::cout << "[LargeMsgPerf] " << message_count << " msgs of " << size / 1024 << "KB took "
		<< ms << " ms => " << rate << " msg/s\n";
}

// Thread lifecycle safety test
void testThreadLifecycleSafety()
{
	using namespace lux::communication::intraprocess;

	std::cout << "\n=== testThreadLifecycleSafety ===\n";

	auto domain = std::make_shared<Domain>(1);
	auto node = std::make_shared<Node>("LifeNode", domain);

	auto exec = std::make_shared<lux::communication::MultiThreadedExecutor>(2);
	exec->addNode(node);

	std::atomic<bool> running{ true };
	std::atomic<int>  count{ 0 };

	std::shared_ptr<Subscriber<StringMsg>> sub;
	sub = node->createSubscriber<StringMsg>("life_topic", [&](const StringMsg& m) {
		(void)m; count.fetch_add(1, std::memory_order_relaxed); });

	auto pub = node->createPublisher<StringMsg>("life_topic");

	std::thread spin_th([&] { exec->spin(); });
	std::thread pub_th([&] {
		while (running.load()) {
			pub->publish(StringMsg{ "hi" });
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		});

	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	sub.reset(); // destroy subscriber
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	sub = node->createSubscriber<StringMsg>("life_topic", [&](const StringMsg& m) {
		(void)m; count.fetch_add(1, std::memory_order_relaxed); });

	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	running.store(false);

	exec->stop();
	spin_th.join();
	pub_th.join();

	std::cout << "[LifecycleTest] received " << count.load() << " messages without crash.\n";
}

// Memory leak detection
struct CountingMsg
{
	static std::atomic<int> live_count;
	int id{ 0 };
	CountingMsg() { live_count.fetch_add(1, std::memory_order_relaxed); }
	CountingMsg(const CountingMsg&) { live_count.fetch_add(1, std::memory_order_relaxed); }
	~CountingMsg() { live_count.fetch_sub(1, std::memory_order_relaxed); }
};

std::atomic<int> CountingMsg::live_count{ 0 };

static void testMemoryLeakCheck(int message_count = 1000)
{
	using namespace lux::communication::intraprocess;

	std::cout << "\n=== testMemoryLeakCheck ===\n";

	auto domain = std::make_shared<Domain>(1);
	auto node = std::make_shared<Node>("MemNode", domain);

	std::atomic<int> recv{ 0 };
	auto sub = node->createSubscriber<CountingMsg>("mem_topic", [&](const CountingMsg& m) {
		(void)m; recv.fetch_add(1, std::memory_order_relaxed); });

	auto pub = node->createPublisher<CountingMsg>("mem_topic");

	auto exec = std::make_shared<lux::communication::SingleThreadedExecutor>();
	exec->addNode(node);

	std::thread spin_th([&] { exec->spin(); });
	for (int i = 0; i < message_count; ++i) {
		CountingMsg m; m.id = i;
		pub->publish(m);
	}

	while (recv.load(std::memory_order_acquire) < message_count) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	exec->stop();
	spin_th.join();
	sub.reset();
	pub.reset();
	node.reset();
	domain.reset();

	if (CountingMsg::live_count.load() == 0) {
		std::cout << "[OK] no message leak detected.\n";
	}
	else {
		std::cout << "[FAIL] leak count=" << CountingMsg::live_count.load() << "\n";
	}
}

// =============== Main test entry ===============

int main()
{
	// 1. Functional tests
	testDomainIsolation();
	testSingleDomainMultiNode();
	testMultiSubscriber();
	testZeroCopyCheck();

	// 2. Performance tests
	testPerformanceSinglePubSub(100000);       // 100k messages
	testPerformanceMultiSubscriber(5, 50000);  // 5 subscribers, 50k messages

	testLatencySinglePubSub(1e5); // example run
	testMultiThreadedExecutorBasic(/*thread_count*/ 4, /*message_count*/ 50000);
	testMultiThreadedExecutorWithCallbackGroups(/*thread_count*/ 4, /*message_count*/ 10000);

	testPerformanceLargeMessage();
	testThreadLifecycleSafety();
	testMemoryLeakCheck();

	std::cout << "\nAll tests done.\n";
	return 0;
}