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
#include <lux/cxx/container/SparseSet.hpp>

#if __has_include(<moodycamel/concurrentqueue.h>)
#   include <moodycamel/concurrentqueue.h>
#elif __has_include(<concurrentqueue/moodycamel/concurrentqueue.h>)
#   include <concurrentqueue/moodycamel/concurrentqueue.h>
#elif __has_include(<concurrentqueue/concurrentqueue.h>)
#   include <concurrentqueue/concurrentqueue.h>
#endif

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
		virtual void handleSubscriber(SubscriberBase* sub) = 0;

		virtual void spin()
		{
			if (!spinning_) {
				spinning_ = true;
			}

			while (spinning_)
			{
				auto sub = waitOneReady();
				if (!spinning_)
					break;
				if (sub)
				{
					handleSubscriber(sub);
				}
			}
		}
		
		virtual void stop()
		{
			if (spinning_.exchange(false))
			{
				ready_sem_.release();
				notifyCondition();
			}
		}

		void wakeup()
		{
			ready_sem_.release();
		}

		SubscriberBase* waitOneReady()
		{
			ready_sem_.acquire();
			SubscriberBase* sub = nullptr;
			ready_queue_.try_dequeue(sub);
			return sub;
		}

		SubscriberBase* waitOneReadyTimeout(std::chrono::milliseconds timeout)
		{
			if (ready_sem_.try_acquire_for(timeout)) {
				SubscriberBase* sub = nullptr;
				ready_queue_.try_dequeue(sub);
				return sub;
			}
			return nullptr;
		}

		void enqueueReady(SubscriberBase* sub)
		{
			ready_queue_.enqueue(std::move(sub));
			ready_sem_.release();
		}

	protected:
		using NodeList	 = lux::cxx::AutoSparseSet<NodeBase*>;
		using ReadyQueue = moodycamel::ConcurrentQueue<SubscriberBase*>;

		NodeList							nodes_;
		std::mutex							nodes_mutex_;
		ReadyQueue							ready_queue_;
		std::counting_semaphore<INT_MAX>	ready_sem_{ 0 };

	protected:
		void			waitCondition();
		void			notifyCondition();
		virtual bool	checkRunnable();

		std::atomic<bool>						spinning_{ false };
		std::mutex								cv_mutex_;
		std::condition_variable					cv_;
	};

} // namespace lux::communication
