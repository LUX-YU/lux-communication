#include "lux/communication/executor/SingleThreadedExecutor.hpp"
#include "lux/communication/SubscriberBase.hpp"

namespace lux::communication 
{
    SingleThreadedExecutor::~SingleThreadedExecutor()
    {
        stop();
    }

    void SingleThreadedExecutor::spinSome()
    {
        // Drain all currently ready subscribers (non-blocking).
        // Fast path: try_dequeue is a cheap CAS on the lock-free queue.
        // Consume semaphore token only after successful dequeue to stay balanced.
        SubscriberBase* sub = nullptr;
        while (ready_queue_.try_dequeue(sub))
        {
            (void)ready_sem_.try_acquire_for(std::chrono::milliseconds(0));
            if (sub)
                handleSubscriber(sub);
        }
    }

    void SingleThreadedExecutor::handleSubscriber(SubscriberBase* sub)
    {
        sub->takeAll();
    }

} // namespace lux::communication
