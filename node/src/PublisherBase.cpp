#include <lux/communication/PublisherBase.hpp>
#include <lux/communication/ITopicHolder.hpp>
#include <lux/communication/NodeBase.hpp>

namespace lux::communication
{
	PublisherBase::PublisherBase(NodeBaseSptr node, TopicHolderSptr topic)
		: topic_(std::move(topic)), node_(std::move(node))
	{
		node_->addPublisher(this);
	}

	PublisherBase::~PublisherBase()
	{
		node_->removePublisher(this);
	}

	size_t PublisherBase::id() const
	{
		return id_;
	}

	void PublisherBase::setId(size_t id)
	{
		id_ = id;
	}

	ITopicHolder& PublisherBase::topic()
	{
		return *topic_;
	}

	const ITopicHolder& PublisherBase::topic() const
	{
		return *topic_;
	}
}