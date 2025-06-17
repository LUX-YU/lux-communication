#pragma once
#include <memory>
#include <string>
#include <lux/communication/visibility.h>

namespace lux::communication
{
	class NodeBase;
	class TopicBase;
	using TopicSptr = std::shared_ptr<TopicBase>;

	class LUX_COMMUNICATION_PUBLIC PublisherBase 
	{
		friend class NodeBase;
		friend class TopicBase;
	public:
		virtual ~PublisherBase();

		size_t idInNode() const
		{
			return id_in_node_;
		}

		size_t idInTopic() const
		{
			return id_in_topic_;
		}

		template<typename T>
		T& topicAs() requires std::is_base_of_v<TopicBase, T>
		{
			return static_cast<T&>(*topic_);
		}

	protected:
		PublisherBase(TopicSptr topic, NodeBase*);

	private:
		void setIdInNode(size_t id)
		{
			id_in_node_ = id;
		}

		void setIdInTopic(size_t id)
		{
			id_in_topic_ = id;
		}

		NodeBase*		node_;
		TopicSptr		topic_;
		size_t			id_in_node_{std::numeric_limits<size_t>::max()};
		size_t          id_in_topic_{std::numeric_limits<size_t>::max()};
	};
}