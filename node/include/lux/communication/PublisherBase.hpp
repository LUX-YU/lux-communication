#pragma once
#include <memory>
#include <lux/communication/visibility.h>

namespace lux::communication
{
	class NodeBase;
	class ITopicHolder;

	using NodeBaseSptr = std::shared_ptr<NodeBase>;
	using TopicHolderSptr = std::shared_ptr<ITopicHolder>;

	class LUX_COMMUNICATION_PUBLIC PublisherBase 
		: public std::enable_shared_from_this<PublisherBase>
	{
		friend class NodeBase;
	public:
		PublisherBase(NodeBaseSptr, TopicHolderSptr);

		virtual ~PublisherBase();

		size_t id() const;

		ITopicHolder& topic();
		const ITopicHolder& topic() const;

	private:
		void setId(size_t id);

		size_t			id_;
		TopicHolderSptr topic_;
		NodeBaseSptr	node_;
	};
}