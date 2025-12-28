#pragma once

#include <lux/communication/ExecutorBase.hpp>
#include <lux/cxx/concurrent/ThreadPool.hpp>

namespace lux::communication {

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

} // namespace lux::communication
