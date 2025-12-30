#pragma once

#include <string>
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

	// Subscriber snapshot type for Copy-On-Write (lock-free read)
	using SubscriberSnapshot = std::shared_ptr<const std::vector<SubscriberBase*>>;

    class LUX_COMMUNICATION_PUBLIC TopicBase
    {
		friend class Domain;
		friend class PublisherBase;
		friend class SubscriberBase;
    public:

		TopicBase() = default;

        virtual ~TopicBase();

		Domain& domain() { return *domain_; }
		const Domain& domain() const { return *domain_; }

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

		/**
		 * @brief Get a snapshot of current subscribers (lock-free read).
		 *        The returned snapshot is immutable and safe to iterate.
		 */
		SubscriberSnapshot getSubscriberSnapshot() const
		{
			return sub_snapshot_.load(std::memory_order_acquire);
		}

    protected:
		void setDomain(Domain* d)
		{
			domain_ = d;
		}

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

		/**
		 * @brief Rebuild the subscriber snapshot (called under mutex_sub_ lock).
		 */
		void rebuildSubscriberSnapshot();

		template<typename Func>
		void foreachSubscriber(Func&& func)
		{
			// Lock-free read of snapshot (Copy-On-Write)
			auto snapshot = sub_snapshot_.load(std::memory_order_acquire);
			if (snapshot)
			{
				for (auto sub : *snapshot)
				{
					func(sub);
				}
			}
		}

    protected:
		using PubSet = lux::cxx::AutoSparseSet<PublisherBase*>;
		using SubSet = lux::cxx::AutoSparseSet<SubscriberBase*>;

		Domain*						domain_{ nullptr };

		mutable std::mutex			mutex_pub_;
		mutable std::mutex			mutex_sub_;

		PubSet						publishers_;
		SubSet						subscribers_;

		// Copy-On-Write subscriber snapshot (lock-free read, locked write)
		std::atomic<SubscriberSnapshot>	sub_snapshot_;

		lux::cxx::basic_type_info	type_info_;
		size_t						id_in_domain_{std::numeric_limits<size_t>::max()};
		std::string					topic_name_;
    };
}
