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

	class LUX_COMMUNICATION_PUBLIC Node : public EventHandler<Node>
	{
		friend class PubSubBase;
		friend class EventHandler<Node>;
	protected:
		struct MakeSharedCoreHelper{};
	public:
		using parent_t = EventHandler<Node>;
		template<typename T> using callback_t = std::function<void(message_t<T>)>;

		Node(std::shared_ptr<Core> core, std::string_view name);

		virtual ~Node();

		Core& core();

		const Core& core() const;

		const std::string& name() const;

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

		bool spinOnce();

		void spin();

	private:
		bool isStop() const;

		bool handle(const CommunicationEvent& event);

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

		void removePubSub(PubSubBase* ptr);

		void addPubSub(PubSubBase* ptr);

		void allPublisherStop();

		void allSubscriberStop();

		queue_t<std::unique_ptr<CommunicationEvent>>	event_queue_;
		bool											exit_{false};
		PubSubMap										publishers_;
		SubscriberList									subscirbers_;
		std::shared_ptr<Core>							core_;
		std::string										name_;
	};

	class SingleThreadNode : public Node {
		~SingleThreadNode() {

		}

	private:
		std::atomic<bool> eixt;
	};
}
