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
		sub->setIdInNode(idx);
	}

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
	}
}