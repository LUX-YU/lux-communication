#pragma once

#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <thread>
#include <unordered_set>
#include <queue>
#include <chrono>

#include <lux/communication/visibility.h>
#include <lux/communication/CallbackGroup.hpp>
#include <lux/communication/builtin_msgs/common_msgs/timestamp.st.h>
#include <lux/cxx/concurrent/ThreadPool.hpp>

namespace lux::communication {

	class CallbackGroup;
	using CallbackGroupSptr = std::shared_ptr<CallbackGroup>;
	using CallbackGroupWptr = std::weak_ptr<CallbackGroup>;

	class LUX_COMMUNICATION_PUBLIC Executor : public std::enable_shared_from_this<Executor>
	{
	public:
		Executor();
		virtual ~Executor();

		virtual void addNode(std::shared_ptr<NodeBase> node);
		virtual void removeNode(std::shared_ptr<NodeBase> node);

		virtual void spinSome() = 0;
		virtual void spin();
		virtual void stop();
		virtual void wakeup();

	protected:
		std::vector<CallbackGroupWptr>			callback_groups_;
		std::mutex								callback_groups_mutex_;

	protected:
		void waitCondition();
		void notifyCondition();
		virtual bool checkRunnable();

		std::atomic<bool>						running_;
		std::mutex								cv_mutex_;
		std::condition_variable					cv_;
	};

	class LUX_COMMUNICATION_PUBLIC SingleThreadedExecutor : public Executor
	{
	public:
		SingleThreadedExecutor() = default;
		~SingleThreadedExecutor() override;

		void spinSome() override;
	};

	class LUX_COMMUNICATION_PUBLIC MultiThreadedExecutor : public Executor
	{
	public:
		explicit MultiThreadedExecutor(size_t threadNum = 2);
		~MultiThreadedExecutor() override;

		void spinSome() override;
		void stop() override;

	private:
		lux::cxx::ThreadPool thread_pool_;
	};

	class LUX_COMMUNICATION_PUBLIC TimeOrderedExecutor : public Executor
	{
	public:
		explicit TimeOrderedExecutor(std::chrono::nanoseconds time_offset = std::chrono::nanoseconds{ 0 });
		~TimeOrderedExecutor() override;

		void addNode(std::shared_ptr<NodeBase> node) override;

		void spinSome() override;
		void spin() override;
		void stop() override;

		void setTimeOffset(std::chrono::nanoseconds offset);
		std::chrono::nanoseconds getTimeOffset() const;

	protected:
		bool checkRunnable() override;

	private:
		void fetchReadyEntries();
		void processReadyEntries();
		void doWait();

	private:
		std::priority_queue<TimeExecEntry>  buffer_;
		std::chrono::nanoseconds            time_offset_;
	};

} // namespace lux::communication
