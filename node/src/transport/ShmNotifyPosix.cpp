/// ShmNotify — Linux (POSIX) implementation using futex.
#include <lux/communication/transport/ShmNotify.hpp>

#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cerrno>
#include <ctime>

namespace lux::communication::transport
{
    // ──── ShmNotifier (Publisher) ────
    ShmNotifier::ShmNotifier(NotifyBlock *block)
        : block_(block) {}

    ShmNotifier::~ShmNotifier() = default;

    void ShmNotifier::wake()
    {
        // Wake at most 1 waiter (SPSC so there is only 1 reader).
        syscall(SYS_futex,
                reinterpret_cast<uint32_t *>(&block_->futex_word),
                FUTEX_WAKE, 1,
                nullptr, nullptr, 0);
    }

    // ──── Free functions ────

    bool kernelWait(NotifyBlock *block,
                    uint32_t expected_futex_val,
                    std::chrono::microseconds timeout)
    {
        struct timespec ts{};
        ts.tv_sec = static_cast<time_t>(timeout.count() / 1'000'000);
        ts.tv_nsec = static_cast<long>((timeout.count() % 1'000'000) * 1'000);

        int rc = static_cast<int>(syscall(
            SYS_futex,
            reinterpret_cast<uint32_t *>(&block->futex_word),
            FUTEX_WAIT, expected_futex_val,
            &ts, nullptr, 0));

        if (rc == 0)
            return true; // woke up normally
        if (errno == EAGAIN)
            return true; // value changed → data arrived
        // ETIMEDOUT or EINTR
        return false;
    }

    void *waiterOpen(NotifyBlock * /*block*/)
    {
        // On Linux, futex operates directly on the shared-memory address.
        return nullptr;
    }

    void waiterClose(void * /*handle*/)
    {
        // Nothing to close on Linux.
    }

    // ──── ShmWaiter (Subscriber) ────

    ShmWaiter::ShmWaiter(NotifyBlock *block)
        : block_(block) {}

    ShmWaiter::~ShmWaiter() = default;

} // namespace lux::communication::transport
