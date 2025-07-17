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

    static SingleThreadedExecutor& default_executor()
    {
        static SingleThreadedExecutor instance;
        return instance;
    }

    void spin(Node* node)
    {
        auto& exec = default_executor();
        exec.addNode(node);
        exec.spin();
        exec.removeNode(node);
    }

    void spinUntil(Node* node, bool& flag)
    {
        auto& exec = default_executor();
        exec.addNode(node);
        while (flag && exec.spinSome());
        exec.removeNode(node);
    }

    void stopSpin()
    {
        auto& exec = default_executor();
        exec.stop();
    }
} // namespace lux::communication::intraprocess
