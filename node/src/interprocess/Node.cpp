#include "lux/communication/interprocess/Node.hpp"

namespace lux::communication::interprocess {

Node::Node(const std::string& name, std::shared_ptr<lux::communication::Domain> domain)
	: NodeBase(std::move(name), std::move(domain))
{}

Node::~Node()
{
    stop();
}

int Node::getDomainId() const
{
    return domain_ ? domain_->getDomainId() : 0;
}

void Node::stop()
{
    for (auto& fn : subscriber_stoppers_) { fn(); }
    subscriber_stoppers_.clear();
}

} // namespace lux::communication::interprocess
