#pragma once

#include <lux/communication/transport/ShmRingBuffer.hpp>
#include <lux/communication/transport/CpuRelax.hpp>
#include <lux/communication/visibility.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

namespace lux::communication::transport
{
    // =====================================
    // CPU relax / spin hint
    // =====================================
    // ──── Notifier (Publisher side) ────
    class LUX_COMMUNICATION_PUBLIC ShmNotifier
    {
    public:
        /// Bind to a ring's NotifyBlock.
        explicit ShmNotifier(NotifyBlock *block);
        ~ShmNotifier();

        ShmNotifier(const ShmNotifier &) = delete;
        ShmNotifier &operator=(const ShmNotifier &) = delete;

        /// Wake a sleeping reader.
        ///   Linux:   futex(FUTEX_WAKE, &futex_word, 1)
        ///   Windows: SetEvent(hEvent)
        void wake();

    private:
        NotifyBlock *block_{nullptr};

#ifdef _WIN32
        void *hEvent_ = nullptr; // HANDLE
#endif
    };

    // ──── Waiter (Subscriber side) ────

    /// Platform-specific kernel wait (implemented in ShmNotifyPosix.cpp / ShmNotifyWin.cpp).
    /// Returns true if woken, false if timed-out.
    LUX_COMMUNICATION_PUBLIC bool kernelWait(
        NotifyBlock *block,
        uint32_t expected_futex_val,
        std::chrono::microseconds timeout
#ifdef _WIN32
        ,
        void *hEvent
#endif
    );

    /// Open / create the platform notification object for the reader side.
    /// Linux:  no-op, returns nullptr.
    /// Windows: OpenEventA or CreateEventA from block->event_name, returns HANDLE.
    LUX_COMMUNICATION_PUBLIC void *waiterOpen(NotifyBlock *block);

    /// Close the platform notification object.
    LUX_COMMUNICATION_PUBLIC void waiterClose(void *handle);

    class LUX_COMMUNICATION_PUBLIC ShmWaiter
    {
    public:
        /// Bind to a ring's NotifyBlock.
        explicit ShmWaiter(NotifyBlock *block);
        ~ShmWaiter();

        ShmWaiter(const ShmWaiter &) = delete;
        ShmWaiter &operator=(const ShmWaiter &) = delete;

        /// Three-phase adaptive wait: spin → yield → kernel wait.
        /// @param check_fn  Predicate that returns true when data is ready.
        /// @param timeout   Maximum time to spend in kernel wait.
        /// @return true if check_fn returned true, false if timed-out.
        template <typename CheckFn>
        bool wait(CheckFn &&check_fn, std::chrono::microseconds timeout);

        /// Spin-only wait (never enters kernel).
        template <typename CheckFn>
        bool spinWait(CheckFn &&check_fn, int spin_count = 1000);

    private:
        NotifyBlock *block_{nullptr};

#ifdef _WIN32
        void *hEvent_ = nullptr;
#endif
    };

    // ──── Inline template implementations ────

    template <typename CheckFn>
    bool ShmWaiter::spinWait(CheckFn &&check_fn, int spin_count)
    {
        for (int i = 0; i < spin_count; ++i)
        {
            if (check_fn())
            {
                return true;
            }
            detail::cpuRelax();
        }
        return false;
    }

    template <typename CheckFn>
    bool ShmWaiter::wait(CheckFn &&check_fn, std::chrono::microseconds timeout)
    {
        // Phase 1: short spin with architecture-appropriate hint
        if (spinWait(std::forward<CheckFn>(check_fn), 1000))
        {
            return true;
        }

        // Phase 2: scheduler-friendly yield
        for (int i = 0; i < 10; ++i)
        {
            if (check_fn())
            {
                return true;
            }
            std::this_thread::yield();
        }

        // Phase 3: kernel wait
        if (timeout.count() <= 0)
        {
            return check_fn();
        }

        // Snapshot futex/event word so the kernel side can detect changes.
        uint32_t expected = block_->futex_word.load(std::memory_order_relaxed);

        // Last cheap check before entering the kernel
        if (check_fn())
        {
            return true;
        }

        const bool woken = kernelWait(
            block_,
            expected,
            timeout
#ifdef _WIN32
            ,
            hEvent_
#endif
        );
        (void)woken;

        // Re-check predicate after wake or timeout
        return check_fn();
    }

} // namespace lux::communication::transport