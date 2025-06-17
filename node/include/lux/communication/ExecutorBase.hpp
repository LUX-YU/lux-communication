#pragma once

#include <vector>
#include <mutex>
#include <condition_variable>
#include <semaphore>
#include <climits>
#include <atomic>
#include <memory>
#include <thread>
#include <queue>
#include <chrono>

#include <lux/communication/TimeExecEntry.hpp>
#include <lux/communication/CallbackGroupBase.hpp>
#include <lux/communication/Queue.hpp>
#include <lux/communication/builtin_msgs/common_msgs/timestamp.st.h>
#include <lux/communication/visibility.h>
#include <lux/cxx/concurrent/ThreadPool.hpp>

namespace lux::communication {

	class NodeBase;
	class CallbackGroupBase;
	class SubscriberBase;

	class LUX_COMMUNICATION_PUBLIC ExecutorBase
	{
	public:
		ExecutorBase();
		virtual ~ExecutorBase();

		virtual void addNode(NodeBase* node);
		virtual void removeNode(NodeBase* node);

		virtual void spinSome() = 0;
		virtual void spin();
		virtual void stop();
		virtual void wakeup();

		SubscriberBase* waitOneReady();
		void enqueueReady(SubscriberBase* sub);
		virtual void handleSubscriber(SubscriberBase* sub) = 0;

	protected:
		using NodeList = lux::cxx::AutoSparseSet<NodeBase*>;

		NodeList		nodes_;
		std::mutex		nodes_mutex_;

		using ReadyQueue = moodycamel::ConcurrentQueue<SubscriberBase*>;

		ReadyQueue							ready_queue_;
		std::counting_semaphore<INT_MAX>	ready_sem_{ 0 };

	protected:
		void			waitCondition();
		void			notifyCondition();
		virtual bool	checkRunnable();

		std::atomic<bool>						running_;
		std::mutex								cv_mutex_;
		std::condition_variable					cv_;
	};

	class LUX_COMMUNICATION_PUBLIC SingleThreadedExecutor : public ExecutorBase
	{
	public:
		SingleThreadedExecutor() = default;
		~SingleThreadedExecutor() override;

		void spinSome() override;
		void handleSubscriber(SubscriberBase* sub) override;
	};

	class LUX_COMMUNICATION_PUBLIC MultiThreadedExecutor : public ExecutorBase
	{
	public:
		explicit MultiThreadedExecutor(size_t threadNum = 2);
		~MultiThreadedExecutor() override;

		void spinSome() override;
		void stop() override;
		void handleSubscriber(SubscriberBase* sub) override;

	private:
		lux::cxx::ThreadPool thread_pool_;
	};

	class LUX_COMMUNICATION_PUBLIC TimeOrderedExecutor : public ExecutorBase
	{
	public:
		explicit TimeOrderedExecutor(std::chrono::nanoseconds time_offset = std::chrono::nanoseconds{ 0 });
		~TimeOrderedExecutor() override;

		void spinSome() override;
		void spin() override;
		void stop() override;

		void setTimeOffset(std::chrono::nanoseconds offset);
		std::chrono::nanoseconds getTimeOffset() const;

	protected:
		bool checkRunnable() override;
		void handleSubscriber(SubscriberBase* sub) override;

	private:
		void processReadyEntries();
		void doWait();

	private:
		std::priority_queue<TimeExecEntry>  buffer_;
		std::chrono::nanoseconds            time_offset_;
	};

} // namespace lux::communication
