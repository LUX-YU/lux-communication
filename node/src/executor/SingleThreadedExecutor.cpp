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
        if (!spinning_) {
            return;
        }
        auto sub = waitOneReadyTimeout(std::chrono::milliseconds(1));
        if (sub)
        {
            handleSubscriber(std::move(sub));
        }
    }

    void SingleThreadedExecutor::handleSubscriber(SubscriberBase* sub)
    {
        sub->takeAll();
    }

} // namespace lux::communication
