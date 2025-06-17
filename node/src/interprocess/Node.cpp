#include "lux/communication/interprocess/Node.hpp"
#include "lux/communication/CallbackGroupBase.hpp"

namespace lux::communication::interprocess 
{
    Node::Node(const std::string& name, std::shared_ptr<lux::communication::Domain> domain)
    	: TNodeBase(std::move(name), std::move(domain))
    {}
    
    Node::~Node()
    {
        stop();
    }
    
    void Node::stop()
    {
        for (auto& fn : subscriber_stoppers_) { fn(); }
        subscriber_stoppers_.clear();
    }
} // namespace lux::communication::interprocess
