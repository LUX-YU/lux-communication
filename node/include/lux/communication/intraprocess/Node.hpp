#pragma once

#include <memory>
#include <lux/communication/visibility.h>
#include <lux/communication/Domain.hpp>
#include <lux/communication/NodeBase.hpp>

namespace lux::communication
{
    class CallbackGroupBase;
}

namespace lux::communication::intraprocess
{
    using lux::communication::Domain;
	class LUX_COMMUNICATION_PUBLIC Node : public lux::communication::NodeBase
    {
        template<typename T> friend class Publisher;
        template<typename T> friend class Subscriber;
    public:
        explicit Node(const std::string& node_name, Domain& domain = Domain::default_domain());

        ~Node();

        CallbackGroupBase* defaultCallbackGroup();
    private:

        std::unique_ptr<lux::communication::CallbackGroupBase> default_callbackgroup_;
    };

    LUX_COMMUNICATION_PUBLIC void spin(Node*);
    LUX_COMMUNICATION_PUBLIC void spinUntil(Node*, bool& flag);
    LUX_COMMUNICATION_PUBLIC void stopSpin();
}

