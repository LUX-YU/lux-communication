#include "lux/communication/intraprocess/Node.hpp"

namespace lux::communication::intraprocess {
    Node::Node(const std::string& node_name, Domain& domain)
        : NodeBase(node_name, domain)
    {
        default_callbackgroup_ = std::make_unique<CallbackGroupBase>();
    }
    
    Node::~Node() = default;

    CallbackGroupBase* Node::defaultCallbackGroup()
    {
        return default_callbackgroup_.get();
    }
} // namespace lux::communication::intraprocess
