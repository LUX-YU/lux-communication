#include <lux/communication/NodeBase.hpp>
#include <lux/communication/PublisherBase.hpp>
#include <lux/communication/SubscriberBase.hpp>

namespace lux::communication
{
	NodeBase::NodeBase(const std::string& name, std::shared_ptr<Domain> domain)
		: node_name_(name), domain_(std::move(domain))
	{
	}

	NodeBase::~NodeBase() = default;

	const std::string& NodeBase::name()
	{
		return node_name_;
	}

	void NodeBase::addCallbackGroup(std::shared_ptr<CallbackGroup> callback_group)
	{
		std::lock_guard lck(mutex_callback_groups_);
		callback_group->setId(
			callback_groups_.insert(callback_group)
		);
	}

	void NodeBase::addPublisher(std::shared_ptr<PublisherBase> pub)
	{
		std::lock_guard lk(mutex_pub_);
		if (publishers_.contains(pub->id()))        // SparseSet 新增 contains(ptr) 接口
			return;
		pub->setId(publishers_.insert(pub));
	}

	void NodeBase::addSubscriber(std::shared_ptr<SubscriberBase> sub)
	{
		std::lock_guard lck(mutex_sub_);
		if (subscribers_.contains(sub->id()))
		{
			return;
		}
		sub->setId(subscribers_.insert(sub));
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

	void NodeBase::removeCallbackGroup(size_t g_id)
	{
		std::lock_guard lck(mutex_callback_groups_);
		if (!callback_groups_.contains(g_id))
		{
			return; // CallbackGroup not found
		}
		callback_groups_.erase(g_id);
	}

	Domain& NodeBase::domain()
	{
		return *domain_;
	}

	const Domain& NodeBase::domain() const
	{
		return *domain_;
	}

	const std::vector<std::weak_ptr<CallbackGroup>>& NodeBase::callbackGroups() const
	{
		std::lock_guard lck(mutex_callback_groups_);
		return callback_groups_.values();
	}
} // namespace lux::communication