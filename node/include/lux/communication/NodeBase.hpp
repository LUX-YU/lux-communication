#pragma once
#include <memory>
#include <mutex>
#include <lux/cxx/container/SparseSet.hpp>
#include <lux/communication/visibility.h>

namespace lux::communication
{
	class Domain;
	class CallbackGroupBase;
	class PublisherBase;
	class SubscriberBase;

	class LUX_COMMUNICATION_PUBLIC NodeBase
	{
		friend class PublisherBase;
		friend class SubscriberBase;
		friend class CallbackGroupBase;
		friend class ExecutorBase;
	public:
		NodeBase(const std::string& name, Domain& domain);

		~NodeBase();

		const std::string& name();

		Domain& domain();
		const Domain& domain() const;

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

		template<typename Func>
		void foreachPublisher(Func&& func)
		{
			std::vector<PublisherBase*> pub_buffers;
			{
				std::scoped_lock lck(mutex_pub_);
				pub_buffers = publishers_.values();
			}

			for (auto pub : pub_buffers)
			{
				func(pub);
			}
		}

		template<typename Func>
		void foreachCallbackGroup(Func&& func)
		{
			std::vector<CallbackGroupBase*> cbg_buffers;
			{
				std::scoped_lock lck(mutex_callback_groups_);
				cbg_buffers = callback_groups_.values();
			}

			for (auto cbg : cbg_buffers)
			{
				func(cbg);
			}
		}

		size_t idInExecutor() const
		{
			return id_in_executor_;
		}

	protected:

		void addCallbackGroup(CallbackGroupBase*);
		void addPublisher(PublisherBase*);
		void addSubscriber(SubscriberBase*);

		void removeCallbackGroup(CallbackGroupBase*);
		void removePublisher(PublisherBase*);
		void removeSubscriber(SubscriberBase*);
		
		// also assign id
		void setExecutor(size_t, ExecutorBase*);

	private:
		std::string node_name_;

		using CallbackGroupList = lux::cxx::AutoSparseSet<CallbackGroupBase*>;
		using PubSet = lux::cxx::AutoSparseSet<PublisherBase*>;
		using SubSet = lux::cxx::AutoSparseSet<SubscriberBase*>;
		
		mutable std::mutex		mutex_callback_groups_;
		mutable std::mutex		mutex_pub_;
		mutable std::mutex		mutex_sub_;

		PubSet					publishers_;
		SubSet					subscribers_;
		CallbackGroupList		callback_groups_;
		
		Domain&					domain_;

		size_t                  id_in_executor_{std::numeric_limits<size_t>::max()};
	};
}