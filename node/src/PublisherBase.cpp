#include <lux/communication/PublisherBase.hpp>
#include <lux/communication/TopicBase.hpp>
#include <lux/communication/NodeBase.hpp>

namespace lux::communication
{
	PublisherBase::PublisherBase(NodeBaseSptr node, TopicHolderSptr topic)
		: topic_(std::move(topic)), node_(std::move(node))
	{
		
	}

	PublisherBase::~PublisherBase()
	{
		node_->removePublisher(id_);
	}

	size_t PublisherBase::id() const
	{
		return id_;
	}

	void PublisherBase::setId(size_t id)
	{
		id_ = id;
	}

	TopicBase& PublisherBase::topic()
	{
		return *topic_;
	}

	const TopicBase& PublisherBase::topic() const
	{
		return *topic_;
	}
}