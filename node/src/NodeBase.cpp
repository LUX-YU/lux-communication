#include <lux/communication/NodeBase.hpp>
#include <lux/communication/PublisherBase.hpp>
#include <lux/communication/SubscriberBase.hpp>

namespace lux::communication
{
	NodeBase::NodeBase(const std::string& name, std::shared_ptr<Domain> domain)
		: node_name_(name), domain_(std::move(domain))
	{
	}

	const std::string& NodeBase::name()
	{
		return node_name_;
	}

	void NodeBase::addPublisher(PublisherBase* pub)
	{
		std::lock_guard lck(mutex_pub_);
		pub->setId(publishers_.insert(pub));
		if (publishers_.contains(pub->id()))
		{
			return;
		}
		publishers_.insert(std::move(pub));
	}

	void NodeBase::addSubscriber(SubscriberBase* sub)
	{
		std::lock_guard lck(mutex_sub_);
		sub->setId(subscribers_.insert(sub));
		if (subscribers_.contains(sub->id()))
		{
			return;
		}
		subscribers_.insert(std::move(sub));
	}

	void NodeBase::removePublisher(PublisherBase* pub)
	{
		std::lock_guard lck(mutex_pub_);
		if (!publishers_.contains(pub->id()))
		{
			return; // Publisher not found
		}
		publishers_.erase(pub->id());
	}

	void NodeBase::removeSubscriber(SubscriberBase* sub)
	{
		std::lock_guard lck(mutex_sub_);
		if (!subscribers_.contains(sub->id()))
		{
			return; // Subscriber not found
		}
		subscribers_.erase(sub->id());
	}

	void NodeBase::removePublisher(size_t pub_id)
	{
		std::lock_guard lck(mutex_pub_);
		if (!publishers_.contains(pub_id))
		{
			return; // Publisher not found
		}
		publishers_.erase(pub_id);
	}

	void NodeBase::removeSubscriber(size_t sub_id)
	{
		std::lock_guard lck(mutex_sub_);
		if (!subscribers_.contains(sub_id))
		{
			return; // Subscriber not found
		}
		subscribers_.erase(sub_id);
	}

	void NodeBase::addCallbackGroup(CallbackGroup* group)
	{
		std::lock_guard lck(mutex_callback_groups_);
		for (const auto& g : callback_groups_)
		{
			if (g == group)
			{
				return; // Group already exists
			}
		}
		
		callback_groups_.push_back(std::move(group));
	}

	void NodeBase::removeCallbackGroup(CallbackGroup* group)
	{
		std::lock_guard lck(mutex_callback_groups_);
		auto it = std::find(callback_groups_.begin(), callback_groups_.end(), group);
		if (it != callback_groups_.end())
		{
			callback_groups_.erase(it);
			return; // Group not found
		}
	}

	Domain& NodeBase::domain()
	{
		return *domain_;
	}

	const Domain& NodeBase::domain() const
	{
		return *domain_;
	}

	const NodeBase::CallbackGroupList NodeBase::callbackGroups() const
	{
		return callback_groups_;
	}
} // namespace lux::communication