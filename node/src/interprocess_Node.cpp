#include "lux/communication/interprocess/Node.hpp"

namespace lux::communication::interprocess {

Node::Node(const std::string& name, std::shared_ptr<lux::communication::Domain> domain)
    : name_(name), domain_(std::move(domain))
{
    default_group_ = std::make_shared<lux::communication::CallbackGroup>(
        lux::communication::CallbackGroupType::MutuallyExclusive);
    callback_groups_.push_back(default_group_);
}

Node::~Node()
{
    stop();
}

int Node::getDomainId() const
{
    return domain_ ? domain_->getDomainId() : 0;
}

std::shared_ptr<lux::communication::CallbackGroup> Node::getDefaultCallbackGroup() const
{
    return default_group_;
}

void Node::stop()
{
    for (auto& fn : subscriber_stoppers_) { fn(); }
    subscriber_stoppers_.clear();
}

} // namespace lux::communication::interprocess
