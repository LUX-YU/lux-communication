#pragma once
#include <memory>
#include <numeric>
#include <future>

#include "lux/cxx/concurrent/BlockingQueue.hpp"

#ifdef __MACRO_USE_LOCKFREE_QUEUE__
#	if __has_include("concurrentqueue/concurrentqueue.h")
#		include "concurrentqueue/concurrentqueue.h"
#	else
#		include "moodycamel/blockingconcurrentqueue.h"
#	endif
#endif

namespace lux::communication::introprocess {
#ifdef __USE_SHARED_MESSAGE_MODE__
#	define make_message(type, ...) std::make_shared<type>(__VA_ARGS__)
	template<typename T> using message_t = std::shared_ptr<T>;
	template<T>
	static inline message_t<T> msg_copy_or_reference(const message_t<T>& msg) {
		return msg;
	}
#else
#	define make_message(type, ...) std::make_unique<type>(__VA_ARGS__)
	template<typename T> using message_t = std::unique_ptr<T>;
	template<typename T>
	static inline message_t<T> msg_copy_or_reference(const message_t<T>& msg) {
		return std::make_unique<T>(*msg);
	}
#endif

	static inline constexpr size_t max_queue_size	= std::numeric_limits<size_t>::max();
	static inline constexpr size_t queue_batch_size = 200;

	template<typename T> using blocking_queue_t = lux::cxx::BlockingQueue<T>;
	template<typename T>
	static inline bool blocking_queue_pop(blocking_queue_t<T>& queue, T& obj) {
		return queue.pop(obj);
	}

	template<typename T>
	static inline bool blocking_queue_try_pop(blocking_queue_t<T>& queue, T& obj) {
		return queue.try_pop(obj);
	}

	template<typename T, typename U>
	static inline bool blocking_queue_push(blocking_queue_t<T>& queue, U&& obj) {
		return queue.push(std::forward<U>(obj));
	}

	template<typename T, typename U>
	static inline bool blocking_queue_try_push(blocking_queue_t<T>& queue, U&& obj) {
		return queue.try_push(std::forward<U>(obj));
	}

	template<typename T>
	static inline void blocking_queue_close(blocking_queue_t<T>& queue) {
		queue.close();
	}

	template<typename T>
	static inline bool blocking_queue_is_closed(blocking_queue_t<T>& queue) {
		return queue.closed();
	}

#ifdef __MACRO_USE_LOCKFREE_QUEUE__
	template<typename T> using queue_t = moodycamel::ConcurrentQueue<T>;
	
	template<typename T>
	static inline bool queue_pop(queue_t<T>& queue, T& obj) {
		return queue.try_dequeue(obj);
	}

	template<typename T>
	static inline size_t queue_pop_bulk(queue_t<T>& queue, std::vector<T>& data, size_t max) {
		data.resize(max);
		size_t count = queue.try_dequeue_bulk(data.begin(), max);
		data.resize(count);
		return count;
	}

	template<typename T>
	static inline bool queue_try_pop(queue_t<T>& queue, T& obj) {
		return queue.try_dequeue(obj);
	}

	template<typename T, typename U>
	static inline bool queue_push(queue_t<T>& queue, U&& obj) {
		return queue.enqueue(std::forward<U>(obj));
	}

	template<typename T>
	static inline bool queue_push_bulk(queue_t<T>& queue, std::vector<T>& data) {
		return queue.enqueue_bulk(std::make_move_iterator(data.begin()), data.size());
	}

	template<typename T, typename U>
	static inline bool queue_try_push(queue_t<T>& queue, U&& obj) {
		return queue.try_enqueue(std::forward<U>(obj));
	}

	template<typename T>
	static inline void queue_close(queue_t<T>& queue){}
#else
	template<typename T> using queue_t = blocking_queue_t<T>;
	
	template<typename T>
	static inline bool queue_pop(queue_t<T>& queue, T& obj) {
		return queue.pop(obj);
	}

	template<typename T>
	static inline size_t queue_pop_bulk(queue_t<T>& queue, std::vector<T>& data, size_t max) {
		data.resize(max);
		size_t count = queue.try_pop_bulk(data.begin(), max);
		data.resize(count);
		return count;
	}

	template<typename T>
	static inline bool queue_try_pop(queue_t<T>& queue, T& obj) {
		return queue.try_pop(obj);
	}

	template<typename T, typename U>
	static inline bool queue_push(queue_t<T>& queue, U&& obj) {
		return queue.push(std::forward<U>(obj));
	}

	template<typename T>
	static inline bool queue_push_bulk(queue_t<T>& queue, std::vector<T>& data) {
		return queue.push_bulk(std::make_move_iterator(data.begin()), data.size());
	}

	template<typename T, typename U>
	static inline bool queue_try_push(queue_t<T>& queue, U&& obj) {
		return queue.try_push(std::forward<U>(obj));
	}

	template<typename T>
	static inline void queue_close(queue_t<T>& queue) {
		queue.close();
	}
#endif

	// Topic Events
	enum class ECommunicationEvent {
		PublisherLeave,
		PublisherJoin,
		PublisherNewData,
		SubscriberNewData,
		SubscriberLeave,
		SubscriberJoin,
		DomainClose,
		Stop
	};

	class PubSubBase;
	class SubscriberBase;
	template<typename T> class Publisher;
	template<typename T> class Subscriber;

	class TopicDomainBase;

	struct EventPayload {};
	struct PublisherPayload : EventPayload { PubSubBase* object{ nullptr }; };
	struct SubscriberPayload : EventPayload { SubscriberBase* object{ nullptr }; };
	struct DomainPayload : EventPayload { TopicDomainBase* object{ nullptr }; };

	struct PublisherRequestPayload : PublisherPayload { std::promise<void> promise; };
	struct SubscriberRequestPayload : SubscriberPayload { std::promise<void> promise; };
	struct DomainRequestPayload : DomainPayload { std::promise<void> promise; };

	struct Stoppayload : EventPayload{};

	template<ECommunicationEvent E> struct PayloadTypeMap;
	template<> struct PayloadTypeMap<ECommunicationEvent::PublisherJoin> { using type = PublisherRequestPayload; };
	template<> struct PayloadTypeMap<ECommunicationEvent::PublisherLeave> { using type = PublisherRequestPayload; };
	template<> struct PayloadTypeMap<ECommunicationEvent::PublisherNewData> { using type = PublisherPayload; };
	template<> struct PayloadTypeMap<ECommunicationEvent::SubscriberNewData> { using type = SubscriberPayload; };
	template<> struct PayloadTypeMap<ECommunicationEvent::SubscriberJoin> { using type = SubscriberRequestPayload; };
	template<> struct PayloadTypeMap<ECommunicationEvent::SubscriberLeave> { using type = SubscriberRequestPayload; };
	template<> struct PayloadTypeMap<ECommunicationEvent::DomainClose> { using type = SubscriberRequestPayload; };
	template<> struct PayloadTypeMap<ECommunicationEvent::Stop> { using type = Stoppayload; };

	struct CommunicationEvent {
		ECommunicationEvent             type;
		std::unique_ptr<EventPayload>   payload;
	};

	template<typename Derived>
	class EventHandler {
	public:
		EventHandler(size_t queue_size)
			: event_queue_(queue_size){}

		bool handle(const CommunicationEvent& event) {
			return static_cast<Derived*>(this)->handle(event);
		}

		bool isStop() const {
			return static_cast<const Derived*>(this)->isStop();
		}

		void stop_event_handler() {
			blocking_queue_close(event_queue_);
		}

		void event_loop() {
			while (!isStop()) {
				if (!event_tick()) {
					break;
				}
			}
		}

		bool event_tick(){
			if (blocking_queue_is_closed(event_queue_)) {
				return false;
			}

			CommunicationEvent event;
			if(blocking_queue_pop(event_queue_, event)){
				return handle(event);
			}

			return true;
		}

		template<ECommunicationEvent E, typename U>
		void notify(std::unique_ptr<U> payload) 
			requires std::is_base_of_v<EventPayload, U>
		{
			CommunicationEvent event{
				.type	 = E,
				.payload = std::move(payload)
			};

			blocking_queue_push(event_queue_, std::move(event));
		}

		template<ECommunicationEvent E, typename U>
		std::future<void> request(std::unique_ptr<U> payload)
			requires std::is_base_of_v<EventPayload, U>
		{
			auto future = payload->promise.get_future();

			CommunicationEvent event{
				.type = E,
				.payload = std::move(payload)
			};

			// if core is ok, push event to queue
			if (!isStop()) {
				blocking_queue_push(event_queue_, std::move(event));
			}
			else {
				// if core is down, handle event directly
				handle(event);
			}

			return future;
		}

	protected:
		blocking_queue_t<CommunicationEvent> event_queue_;
	};


	class Node;
	class Core;
	class TopicDomainBase;

	enum class EPubSub {
		Publisher,
		Subscriber
	};

	class PubSubBase {
	public:
		friend class Node;

		using node_t	 = Node;
		using node_ptr_t = node_t*;
		// using node_ptr_t = std::shared_ptr<node_t>;

		PubSubBase(node_ptr_t node, EPubSub type);

		EPubSub type() const {
			return type_;
		}

		virtual void stop() = 0;

		virtual ~PubSubBase();

		virtual std::shared_ptr<TopicDomainBase> domain() = 0;

	protected:
		EPubSub	   type_;
		node_ptr_t node_;
	};

	class SubscriberBase : public PubSubBase {
	public:
		SubscriberBase(node_ptr_t node)
			: PubSubBase(std::move(node), EPubSub::Subscriber) {
		}

		virtual void popAndDoCallback() = 0;
	};

	template<class Derived, typename T>
	class CrtpPublisher : public PubSubBase {
	public:
		using PubSubBase::PubSubBase;

		size_t pop_bulk(std::vector<message_t<T>>& messages, size_t max_count) {
			return static_cast<Derived*>(this)->pop_bulk(messages, max_count);
		}

		bool pop(message_t<T>& message) {
			return static_cast<Derived*>(this)->pop(message);
		}
	};

	template<class Derived, typename T>
	class CrtpSubscriber : public SubscriberBase {
	public:
		using SubscriberBase::SubscriberBase;

		void push_bulk(std::vector<message_t<T>>& messages) {
			return static_cast<Derived*>(this)->push_bulk(messages);
		}

		bool push(message_t<T> message) {
			return static_cast<Derived*>(this)->push(std::move(message));
		}
	};
}
