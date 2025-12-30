#include <lux/communication/TopicBase.hpp>
#include <lux/communication/PublisherBase.hpp>
#include <lux/communication/SubscriberBase.hpp>

namespace lux::communication
{
	static inline constexpr size_t invalid_id = std::numeric_limits<size_t>::max();

	TopicBase::~TopicBase()
	{

	}

	void TopicBase::addPublisher(PublisherBase* pub)
	{
		std::lock_guard lck(mutex_pub_);
		if (publishers_.contains(pub->idInNode()))
		{
			return;
		}
		auto idx = publishers_.insert(pub);
		pub->setIdInNode(idx);
	}

	void TopicBase::addSubscriber(SubscriberBase* sub)
	{
		std::lock_guard lck(mutex_sub_);
		if (subscribers_.contains(sub->idInNode()))
		{
			return;
		}
		auto idx = subscribers_.insert(sub);
		sub->setIdInNode(idx);		// Rebuild COW snapshot
		rebuildSubscriberSnapshot();	}

	void TopicBase::removePublisher(PublisherBase* pub)
	{
		std::lock_guard lck(mutex_pub_);
		if (!publishers_.contains(pub->idInNode()))
		{
			return; // CallbackGroup not found
		}
		publishers_.erase(pub->idInNode());
		pub->setIdInNode(invalid_id);
	}

	void TopicBase::removeSubscriber(SubscriberBase* sub)
	{
		std::lock_guard lck(mutex_sub_);
		if (!subscribers_.contains(sub->idInNode()))
		{
			return; // CallbackGroup not found
		}
		subscribers_.erase(sub->idInNode());
		sub->setIdInNode(invalid_id);
		// Rebuild COW snapshot
		rebuildSubscriberSnapshot();
	}

	void TopicBase::rebuildSubscriberSnapshot()
	{
		// Called under mutex_sub_ lock
		SubscriberSnapshot new_snapshot = std::make_shared<const std::vector<SubscriberBase*>>(subscribers_.values());
		sub_snapshot_.store(new_snapshot, std::memory_order_release);
	}
}