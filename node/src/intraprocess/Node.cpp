#include "lux/communication/intraprocess/Node.hpp"
#include "lux/communication/CallbackGroupBase.hpp"

namespace lux::communication::intraprocess {
    Node::Node(const std::string& node_name, Domain& domain)
        : NodeBase(node_name, domain)
    {
        default_callbackgroup_ = std::make_unique<lux::communication::CallbackGroupBase>(
            this, CallbackGroupType::MutuallyExclusive
        );
    }
    
    Node::~Node() = default;

    CallbackGroupBase* Node::defaultCallbackGroup()
    {
        return default_callbackgroup_.get();
    }

    void spin(Node* node)
    {
        static SingleThreadedExecutor default_executor;
        default_executor.addNode(node);
        default_executor.spin();
        default_executor.removeNode(node);
    }
} // namespace lux::communication::intraprocess
