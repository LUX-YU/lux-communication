#pragma once

#include <string>
#include <typeindex>
#include <mutex>
#include <atomic>
#include <lux/cxx/container/SparseSet.hpp>
#include <lux/cxx/compile_time/type_info.hpp>
#include <lux/communication/visibility.h>

namespace lux::communication
{
    class Domain;
	class PublisherBase;
	class SubscriberBase;

    class LUX_COMMUNICATION_PUBLIC TopicBase
    {
		friend class Domain;
		friend class PublisherBase;
		friend class SubscriberBase;
    public:

		TopicBase() = default;

        virtual ~TopicBase();

		const std::string& name() const
		{
			return topic_name_;
		}

		size_t idInDoamin() const
		{
			return id_in_domain_;
		}

		// Get the type info for this Topic
		const lux::cxx::basic_type_info& typeInfo() const
		{
			return type_info_;
		}

    protected:
		void setTopicName(const std::string& name)
		{
			topic_name_ = name;
		}

		void setIdInDoamin(size_t index)
		{
			id_in_domain_ = index;
		}

		void setTypeInfo(const lux::cxx::basic_type_info& type_info)
		{
			type_info_ = type_info;
		}

		void addPublisher(PublisherBase*);
		void addSubscriber(SubscriberBase*);

		void removePublisher(PublisherBase*);
		void removeSubscriber(SubscriberBase*);

		template<typename Func>
		void foreachSubscriber(Func&& func)
		{
			std::vector<SubscriberBase*> sub_buffers;
			{
				std::scoped_lock lck(mutex_sub_);
				sub_buffers = subscribers_.values();
			}
			
			for (auto sub : sub_buffers)
			{
				func(sub);
			}
		}

    protected:
		using PubSet = lux::cxx::AutoSparseSet<PublisherBase*>;
		using SubSet = lux::cxx::AutoSparseSet<SubscriberBase*>;

		mutable std::mutex			mutex_pub_;
		mutable std::mutex			mutex_sub_;

		PubSet						publishers_;
		SubSet						subscribers_;

		lux::cxx::basic_type_info	type_info_;
		size_t						id_in_domain_{std::numeric_limits<size_t>::max()};
		std::string					topic_name_;
    };
}
