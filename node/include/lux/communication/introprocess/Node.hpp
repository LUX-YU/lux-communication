#pragma once
#include <memory>
#include <string_view>
#include <string>
#include <unordered_set>
#include <functional>
#include <iostream>

#include "lux/cxx/concurrent/BlockingQueue.hpp"
#include "lux/communication/introprocess/Config.hpp"
#include "lux/communication/visibility.h"

namespace lux::communication::introprocess{

	enum class ENodeEventType{
		NewSubscriberMessage,
		PublisherLeave,
		SubscriberLeave
	};

	class NodeEvent {
		ENodeEventType event_type;
	};

	struct NewSubscriberMessageEvent : public NodeEvent {
		SubscriberBase* object;
	};

	struct PublisherLeaveEvent : public NodeEvent {
		SubscriberBase* object;
	};

	struct SubscriberLeaveEvent : public NodeEvent {
		SubscriberBase* object;
	};

	class Node : public EventHandler<Node>
	{
		friend class PubSubBase;
		friend class EventHandler<Node>;
	protected:
		struct MakeSharedCoreHelper{};
	public:
		using parent_t = EventHandler<Node>;
		template<typename T> using callback_t = std::function<void(message_t<T>)>;

		Node(std::shared_ptr<Core> core, std::string_view name)
			: core_(std::move(core)), name_(name), parent_t(max_queue_size){
			auto payload = std::make_unique<NodeRequestPayload>();
			payload->object = this;

			auto future = core_->request<ECommunicationEvent::NodeCreated>(std::move(payload));
		
			future.get();
		}

		virtual ~Node() {
			exit_ = true;

			auto payload = std::make_unique<NodeRequestPayload>();
			payload->object = this;
			auto future = core_->request<ECommunicationEvent::NodeClosed>(std::move(payload));

			future.get();

			allPublisherStop();
			allSubscriberStop();

			parent_t::stop_event_handler();
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
		createPublisher(std::string_view topic, size_t queue_size) {
			auto new_publisher = std::make_shared<Publisher<T>>(
				this,
				topic, queue_size
			);

			return new_publisher;
		}

		template<typename T> std::shared_ptr<Subscriber<T>>
		createSubscriber(std::string_view topic, callback_t<T> cb, size_t queue_size) {
			auto new_subscriber = std::make_shared<Subscriber<T>>(
				this,
				topic, std::move(cb), queue_size
			);

			return new_subscriber;
		}

		bool spinOnce() {
			return parent_t::event_tick();
		}

		void spin() {
			parent_t::event_loop();
		}

	private:
		bool isStop() const {
			return !core().ok();
		}

		bool handle(const CommunicationEvent& event) {
			auto payload_ptr	= event.payload.get();
			switch (event.type) {
				case ECommunicationEvent::PublisherLeave:
				{
					auto payload = static_cast<PublisherRequestPayload*>(event.payload.get());
					removePubSub(payload->object);
					payload->promise.set_value();
					break;
				}
				case ECommunicationEvent::SubscriberLeave:
				{
					auto payload = static_cast<SubscriberRequestPayload*>(event.payload.get());
					removePubSub(payload->object);
					payload->promise.set_value();
					break;
				}
				case ECommunicationEvent::SubscriberNewData:
				{
					auto payload    = static_cast<SubscriberPayload*>(event.payload.get());
					auto subscriber = static_cast<SubscriberBase*>(payload->object);
					subscriber->popAndDoCallback();
					break;
				}
				case ECommunicationEvent::Stop:
				{
					auto payload = static_cast<Stoppayload*>(event.payload.get());
					allPublisherStop();
					allSubscriberStop();
					payload->promise.set_value();
					parent_t::stop();
					return false;
				}
			}

			return true;
		}

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

		void allPublisherStop() {
			for (auto& pub : publishers_) {
				pub->stop();
			}
		}

		void allSubscriberStop() {
			for (auto& sub : subscirbers_) {
				sub->stop();
			}
		}

		queue_t<std::unique_ptr<CommunicationEvent>>	event_queue_;
		bool											exit_{false};
		PubSubMap										publishers_;
		SubscriberList									subscirbers_;
		std::shared_ptr<Core>							core_;
		std::string										name_;
	};

	PubSubBase::PubSubBase(node_ptr_t node, EPubSub type)
		: node_(std::move(node)), type_(type) {
		node_->addPubSub(this);
	}

	PubSubBase::~PubSubBase() {
		auto payload = std::make_unique<PublisherRequestPayload>();
		payload->object		= this;
		auto future = node_->request<ECommunicationEvent::PublisherLeave>(std::move(payload));
	
		future.get();
	};

	class SingleThreadNode : public Node {
		~SingleThreadNode() {

		}

	private:
		std::atomic<bool> eixt;
	};
}
