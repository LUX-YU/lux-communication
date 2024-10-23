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
        Core(const MakeSharedCoreHelper& helper, int argc, char* argv[])
            : Core(argc, argv){}

        virtual ~Core() {
            blocking_queue_close(leave_request_list_);
            if (ok_) {
                shutdown();
            }
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
            DomainLeaveRequest* request;
            while (ok_ && blocking_queue_pop(leave_request_list_, request)){
                std::scoped_lock lck(mutex_);
                removeTopicDomain(request->object);
                request->promise.set_value();
            }
        }

        std::future<void> domainLeaveReuest(TopicDomainBase* ptr) {
            DomainLeaveRequest request;
            request.object = ptr;
            auto future = request.promise.get_future();
            
            blocking_queue_push(leave_request_list_, &request);

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
        Core(int argc, char* argv[])
            :leave_request_list_(max_queue_size) {}


        std::atomic<bool>                             ok_{true};
        std::thread                                   core_thread_;

        blocking_queue_t<DomainLeaveRequest*>         leave_request_list_;
        std::vector<std::shared_ptr<TopicDomainBase>> topic_domains_;

        std::mutex                                    mutex_;
    };

    TopicDomainBase::~TopicDomainBase() {
        auto future = core_ptr_->domainLeaveReuest(this);
        future.wait();
    }

    template<class T>
    class TopicDomain : public TopicDomainBase, public EventHandler<TopicDomain<T>>
    {
    public:
        using queue_t  = lux::cxx::BlockingQueue<message_t<T>>;
        using parent_t = EventHandler<TopicDomain<T>>;

        template<typename U> friend class Publisher;
        template<typename U> friend class Subscriber;

        explicit TopicDomain(Core* core_ptr, const std::string_view topic_name)
            : TopicDomainBase(core_ptr, topic_name, lux::cxx::basic_type_info{ std::in_place_type_t<T>{} }), 
            parent_t(max_queue_size)
        {
            std::thread([this](){parent_t::event_loop();}).swap(thread_);
        }

        ~TopicDomain(){
            parent_t::stop_event_handler();
            if (thread_.joinable()) {
                thread_.join();
            }
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
