#include "lux/communication/intraprocess/Node.hpp"

namespace lux::communication::intraprocess {
    Node::Node(const std::string& nodeName, std::shared_ptr<Domain> domain)
        : TNodeBase(nodeName, std::move(domain)), running_(false)
    {
    
    }
    
    Node::~Node() {
        stop();
    }
    
    void Node::stop()
    {
        running_ = false;
    }
} // namespace lux::communication::intraprocess

