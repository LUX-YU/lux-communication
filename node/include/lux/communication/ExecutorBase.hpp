#pragma once

#include <vector>
#include <mutex>
#include <condition_variable>
#include <semaphore>
#include <climits>
#include <atomic>
#include <memory>

#include <lux/communication/CallbackGroupBase.hpp>
#include <lux/communication/Queue.hpp>
#include <lux/communication/visibility.h>
#include <lux/cxx/container/SparseSet.hpp>

// ── Spin-loop pause hint (saves power / avoids pipeline stall) ──
#if defined(_MSC_VER)
#  include <intrin.h>
#endif

namespace lux::communication {

namespace detail {
	inline void cpu_pause() noexcept {
#if defined(_MSC_VER) || defined(__x86_64__) || defined(__i386__)
		_mm_pause();
#elif defined(__aarch64__)
		__asm__ volatile("yield");
#endif
	}
} // namespace detail

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
			// Phase 1: user-space spin — avoids kernel semaphore syscalls.
			spinning_in_userspace_.store(true, std::memory_order_seq_cst);
			for (uint32_t i = 0; i < kSpinIterations; ++i) {
				SubscriberBase* sub = nullptr;
				if (ready_queue_.try_dequeue(sub)) {
					spinning_in_userspace_.store(false, std::memory_order_seq_cst);
					return sub;
				}
				detail::cpu_pause();
			}
			spinning_in_userspace_.store(false, std::memory_order_seq_cst);

			// Final drain: catch items enqueued while clearing the flag.
			{
				SubscriberBase* sub = nullptr;
				if (ready_queue_.try_dequeue(sub)) return sub;
			}

			// Phase 2: kernel block.
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
			ready_queue_.enqueue(sub);
			// If executor is spin-polling in user-space, skip the kernel semaphore call.
			// seq_cst ordering guarantees correct visibility (see waitOneReady).
			if (!spinning_in_userspace_.load(std::memory_order_seq_cst)) {
				ready_sem_.release();
			}
		}

	protected:
		using NodeList	 = lux::cxx::AutoSparseSet<NodeBase*>;
		using ReadyQueue = moodycamel::ConcurrentQueue<SubscriberBase*>;

		NodeList							nodes_;
		std::mutex							nodes_mutex_;
		ReadyQueue							ready_queue_;
		std::counting_semaphore<INT_MAX>	ready_sem_{ 0 };

		/// True while waitOneReady() is in its user-space spin loop.
		/// enqueueReady() reads this to decide whether to skip sem.release().
		std::atomic<bool>					spinning_in_userspace_{ false };

		/// Number of spin iterations before falling back to kernel semaphore.
		/// ~4096 × _mm_pause (~40ns each on Skylake+) ≈ 160μs max spin time.
		static constexpr uint32_t kSpinIterations = 4096;

	protected:
		void			waitCondition();
		void			notifyCondition();
		virtual bool	checkRunnable();

		std::atomic<bool>						spinning_{ false };
		std::mutex								cv_mutex_;
		std::condition_variable					cv_;
	};

} // namespace lux::communication
