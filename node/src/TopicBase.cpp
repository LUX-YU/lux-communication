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
		if (publishers_.contains(pub->idInTopic()))
		{
			return;
		}
		auto idx = publishers_.insert(pub);
		pub->setIdInTopic(idx);
	}

	void TopicBase::addSubscriber(SubscriberBase* sub)
	{
		std::lock_guard lck(mutex_sub_);
		if (subscribers_.contains(sub->idInTopic()))
		{
			return;
		}
		auto idx = subscribers_.insert(sub);
		sub->setIdInTopic(idx);
	}

	void TopicBase::removePublisher(PublisherBase* pub)
	{
		std::lock_guard lck(mutex_pub_);
		if (!publishers_.contains(pub->idInTopic()))
		{
			return; // Publisher not found
		}
		publishers_.erase(pub->idInTopic());
		pub->setIdInTopic(invalid_id);
	}

	void TopicBase::removeSubscriber(SubscriberBase* sub)
	{
		std::lock_guard lck(mutex_sub_);
		if (!subscribers_.contains(sub->idInTopic()))
		{
			return; // Subscriber not found
		}
		subscribers_.erase(sub->idInTopic());
		sub->setIdInTopic(invalid_id);
	}
}