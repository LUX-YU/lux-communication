#pragma once

#include <lux/communication/ExecutorBase.hpp>
#include <lux/communication/ReorderBuffer.hpp>

namespace lux::communication
{
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

		void spinSome() override;
		void spin() override;
		void stop() override;

		/**
		 * @brief Get diagnostic statistics for performance monitoring.
		 */
		const ReorderBufferStats &stats() const { return buffer_.stats(); }

		/**
		 * @brief Reset diagnostic statistics.
		 */
		void reset_stats() { buffer_.reset_stats(); }

		/**
		 * @brief Get current pending size in reorder buffer.
		 */
		size_t pending_size() const { return buffer_.pending_size(); }

	protected:
		bool checkRunnable() override;
		void handleSubscriber(SubscriberBase *sub) override;

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
		bool drainOneSubscriber(SubscriberBase *sub);

		/**
		 * @brief Collect unique ready subscribers from ready_queue_ into ready_batch_.
		 *        Deduplicates to prevent the same subscriber from being drained
		 *        multiple times per round (caused by drainExecSome re-notification).
		 * @return Number of unique subscribers collected
		 */
		size_t collectUniqueReady();

		/**
		 * @brief Interleaved drain + execute loop over the collected batch.
		 *        Drains a small batch from each subscriber, then executes consecutive
		 *        entries.  Repeats until all subscriber queues are empty.
		 */
		void drainAndExecute(size_t batch_count);

	private:
		// Default items to drain per subscriber per round.
		static constexpr size_t kMaxDrainPerSubscriber = 32;

		// Adaptive throttle: when a subscriber's max drained seq exceeds
		// next_seq by this much, throttle its drain to 1 item so the
		// gap-filling subscriber catches up.  Keeps the reorder window
		// bounded at roughly kMaxWindow + one batch span.
		static constexpr uint64_t kMaxWindow = 128;

		// Max unique subscribers to collect per drain phase.
		static constexpr size_t kMaxReadyBatch = 64;

		ReorderBuffer buffer_; // Ring buffer with O(1) reordering + fallback

		// Reusable buffer for drainOneSubscriber (avoid allocation per call)
		std::vector<ExecEntry> drain_buffer_;

		// Batch buffer: collect unique ready subscribers before draining
		SubscriberBase* ready_batch_[kMaxReadyBatch]{};
	};

} // namespace lux::communication
