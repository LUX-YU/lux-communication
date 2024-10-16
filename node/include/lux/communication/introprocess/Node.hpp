#pragma once
#include <memory>
#include <string_view>
#include <string>
#include <unordered_set>
#include <functional>
#include <iostream>

#include "lux/communication/introprocess/Config.hpp"
#include "lux/communication/visibility.h"

namespace lux::communication::introprocess{
	class PubSubBase;
	template<typename T> class Publisher;
	template<typename T> class Subscriber;
	class Core;

	enum class EPubSub {
		Publisher,
		Subscriber
	};

	class Node;
	class PubSubBase {
	public:
		using node_t     = Node;
		using node_ptr_t = node_t*;
		// using node_ptr_t = std::shared_ptr<node_t>;

		PubSubBase(node_ptr_t node, EPubSub type);

		EPubSub type() const {
			return type_;
		}

		virtual ~PubSubBase();

		virtual std::shared_ptr<TopicDomainBase> domain() = 0;

	protected:
		EPubSub	   type_;
		node_ptr_t node_;
	};

	class SubscriberBase : public PubSubBase {
	public:
		SubscriberBase(node_ptr_t node) 
			: PubSubBase(std::move(node), EPubSub::Subscriber){

		}

		virtual void popAndDoCallback() = 0;
	};

	class Node
	{
		friend class PubSubBase;
	protected:
		struct MakeSharedCoreHelper{};
	public:
		Node(std::shared_ptr<Core> core, std::string_view name)
			: core_(std::move(core)), name_(name){}

		virtual ~Node() {
			exit_ = true;
		}

		Core& core() {
			return *core_;
		}

		const Core& core() const {
			return *core_;
		}

		const std::string& name() const {
			return name_;
		}

		template<typename T> std::shared_ptr<Publisher<T>>
		createPublisher(std::string_view topic, size_t queue_size);

		template<typename T> using callback_t = std::function<void(message_t<T>)>;

		template<typename T> std::shared_ptr<Subscriber<T>>
		createSubscriber(std::string_view topic, callback_t<T> cb, size_t queue_size);

		void spinOnce() {
			for (auto subscriber : subscirbers_) {
				auto sub_base = static_cast<SubscriberBase*>(subscriber);
				sub_base->popAndDoCallback();
			}
		}

		void spin() {
			while (core_->ok() && !exit_) {
				spinOnce();
			}
		}

	private:
		using PubSubMap		 = std::vector<PubSubBase*>;
		using SubscriberList = std::vector<SubscriberBase*>;

		template<typename ListType, typename Ptr>
		void removeFromList(ListType& list, Ptr* ptr) {
			auto iter = std::find_if(
				list.begin(), list.end(),
				[ptr](PubSubBase* p) {
					return ptr == p;
				}
			);

			list.erase(iter);
		}

		template<typename ListType, typename Ptr>
		void addToList(ListType& list, Ptr* ptr) {
			list.push_back(ptr);
		}

		void removePubSub(PubSubBase* ptr) {
			ptr->type() == EPubSub::Publisher ?
				removeFromList(publishers_, ptr) :
				removeFromList(subscirbers_, static_cast<SubscriberBase*>(ptr));
		}

		void addPubSub(PubSubBase* ptr) {
			ptr->type() == EPubSub::Publisher ?
				addToList(publishers_, ptr) :
				addToList(subscirbers_, static_cast<SubscriberBase*>(ptr));
		}

		bool						exit_{false};
		PubSubMap					publishers_;
		SubscriberList				subscirbers_;
		std::shared_ptr<Core>		core_;
		std::string					name_;
	};

	PubSubBase::PubSubBase(node_ptr_t node, EPubSub type)
		: node_(std::move(node)), type_(type) {
		node_->addPubSub(this);
	}

	PubSubBase::~PubSubBase() {
		node_->removePubSub(this);
	};
}
