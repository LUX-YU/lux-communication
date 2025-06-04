#pragma once
#include <memory>
#include <mutex>
#include "CallbackGroup.hpp"
#include <lux/cxx/container/SparseSet.hpp>
#include <lux/communication/visibility.h>

namespace lux::communication
{
	class Domain;
	class PublisherBase;
	class SubscriberBase;

	class LUX_COMMUNICATION_PUBLIC NodeBase
	{
		friend class PublisherBase;
		friend class SubscriberBase;
		friend class CallbackGroup;
		friend class Executor;
	public:
		NodeBase(const std::string&, std::shared_ptr<Domain> domain);

		~NodeBase();

		const std::string& name();

		const Domain& domain() const;
		const std::vector<std::weak_ptr<CallbackGroup>>& callbackGroups() const;

	protected:
		Domain& domain();

		void addCallbackGroup(std::shared_ptr<CallbackGroup> callback_group);
		void addPublisher(std::shared_ptr<PublisherBase>);
		void addSubscriber(std::shared_ptr<SubscriberBase>);

		void removePublisher(size_t pub_id);
		void removeSubscriber(size_t sub_id);
		void removeCallbackGroup(size_t g_id);

	private:
		std::string node_name_;

		using CallbackGroupList = lux::cxx::AutoSparseSet<std::weak_ptr<CallbackGroup>>;
		using PubSet = lux::cxx::AutoSparseSet<std::weak_ptr<PublisherBase>>;
		using SubSet = lux::cxx::AutoSparseSet<std::weak_ptr<SubscriberBase>>;
		
		mutable std::mutex		mutex_pub_;
		mutable std::mutex		mutex_sub_;
		mutable std::mutex		mutex_callback_groups_;

		PubSet					publishers_;
		SubSet					subscribers_;
		CallbackGroupList		callback_groups_;
		
		std::shared_ptr<Domain>	domain_;

	protected:
		std::shared_ptr<CallbackGroup> default_callback_group_;
	};

	template<typename Derived>
	class TNodeBase : public NodeBase
	{
	public:
		using NodeBase::NodeBase;

		std::shared_ptr<CallbackGroup> createCallbackGroup(CallbackGroupType type)
		{
			auto self_sptr = static_cast<Derived&>(*this).shared_from_this();
			auto group = std::make_shared<CallbackGroup>(std::move(self_sptr), type);
			NodeBase::addCallbackGroup(group);
			return group;
		}

		std::shared_ptr<CallbackGroup> default_callback_group()
		{
			if (default_callback_group_)
			{
				return default_callback_group_;
			}

			auto self_sptr = static_cast<Derived&>(*this).shared_from_this();
			default_callback_group_ = std::make_shared<CallbackGroup>(self_sptr);

			NodeBase::addCallbackGroup(default_callback_group_);

			return default_callback_group_;
		}
	};
}