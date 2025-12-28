#pragma once

#include <lux/communication/ExecutorBase.hpp>

namespace lux::communication {

	class LUX_COMMUNICATION_PUBLIC SingleThreadedExecutor : public ExecutorBase
	{
	public:
		SingleThreadedExecutor() = default;
		~SingleThreadedExecutor() override;

		bool spinSome() override;
		void handleSubscriber(SubscriberBase* sub) override;
	};

} // namespace lux::communication
