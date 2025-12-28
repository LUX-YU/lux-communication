#pragma once

#include <lux/communication/ExecutorBase.hpp>
#include <lux/communication/ReorderBuffer.hpp>

namespace lux::communication {

	/**
	 * @brief Executor that processes callbacks strictly by global sequence number.
	 *        Ensures that callbacks are executed in the exact order messages were published,
	 *        regardless of which subscriber receives them.
	 *        
	 *        Key optimizations:
	 *        1. Ring buffer with O(1) insert/pop for reordering
	 *        2. Bounded drain (drainExecSome) - only drain a small batch per subscriber
	 *        3. "Execute first, drain on gap" strategy - prioritize execution over draining
	 *        4. Round-robin subscriber scheduling via re-notify
	 */
	class LUX_COMMUNICATION_PUBLIC SeqOrderedExecutor : public ExecutorBase
	{
	public:
		SeqOrderedExecutor() = default;
		~SeqOrderedExecutor() override;

		bool spinSome() override;
		void spin() override;
		void stop() override;

		/**
		 * @brief Get diagnostic statistics for performance monitoring.
		 */
		const ReorderBufferStats& stats() const { return buffer_.stats(); }

		/**
		 * @brief Reset diagnostic statistics.
		 */
		void reset_stats() { buffer_.reset_stats(); }

		/**
		 * @brief Get current pending size in reorder buffer.
		 */
		size_t pending_size() const { return buffer_.pending_size(); }

		/**
		 * @brief Get current fallback hashmap size.
		 */
		size_t fallback_size() const { return buffer_.fallback_size(); }

	protected:
		bool checkRunnable() override;
		void handleSubscriber(SubscriberBase* sub) override;

	private:
		/**
		 * @brief Try to execute as many consecutive entries as possible.
		 * @return Number of entries executed
		 */
		size_t executeConsecutive();

		/**
		 * @brief Drain one subscriber with bounded count.
		 * @return true if any entries were drained
		 */
		bool drainOneSubscriber(SubscriberBase* sub);

	private:
		// Max entries to drain per subscriber (bounded drain for round-robin)
		static constexpr size_t kMaxDrainPerSubscriber = 256;

		ReorderBuffer buffer_;         // Ring buffer with O(1) reordering + fallback
		
		// Thread-local buffer for draining (avoid allocation per call)
		// Note: Using inline buffer since this is single-threaded executor
		std::vector<ExecEntry> drain_buffer_;
	};

} // namespace lux::communication
