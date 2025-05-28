#pragma once
#include <memory>
#include <mutex>
#include <lux/cxx/container/SparseSet.hpp>
#include <lux/communication/visibility.h>

namespace lux::communication
{
	class Domain;
	class PublisherBase;
	class SubscriberBase;
	class CallbackGroup;

	class LUX_COMMUNICATION_PUBLIC NodeBase
	{
		friend class PublisherBase;
		friend class SubscriberBase;
	public:
		using CallbackGroupList = std::vector<CallbackGroup*>;

		NodeBase(const std::string&, std::shared_ptr<Domain> domain);

		const std::string& name();

		const CallbackGroupList callbackGroups() const;

		const Domain& domain() const;

	protected:
		Domain& domain();

		void addCallbackGroup(CallbackGroup* group);
		void removeCallbackGroup(CallbackGroup* group);

		void addPublisher(PublisherBase* pub);
		void addSubscriber(SubscriberBase* sub);

		void removePublisher(PublisherBase* pub);
		void removeSubscriber(SubscriberBase* sub);

		void removePublisher(size_t pub_id);
		void removeSubscriber(size_t sub_id);

	private:
		std::string node_name_;

		using PubSet			= lux::cxx::AutoSparseSet<PublisherBase*>;
		using SubSet			= lux::cxx::AutoSparseSet<SubscriberBase*>;
		
		PubSet					publishers_;
		SubSet					subscribers_;
		CallbackGroupList		callback_groups_;
		std::shared_ptr<Domain>	domain_;
		mutable std::mutex		mutex_pub_;
		mutable std::mutex		mutex_sub_;
		mutable std::mutex		mutex_callback_groups_;
	};
}