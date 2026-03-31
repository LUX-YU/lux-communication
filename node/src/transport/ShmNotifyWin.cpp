/// ShmNotify — Windows implementation using Named Events.
#include <lux/communication/transport/ShmNotify.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstring>
#include <stdexcept>

namespace lux::communication::transport
{
    // ──── Helper: open-or-create the Named Event from the NotifyBlock ────

    static HANDLE openOrCreateEvent(NotifyBlock *block)
    {
        const char *name = block->event_name;
        if (name[0] == '\0')
            throw std::runtime_error("ShmNotify(Win): event_name is empty in NotifyBlock");

        // Auto-reset event: after one waiter is released, the event resets.
        HANDLE h = CreateEventA(nullptr, /*bManualReset=*/FALSE,
                                /*bInitialState=*/FALSE, name);
        if (!h)
            throw std::runtime_error("ShmNotify(Win): CreateEventA failed");
        return h;
    }

    // ──── ShmNotifier (Publisher) ────

    ShmNotifier::ShmNotifier(NotifyBlock *block)
        : block_(block), hEvent_(openOrCreateEvent(block))
    {
    }

    ShmNotifier::~ShmNotifier()
    {
        if (hEvent_)
        {
            CloseHandle(static_cast<HANDLE>(hEvent_));
            hEvent_ = nullptr;
        }
    }

    void ShmNotifier::wake()
    {
        SetEvent(static_cast<HANDLE>(hEvent_));
    }

    // ──── Free functions ────

    bool kernelWait(NotifyBlock * /*block*/,
                    uint32_t /*expected_futex_val*/,
                    std::chrono::microseconds timeout,
                    void *hEvent)
    {
        if (!hEvent)
            return false;

        DWORD ms = static_cast<DWORD>(timeout.count() / 1000);
        if (ms == 0 && timeout.count() > 0)
            ms = 1; // at least 1 ms

        DWORD rc = WaitForSingleObject(static_cast<HANDLE>(hEvent), ms);
        return (rc == WAIT_OBJECT_0);
    }

    void *waiterOpen(NotifyBlock *block)
    {
        return openOrCreateEvent(block);
    }

    void waiterClose(void *handle)
    {
        if (handle)
            CloseHandle(static_cast<HANDLE>(handle));
    }

    // ──── ShmWaiter (Subscriber) ────

    ShmWaiter::ShmWaiter(NotifyBlock *block)
        : block_(block), hEvent_(waiterOpen(block))
    {
    }

    ShmWaiter::~ShmWaiter()
    {
        waiterClose(hEvent_);
        hEvent_ = nullptr;
    }

} // namespace lux::communication::transport
