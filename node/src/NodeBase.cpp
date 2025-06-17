#include <lux/communication/NodeBase.hpp>
#include <lux/communication/PublisherBase.hpp>
#include <lux/communication/SubscriberBase.hpp>
#include <lux/communication/CallbackGroupBase.hpp>

namespace lux::communication
{
	NodeBase::NodeBase(const std::string& name, Domain& domain)
		: node_name_(name), domain_(domain)
	{
	}

	NodeBase::~NodeBase() = default;

	const std::string& NodeBase::name()
	{
		return node_name_;
	}

	void NodeBase::addCallbackGroup(CallbackGroupBase* callback_group)
	{
		std::lock_guard lck(mutex_callback_groups_);
		if (callback_groups_.contains(callback_group->idInNode()))
		{
			return;
		}
		auto idx = callback_groups_.insert(callback_group);
		callback_group->setIdInNode(idx);
	}

	void NodeBase::addPublisher(PublisherBase* pub)
	{
		std::lock_guard lck(mutex_pub_);
		if (publishers_.contains(pub->idInNode()))
		{
			return;
		}
		auto idx = publishers_.insert(pub);
		pub->setIdInNode(idx);
	}

	void NodeBase::addSubscriber(SubscriberBase* sub)
	{
		std::lock_guard lck(mutex_sub_);
		if (subscribers_.contains(sub->idInNode()))
		{
			return;
		}
		auto idx = subscribers_.insert(sub);
		sub->setIdInNode(idx);
	}

	void NodeBase::removeCallbackGroup(CallbackGroupBase* callback_group)
	{
		std::lock_guard lck(mutex_callback_groups_);
		if (!callback_groups_.contains(callback_group->idInNode()))
		{
			return; // CallbackGroup not found
		}
		callback_groups_.erase(callback_group->idInNode());
		callback_group->setIdInNode(invalid_id);
	}

	void NodeBase::removePublisher(PublisherBase* pub)
	{
		std::lock_guard lck(mutex_pub_);
		if (!publishers_.contains(pub->idInNode()))
		{
			return; // CallbackGroup not found
		}
		publishers_.erase(pub->idInNode());
		pub->setIdInNode(invalid_id);
	}

	void NodeBase::removeSubscriber(SubscriberBase* sub)
	{
		std::lock_guard lck(mutex_sub_);
		if (!subscribers_.contains(sub->idInNode()))
		{
			return; // CallbackGroup not found
		}
		subscribers_.erase(sub->idInNode());
		sub->setIdInNode(invalid_id);
	}

	Domain& NodeBase::domain()
	{
		return domain_;
	}

	const Domain& NodeBase::domain() const
	{
		return domain_;
	}
} // namespace lux::communication