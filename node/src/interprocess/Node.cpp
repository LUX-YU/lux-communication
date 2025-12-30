#include "lux/communication/interprocess/Node.hpp"
#include "lux/communication/executor/SingleThreadedExecutor.hpp"
#include "lux/communication/CallbackGroupBase.hpp"

namespace lux::communication::interprocess 
{
    Node::Node(const std::string& node_name, Domain& domain)
        : NodeBase(node_name, domain)
    {
        default_callbackgroup_ = std::make_unique<lux::communication::CallbackGroupBase>(
            this, CallbackGroupType::MutuallyExclusive
        );
    }
    
    Node::~Node()
    {
        stop();
    }

    CallbackGroupBase* Node::defaultCallbackGroup()
    {
        return default_callbackgroup_.get();
    }
    
    void Node::stop()
    {
        for (auto& fn : subscriber_stoppers_) { fn(); }
        subscriber_stoppers_.clear();
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
        while (flag) {
            exec.spinSome();
        }
        exec.removeNode(node);
    }

    void stopSpin()
    {
        auto& exec = default_executor();
        exec.stop();
    }

} // namespace lux::communication::interprocess
