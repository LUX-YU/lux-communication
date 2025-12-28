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

    bool MultiThreadedExecutor::spinSome()
    {
        auto sub = waitOneReadyTimeout(std::chrono::milliseconds(1));
        if (!spinning_)
            return false;
        if (sub)
        {
            handleSubscriber(std::move(sub));
        }
        return spinning_;
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
