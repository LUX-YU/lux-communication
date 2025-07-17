#include <lux/communication/PublisherBase.hpp>
#include <lux/communication/TopicBase.hpp>
#include <lux/communication/Domain.hpp>
#include <lux/communication/NodeBase.hpp>

namespace lux::communication
{
	PublisherBase::PublisherBase(TopicSptr topic, NodeBase* node)
		: node_(node), topic_(std::move(topic))
	{
		node_->addPublisher(this);
	}

	PublisherBase::~PublisherBase()
	{
		node_->removePublisher(this);
	}
}