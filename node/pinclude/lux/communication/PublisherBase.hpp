#pragma once
#include <memory>
#include <lux/communication/visibility.h>

namespace lux::communication
{
	class NodeBase;
	class TopicBase;

	using NodeBaseSptr	  = std::shared_ptr<NodeBase>;
	using TopicHolderSptr = std::shared_ptr<TopicBase>;

	class LUX_COMMUNICATION_PUBLIC PublisherBase 
		: public std::enable_shared_from_this<PublisherBase>
	{
		friend class NodeBase;
	public:
		PublisherBase(NodeBaseSptr, TopicHolderSptr);

		virtual ~PublisherBase();

		size_t id() const;

		TopicBase& topic();
		const TopicBase& topic() const;

	private:
		void setId(size_t id);

		size_t			id_;
		TopicHolderSptr topic_;
		NodeBaseSptr	node_;
	};
}