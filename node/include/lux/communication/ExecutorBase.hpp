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

		virtual bool spinSome() = 0;
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
			SubscriberBase* sub;
			ready_queue_.try_dequeue(sub);
			return sub;
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

		std::atomic<bool>						spinning_;
		std::mutex								cv_mutex_;
		std::condition_variable					cv_;
	};

	class LUX_COMMUNICATION_PUBLIC SingleThreadedExecutor : public ExecutorBase
	{
	public:
		SingleThreadedExecutor() = default;
		~SingleThreadedExecutor() override;

		bool spinSome() override;
		void handleSubscriber(SubscriberBase* sub) override;
	};

	class LUX_COMMUNICATION_PUBLIC MultiThreadedExecutor : public ExecutorBase
	{
	public:
		explicit MultiThreadedExecutor(size_t threadNum = 2);
		~MultiThreadedExecutor() override;

		bool spinSome() override;
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

		bool spinSome() override;
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
