#include "lux/communication/executor/MultiThreadedExecutor.hpp"
#include "lux/communication/SubscriberBase.hpp"
#include "lux/communication/CallbackGroupBase.hpp"

namespace lux::communication 
{
    MultiThreadedExecutor::MultiThreadedExecutor(size_t threadNum)
        : thread_pool_(threadNum)
    {
    }

    MultiThreadedExecutor::~MultiThreadedExecutor()
    {
        stop();
    }

    void MultiThreadedExecutor::spinSome()
    {
        // Drain all currently ready subscribers (non-blocking).
        SubscriberBase* sub = nullptr;
        while (ready_queue_.try_dequeue(sub))
        {
            (void)ready_sem_.try_acquire_for(std::chrono::milliseconds(0));
            if (sub)
                handleSubscriber(sub);
        }
    }

    void MultiThreadedExecutor::handleSubscriber(SubscriberBase* sub)
    {
        if (!sub)
            return;

        auto group = sub->callbackGroup();
        if (group->type() == CallbackGroupType::MutuallyExclusive)
        {
            sub->takeAll();
        }
        else
        {
            thread_pool_.submit([sub] { sub->takeAll(); });
        }
    }

    void MultiThreadedExecutor::stop()
    {
        if (spinning_.exchange(false))
        {
            ready_sem_.release();
            notifyCondition();
        }
        thread_pool_.close();
    }

} // namespace lux::communication
