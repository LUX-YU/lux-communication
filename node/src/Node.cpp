#include "lux/communication/intraprocess/Node.hpp"

namespace lux::communication::intraprocess {

Node::Node(const std::string& nodeName, std::shared_ptr<Domain> domain)
    : node_name_(nodeName), domain_(std::move(domain)), running_(false)
{
    default_callback_group_ = std::make_shared<CallbackGroup>(CallbackGroupType::MutuallyExclusive);
    callback_groups_.push_back(default_callback_group_);
}

Node::~Node() {
    stop();
}

std::shared_ptr<CallbackGroup> Node::getDefaultCallbackGroup() const
{
    return default_callback_group_;
}

const std::vector<std::shared_ptr<CallbackGroup>>& Node::getCallbackGroups() const
{
    return callback_groups_;
}

void Node::removePublisher(int pub_id)
{
    std::lock_guard<std::mutex> lock(mutex_pub_);
    if (pub_set_.contains(pub_id))
    {
        pub_set_.erase(pub_id);
        free_pub_ids_.push_back(pub_id);
    }
}

void Node::removeSubscriber(int sub_id)
{
    std::lock_guard<std::mutex> lock(mutex_sub_);
    if (sub_set_.contains(sub_id))
    {
        sub_set_.erase(sub_id);
        free_sub_ids_.push_back(sub_id);
    }
}

void Node::stop()
{
    running_ = false;
}

} // namespace lux::communication::intraprocess

