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
#include <condition_variable>

#include <lux/communication/visibility.h>
#include <lux/cxx/compile_time/type_info.hpp>
#include <lux/cxx/container/HeterogeneousLookup.hpp>
#include <lux/cxx/concurrent/BlockingQueue.hpp>
#include <lux/cxx/compile_time/type_info.hpp>
#include <lux/cxx/concurrent/ThreadPool.hpp>

#include "lux/communication/introprocess/Config.hpp"

namespace lux::communication::introprocess {

    class Core;
    class TopicDomainBase {
    public:
        TopicDomainBase(Core* core, std::string_view topic_name, lux::cxx::basic_type_info info)
            : core_ptr_(core), type_info_(info), topic_name_(topic_name) {}

        virtual ~TopicDomainBase();

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
    class Core {
    protected:
        struct MakeSharedCoreHelper{};
        friend class TopicDomainBase;
    public:
        Core(const MakeSharedCoreHelper& helper, int argc, char* argv[], size_t thread_num)
            : Core(argc, argv, thread_num){}

        virtual ~Core() {
            if (ok_) {
                shutdown();
            }
            if (core_thread_.joinable()) {
                core_thread_.join();
            }
        }

        // Static factory method to create a shared pointer to a new Core instance.
        [[nodiscard]] static std::shared_ptr<Core> create(int argc, char* argv[], size_t thread_num = 5) {
            return std::make_shared<Core>(MakeSharedCoreHelper{}, argc, argv, thread_num);
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
                [topic_name](std::shared_ptr<TopicDomainBase>& ptr) {
                    return ptr->topic_name() == topic_name;
                }
            );

            if (iter == topic_domains_.end()) {
                return nullptr;
            }

            auto result = *iter;

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
                [topic_name](std::shared_ptr<TopicDomainBase>& ptr) {
                    return ptr->topic_name() == topic_name;
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
            std::thread(
                [this]() {core_loop(); }
            ).swap(core_thread_);
        }

        void shutdown() {
            ok_ = false;
        }

    private:

        void core_loop() {
            while (ok_){
                DomainLeaveRequest* request;
                if (!leave_request_list_.empty()) {
                    std::scoped_lock lck(mutex_);
                    while (leave_request_list_.pop(request)) {
                        removeTopicDomain(request->object);
                        request->promise.set_value();
                    }
                }
            }
        }

        std::future<void> domainLeaveReuest(TopicDomainBase* ptr) {
            DomainLeaveRequest request;
            request.object = ptr;
            auto future = request.promise.get_future();
            
            leave_request_list_.push(&request);

            return future;
        }

        void removeTopicDomain(TopicDomainBase* ptr) {
            auto iter = std::find_if(
                topic_domains_.begin(), topic_domains_.end(),
                [ptr](std::shared_ptr<TopicDomainBase>& p) {
                    return ptr == p.get();
                }
            );

            topic_domains_.erase(iter);
        }

        // Private constructor to enforce the use of the static create method
        Core(int argc, char* argv[], size_t thread_num)
            :thread_pool_(thread_num){}


        std::atomic<bool>               ok_{true};
        std::thread                     core_thread_;
        lux::cxx::ThreadPool            thread_pool_;

        lux::cxx::BlockingQueue<DomainLeaveRequest*>  leave_request_list_;
        std::vector<std::shared_ptr<TopicDomainBase>> topic_domains_;

        // A heterogeneous map that stores shared pointers to TopicDomainBase.
        // The map is keyed by topic names and allows storage of TopicDomains of different types.
        // lux::cxx::heterogeneous_map<std::weak_ptr<TopicDomainBase>> topic_domains;
        // domain mutex
        std::mutex                      mutex_;
    };

    TopicDomainBase::~TopicDomainBase() {
        auto future = core_ptr_->domainLeaveReuest(this);
        future.wait();
    }

    template<class Derived, typename T>
    class CrtpPublisher {
    public:
        bool pop(message_t<T>& message) {
            return static_cast<Derived*>(this)->pop(message);
        }
    };

    template<class Derived, typename T>
    class CrtpSubscriber{
    public:
        bool push(message_t<T> message) {
            return static_cast<Derived*>(this)->push(std::move(message));
        }
    };

    // Topic Events
    enum class ETopicDomainEvent {
        PublisherLeave,
        PublisherJoin,
        PublisherNewData,
        SubscriberLeave,
        SubscriberJoin
    };

    template<typename T> class Publisher;
    template<typename T> class Subscriber;

    struct EventPayload{};
    template<typename T> struct PublisherPayload  : EventPayload{Publisher<T>*  object{ nullptr };};
    template<typename T> struct SubscriberPayload : EventPayload{Subscriber<T>* object{ nullptr };};

    template<typename T> struct PublisherRequestPayload : PublisherPayload<T> {std::promise<void> promise;};
    template<typename T> struct SubscriberRequestPayload: SubscriberPayload<T>{std::promise<void> promise;};

    template<typename T, ETopicDomainEvent E> struct PayloadTypeMap;
    template<typename T> struct PayloadTypeMap<T, ETopicDomainEvent::PublisherJoin>   { using type = PublisherRequestPayload<T>; };
    template<typename T> struct PayloadTypeMap<T, ETopicDomainEvent::PublisherLeave>  { using type = PublisherRequestPayload<T>; };
    template<typename T> struct PayloadTypeMap<T, ETopicDomainEvent::PublisherNewData>{ using type = PublisherPayload<T>; };
    template<typename T> struct PayloadTypeMap<T, ETopicDomainEvent::SubscriberJoin>  { using type = SubscriberRequestPayload<T>; };
    template<typename T> struct PayloadTypeMap<T, ETopicDomainEvent::SubscriberLeave> { using type = SubscriberRequestPayload<T>; };

    struct TopicDomainEvent {
        ETopicDomainEvent               type;
        std::unique_ptr<EventPayload>   payload;
    };

    template<class T>
    class TopicDomain : public TopicDomainBase
    {
    public:
        using queue_t = lux::cxx::BlockingQueue<message_t<T>>;

        template<typename U> friend class Publisher;
        template<typename U> friend class Subscriber;

        explicit TopicDomain(Core* core_ptr, const std::string_view topic_name)
            : TopicDomainBase(core_ptr, topic_name, lux::cxx::basic_type_info{ std::in_place_type_t<T>{} }) {
            std::thread(
                [this]() {
                    tick();
                }
            ).swap(thread_);
        }

        ~TopicDomain() {
            event_queue_.close();
            if (thread_.joinable()) {
                thread_.join();
            }
        }

        void tick(){
            TopicDomainEvent domain_event;
            if (!event_queue_.empty()) {
                while (event_queue_.pop(domain_event)) {
                    handleEvents(domain_event);
                }
            }
        }

    private:
        void handleEvents(const TopicDomainEvent& domain_event) {
            using PublisherType = CrtpPublisher<Publisher<T>, T>;
            auto payload_ptr = domain_event.payload.get();
            switch (domain_event.type) {
            case ETopicDomainEvent::PublisherJoin: {
                auto payload = static_cast<PublisherRequestPayload<T>*>(payload_ptr);
                addPublisher(payload->object);
                payload->promise.set_value();
                break;
            }
            case ETopicDomainEvent::PublisherLeave: {
                auto payload = static_cast<PublisherRequestPayload<T>*>(payload_ptr);
                removePublisher(payload->object);
                payload->promise.set_value();
                break;
            }
            case ETopicDomainEvent::PublisherNewData: {
                auto payload = static_cast<PublisherPayload<T>*>(payload_ptr);
                auto pub_ptr = static_cast<PublisherType*>(payload->object);
                message_t<T> message;
                if (pub_ptr->pop(message)) {
                    distributeMessage(std::move(message));
                }
                break;
            }
            case ETopicDomainEvent::SubscriberJoin: {
                auto payload = static_cast<SubscriberRequestPayload<T>*>(payload_ptr);
                addSubscriber(payload->object);
                payload->promise.set_value();
                break;
            }
            case ETopicDomainEvent::SubscriberLeave: {
                auto payload = static_cast<SubscriberRequestPayload<T>*>(payload_ptr);
                removeSubscriber(payload->object);
                payload->promise.set_value();
                break;
            }
            };
        }

        template<ETopicDomainEvent E>
        void notify(std::unique_ptr<typename PayloadTypeMap<T, E>::type> payload) {
            TopicDomainEvent event{
                .type = E,
                .payload = std::move(payload)
            };

            event_queue_.push(std::move(event));
        }

        template<ETopicDomainEvent E>
        std::future<void> request(std::unique_ptr<typename PayloadTypeMap<T, E>::type> payload) {
            auto future = payload->promise.get_future();
            
            TopicDomainEvent event{
                .type    = E,
                .payload = std::move(payload)
            };

            // if core is ok, push event to queue
            if (core_ptr_->ok()) {
                event_queue_.push(std::move(event));
            }
            else{
                // if core is down, handle event directly
                handleEvents(event);
            }

            return future;
        }

        void distributeMessage(message_t<T> message) {
            using SubscriberType = CrtpSubscriber<Subscriber<T>, T>;
            for (auto* subscriber : subscribers_) {
                auto new_msg = msg_copy_or_reference(message);
                static_cast<SubscriberType*>(subscriber)->push(std::move(new_msg));
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

        std::mutex                                  mutex_;
        std::thread                                 thread_;
        std::vector<Publisher<T>*>                  publishers_;
        std::vector<Subscriber<T>*>                 subscribers_;
        lux::cxx::BlockingQueue<TopicDomainEvent>   event_queue_;
    };

} // namespace lux::communication

#endif // __CORE_HPP__
