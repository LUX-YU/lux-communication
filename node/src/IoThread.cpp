#include "lux/communication/IoThread.hpp"

namespace lux::communication
{
    IoThread::IoThread(transport::IoReactor &reactor, const NodeOptions &opts)
        : reactor_(reactor), opts_(opts)
    {
    }

    IoThread::~IoThread()
    {
        stop();
    }

    uint64_t IoThread::registerPoller(ShmPollFn fn)
    {
        std::lock_guard lock(poll_mutex_);
        uint64_t h = next_handle_++;
        poll_entries_.push_back(PollEntry{h, std::move(fn)});
        // Lazy start: spin up the IO thread on first poller registration.
        if (!running_.load(std::memory_order_relaxed))
            start();
        // Wake up the reactor so it picks up the new poller immediately.
        reactor_.wakeup();
        return h;
    }

    void IoThread::unregisterPoller(uint64_t handle)
    {
        std::lock_guard lock(poll_mutex_);
        std::erase_if(poll_entries_, [handle](const PollEntry &e)
                      { return e.handle == handle; });
    }

    void IoThread::start()
    {
        bool expected = false;
        if (!running_.compare_exchange_strong(expected, true))
            return; // already running

        io_thread_ = std::thread([this]
                                 { ioLoop(); });
    }

    void IoThread::stop()
    {
        if (!running_.exchange(false))
            return; // already stopped

        reactor_.wakeup(); // unblock pollOnce

        if (io_thread_.joinable())
            io_thread_.join();
    }

    void IoThread::ioLoop()
    {
        using namespace std::chrono;

        while (running_.load(std::memory_order_relaxed))
        {
            // 1. Drive the network reactor (non-blocking or short timeout).
            if (reactor_.fdCount() > 0)
            {
                reactor_.pollOnce(milliseconds{opts_.reactor_timeout_ms});
            }

            // 2. Poll all SHM readers.
            {
                std::lock_guard lock(poll_mutex_);
                for (auto &entry : poll_entries_)
                {
                    entry.fn();
                }
            }

            // 3. If nothing to do, sleep briefly to avoid busy-spin.
            if (reactor_.fdCount() == 0)
            {
                std::lock_guard lock(poll_mutex_);
                if (poll_entries_.empty())
                {
                    // Nothing registered — sleep a bit.
                    // (release lock before sleeping)
                }
            }

            // Brief yield when no net fds to avoid pure busy loop on SHM poll.
            if (reactor_.fdCount() == 0)
            {
                std::this_thread::sleep_for(microseconds{opts_.shm_poll_interval_us});
            }
        }
    }

} // namespace lux::communication
