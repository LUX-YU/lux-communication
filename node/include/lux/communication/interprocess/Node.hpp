#pragma once

#include <memory>
#include <vector>
#include <functional>

#include <lux/communication/visibility.h>
#include <lux/communication/Domain.hpp>
#include <lux/communication/NodeBase.hpp>

namespace lux::communication
{
    class CallbackGroupBase;
}

namespace lux::communication::interprocess
{
    template<typename T> class Publisher;
    template<typename T> class Subscriber;

    using lux::communication::Domain;

    class LUX_COMMUNICATION_PUBLIC Node : public lux::communication::NodeBase
    {
        template<typename T> friend class Publisher;
        template<typename T> friend class Subscriber;
    public:
        explicit Node(const std::string& node_name, Domain& domain = Domain::default_domain());

        ~Node();

        CallbackGroupBase* defaultCallbackGroup();

        void stop();

    private:
        std::unique_ptr<lux::communication::CallbackGroupBase> default_callbackgroup_;
        std::vector<std::function<void()>> subscriber_stoppers_;
    };

    LUX_COMMUNICATION_PUBLIC void spin(Node*);
    LUX_COMMUNICATION_PUBLIC void spinUntil(Node*, bool& flag);
    LUX_COMMUNICATION_PUBLIC void stopSpin();

} // namespace lux::communication::interprocess
