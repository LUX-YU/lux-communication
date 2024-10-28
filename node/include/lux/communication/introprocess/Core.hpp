//
// Created by lux on 10/6/2024.
//

#ifndef __CORE_HPP__
#define __CORE_HPP__

#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <cstddef>
#include <string>
#include <mutex>
#include <queue>
#include <future>
#include <cassert>
#include <condition_variable>

#include <lux/communication/visibility.h>
#include <lux/cxx/compile_time/type_info.hpp>
#include <lux/cxx/container/HeterogeneousLookup.hpp>
#include <lux/cxx/concurrent/BlockingQueue.hpp>
#include <lux/cxx/concurrent/ThreadPool.hpp>

#include "lux/communication/introprocess/Config.hpp"

namespace lux::communication::introprocess {

    class Core;
    class TopicDomainBase{
    public:
        TopicDomainBase(Core* core, std::string_view topic_name, lux::cxx::basic_type_info info)
            : core_ptr_(core), type_info_(info), topic_name_(topic_name) {}

        virtual ~TopicDomainBase() = default;

        [[nodiscard]] size_t type_hash() const { return type_info_.hash(); }

        [[nodiscard]] std::string_view type_name() const { return type_info_.name(); }

        [[nodiscard]] const std::string& topic_name() const { return topic_name_; }

    protected:
        Core*                       core_ptr_;
    private:
        lux::cxx::basic_type_info   type_info_;
        std::string                 topic_name_;
    };

    // Template declaration of TopicDomain class for generic type T
    template<typename T> class TopicDomain;

    struct DomainLeaveRequest{
        TopicDomainBase*    object;
        std::promise<void>  promise;
    };

    // The Core class manages TopicDomain instances and provides methods
    // to create, access, and check the existence of TopicDomains by topic name.
    class Core : public EventHandler<Core>{
    protected:
        struct MakeSharedCoreHelper{};
        friend class TopicDomainBase;
        friend class EventHandler<Core>;
        friend class Node;
    public:
        using parent_t = EventHandler<Core>;

        Core(const MakeSharedCoreHelper& helper, int argc, char* argv[])
            : Core(argc, argv){}

        virtual ~Core() {
            if (ok_) {
                shutdown();
            }
            stop();
            if (core_thread_.joinable()) {
                core_thread_.join();
            }
        }

        // Static factory method to create a shared pointer to a new Core instance.
        [[nodiscard]] static std::shared_ptr<Core> create(int argc, char* argv[]) {
            return std::make_shared<Core>(MakeSharedCoreHelper{}, argc, argv);
        }

        // Retrieves a TopicDomain of type T associated with the given topic_name.
        // If the domain exists and the type matches, it returns the domain.
        // Throws a runtime error if the type does not match.
        template<typename T>
        std::shared_ptr<TopicDomain<T>> getDomain(std::string_view topic_name) {
            std::scoped_lock lock(mutex_);
            // Check if the topic domain exists in the map
            auto iter = std::find_if(
                topic_domains_.begin(), topic_domains_.end(),
                [topic_name](std::weak_ptr<EventHandlerBase>& ptr) {
                    auto domain_ptr = std::dynamic_pointer_cast<TopicDomain<T>>(ptr.lock());
                    if (!domain_ptr) {
                        return false;
                    }
                    return domain_ptr->topic_name() == topic_name;
                }
            );

            if (iter == topic_domains_.end()) {
                return nullptr;
            }

            auto result = std::dynamic_pointer_cast<TopicDomain<T>>(iter->lock());

            if (result->type_hash() != lux::cxx::type_hash<T>()) {
                throw std::runtime_error("Domain does not have the same type hash with message");
            }

            return std::static_pointer_cast<TopicDomain<T>>(result);
        }

        // Checks if a TopicDomain associated with topic_name exists.
        // Returns true if the domain exists, false otherwise.
        template<typename T>
        [[nodiscard]] bool hasDomain(std::string_view topic_name) const {
            std::scoped_lock lock(mutex_);
            // Uses the heterogeneous map's contains method to check existence
            auto iter = std::find_if(
                topic_domains_.begin(), topic_domains_.end(),
                [topic_name](std::weak_ptr<TopicDomainBase>& ptr) {
                    auto domain_ptr = ptr.lock();
                    if (!domain_ptr) {
                        return false;
                    }
                    return domain_ptr->topic_name() == topic_name;
                }
            );

            return iter != topic_domains_.end();
        }

        // Creates a new TopicDomain of type T with the given topic_name.
        // If the domain already exists, it returns the existing domain.
        template<typename T>
        std::shared_ptr<TopicDomain<T>> createDomain(std::string_view topic_name) {
            // First, check if the domain already exists
            if (auto result = getDomain<T>(topic_name)) {
                // If it exists, return the existing domain
                return result;
            }

            std::scoped_lock lock(mutex_);
            // Create a new TopicDomain instance of type T
            auto domain = std::make_shared<TopicDomain<T>>(this, topic_name);
            // Insert the new domain into the topic_domains map with topic_name as the key
            topic_domains_.push_back(domain);

            // Return the newly created domain
            return domain;
        }

        bool ok() const{
            return ok_;
        }

        void init(){
            std::thread([this]{event_loop();}).swap(core_thread_);
        }

        void shutdown() {
            ok_ = false;
            for (std::weak_ptr<EventHandlerBase> domain : topic_domains_) {
                domain.lock()->stop();
            }

            for (EventHandlerBase* node : nodes_) {
                node->stop();
            }
        }

        void shutdown_wait() {
            ok_ = false;
            auto future = stop_wait();

            future.get();
        }

    private:

        bool isStop() const {
			return !ok_;
		}

        bool handle(const CommunicationEvent& event) {
            switch(event.type){
                case ECommunicationEvent::DomainClosed:{
                    auto payload = static_cast<DomainRequestPayload*>(event.payload.get());
                    auto domain  = static_cast<EventHandlerBase*>(payload->object);
                    remove_topic_domain(domain);

                    payload->promise.set_value();
                    break;
                }
                case ECommunicationEvent::NodeCreated:
                {
                    auto payload = static_cast<NodeRequestPayload*>(event.payload.get());
                    auto node    = static_cast<EventHandlerBase*>(payload->object);

                    nodes_.push_back(node);

                    payload->promise.set_value();
                    break;
                }
                case ECommunicationEvent::NodeClosed:
                {
                    auto payload = static_cast<NodeRequestPayload*>(event.payload.get());
                    auto node = static_cast<EventHandlerBase*>(payload->object);

                    remove_node(node);

                    payload->promise.set_value();
                    break;
                }
                case ECommunicationEvent::Stop: {
                    auto payload = static_cast<Stoppayload*>(event.payload.get());
                    // stop all sub node
                    handle_stop_event();

                    payload->promise.set_value();
                    return false;
                }
			}

            return true;
        }

        void handle_stop_event() {
            std::vector<std::future<void>> future_list;

            {
                std::scoped_lock lck(mutex_);
                future_list.reserve(topic_domains_.size());
                for (std::weak_ptr<EventHandlerBase> domain : topic_domains_) {
                    future_list.push_back(domain.lock()->stop_wait());
                }

                for (auto& future : future_list) {
                    future.get();
                }
                future_list.clear();
            }

            {
                std::scoped_lock lck(node_mutex_);
                future_list.reserve(nodes_.size());
                for (EventHandlerBase* node : nodes_) {
                    future_list.push_back(node->stop_wait());
                }
                for (auto& future : future_list) {
                    future.get();
                }
            }
        }

        void remove_topic_domain(EventHandlerBase* ptr) {
            std::scoped_lock lck(mutex_);
            auto iter = std::remove_if(
                topic_domains_.begin(), topic_domains_.end(),
                [ptr](std::weak_ptr<EventHandlerBase>& p) {
                    auto domain_ptr = p.lock();
                    return !domain_ptr || ptr == domain_ptr.get();
                }
            );
            topic_domains_.erase(iter, topic_domains_.end());
        }

        void remove_node(EventHandlerBase* ptr) {
            std::scoped_lock lck(node_mutex_);
            auto iter = std::remove_if(
                nodes_.begin(), nodes_.end(),
                [ptr](EventHandlerBase* p) {
                    return !p || ptr == p;
                }
            );

            nodes_.erase(iter, nodes_.end());
        }

        // Private constructor to enforce the use of the static create method
        Core(int argc, char* argv[])
            : parent_t(max_queue_size){}


        std::atomic<bool>                             ok_{true};
        std::thread                                   core_thread_;

        // The destruction of nodes and domains is theoretically thread-safe.
        // Each domain/node must send a request before being destroyed.

        // Nodes are created by the invoker, so we only need to store a raw pointer
        // because the lifecycle is controlled by the invoker.
        std::vector<EventHandlerBase*>                nodes_;

        // Topic domains are created by the core. We want each pub/sub to leave,
        // so the domain can automatically deconstruct.
        std::vector<std::weak_ptr<EventHandlerBase>>  topic_domains_;

        std::mutex                                    mutex_;
        std::mutex                                    node_mutex_;
    };

    // Topic Domain is implicitly created by core
    template<class T>
    class TopicDomain : public TopicDomainBase, public EventHandler<TopicDomain<T>>
    {
        friend class Core;
    public:
        using queue_t  = lux::cxx::BlockingQueue<message_t<T>>;
        using parent_t = EventHandler<TopicDomain<T>>;

        template<typename U> friend class Publisher;
        template<typename U> friend class Subscriber;

        explicit TopicDomain(Core* core_ptr, const std::string_view topic_name)
            : TopicDomainBase(core_ptr, topic_name, lux::cxx::basic_type_info{std::in_place_type_t<T>{}}), 
            parent_t(max_queue_size)
        {
            std::thread([this](){parent_t::event_loop();}).swap(thread_);
        }

        ~TopicDomain(){
            parent_t::stop_event_handler();
            if (thread_.joinable()) {
                thread_.join();
            }

            auto event_payload = std::make_unique<DomainRequestPayload>();
            event_payload->object = this;
            auto future = static_cast<EventHandler<Core>*>(core_ptr_)->request<ECommunicationEvent::DomainClosed>(std::move(event_payload));
            future.get();
        }

        bool handle(const CommunicationEvent& event) {
            using PublisherCrtpType = CrtpPublisher<Publisher<T>, T>;
            auto payload_ptr = event.payload.get();
            switch (event.type) {
            case ECommunicationEvent::PublisherJoin: {
                auto payload   = static_cast<PublisherRequestPayload*>(payload_ptr);
                auto publisher = static_cast<Publisher<T>*>(payload->object);
                addPublisher(publisher);
                payload->promise.set_value();
                break;
            }
            case ECommunicationEvent::PublisherLeave: {
                auto payload   = static_cast<PublisherRequestPayload*>(payload_ptr);
                auto publisher = static_cast<Publisher<T>*>(payload->object);
                removePublisher(publisher);
                payload->promise.set_value();
                break;
            }
            case ECommunicationEvent::PublisherNewData: {
                auto payload   = static_cast<PublisherPayload*>(payload_ptr); 
                auto publisher = static_cast<PublisherCrtpType*>(payload->object);
                
                std::vector<message_t<T>> messages(queue_batch_size);
                size_t count = publisher->pop_bulk(messages, queue_batch_size);

                if (count > 0) {
                    distributeMessages(messages);
                }
                break;
            }
            case ECommunicationEvent::SubscriberJoin: {
                auto payload    = static_cast<SubscriberRequestPayload*>(payload_ptr);
                auto subscriber = static_cast<Subscriber<T>*>(payload->object);
                addSubscriber(subscriber);
                payload->promise.set_value();
                break;
            }
            case ECommunicationEvent::SubscriberLeave: {
                auto payload    = static_cast<SubscriberRequestPayload*>(payload_ptr);
                auto subscriber = static_cast<Subscriber<T>*>(payload->object);
                removeSubscriber(subscriber);
                payload->promise.set_value();
                break;
            }
            case ECommunicationEvent::Stop :{
                auto payload = static_cast<Stoppayload*>(payload_ptr);
                parent_t::stop();
                payload->promise.set_value();
                return false;
            }
            };
            return true;
        }

        bool isStop() const {
            return !core_ptr_->ok();
        }

    private:

        void distributeMessages(std::vector<message_t<T>>& messages) {
            using SubscriberType = CrtpSubscriber<Subscriber<T>, T>;

            if (subscribers_.empty()) {
                return;
            }

            if (subscribers_.size() == 1) {
                static_cast<SubscriberType*>(*subscribers_.begin())->push_bulk(messages);
                return;
            }

            for (auto* subscriber : subscribers_) {
                std::vector<message_t<T>> copied_messages;
                copied_messages.reserve(messages.size());
                for (auto& msg : messages) {
                    copied_messages.push_back(msg_copy_or_reference(msg));
                }
                static_cast<SubscriberType*>(subscriber)->push_bulk(copied_messages);
            }
        }

        void removeSubscriber(Subscriber<T>* ptr) {
            std::scoped_lock lck(mutex_);
            auto iter = std::find_if(
                subscribers_.begin(), subscribers_.end(),
                [ptr](Subscriber<T>* p) {
                    return ptr == p;
                }
            );

            subscribers_.erase(iter);
        }

        void removePublisher(Publisher<T>* ptr) {
            std::scoped_lock lck(mutex_);
            auto iter = std::find_if(
                publishers_.begin(), publishers_.end(),
                [ptr](Publisher<T>* p) {
                    return ptr == p;
                }
            );

            publishers_.erase(iter);
        }

        void addSubscriber(Subscriber<T>* ptr) {
            std::scoped_lock lck(mutex_);
            subscribers_.push_back(ptr);
        }

        void addPublisher(Publisher<T>* ptr) {
            std::scoped_lock lck(mutex_);
            publishers_.push_back(ptr);
        }

        std::mutex                              mutex_;
        std::thread                             thread_;
        std::vector<Publisher<T>*>              publishers_;
        std::vector<Subscriber<T>*>             subscribers_;
    };

} // namespace lux::communication

#endif // __CORE_HPP__
