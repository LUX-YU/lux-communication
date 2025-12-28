#include "lux/communication/executor/SingleThreadedExecutor.hpp"
#include "lux/communication/SubscriberBase.hpp"

namespace lux::communication 
{
    SingleThreadedExecutor::~SingleThreadedExecutor()
    {
        stop();
    }

    bool SingleThreadedExecutor::spinSome()
    {
        if (!spinning_) {
            return false;
        }
        auto sub = waitOneReadyTimeout(std::chrono::milliseconds(1));
        if (sub)
        {
            handleSubscriber(std::move(sub));
        }
        return spinning_;
    }

    void SingleThreadedExecutor::handleSubscriber(SubscriberBase* sub)
    {
        sub->takeAll();
    }

} // namespace lux::communication
