#pragma once

#include <queue>
#include <chrono>
#include <lux/communication/ExecutorBase.hpp>
#include <lux/communication/TimeExecEntry.hpp>

namespace lux::communication {

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
		uint64_t                            max_timestamp_seen_{ 0 };
	};

} // namespace lux::communication
