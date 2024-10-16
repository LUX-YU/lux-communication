#pragma once
#include <cstddef>
#include <functional>

#include "lux/communication/introprocess/Core.hpp"
#include "lux/communication/introprocess/Node.hpp"

namespace lux::communication::introprocess {
    template<class T>
    class Subscriber : public SubscriberBase, public CrtpSubscriber<Subscriber<T>, T>{
        friend class Node;
    public:
        using domain_t      = TopicDomain<T>;
        using domain_ptr_t  = std::shared_ptr<domain_t>;

        using callback_t    = typename Node::callback_t<T>;

        Subscriber(node_ptr_t node, std::string_view topic, callback_t callback,  size_t queue_size)
            : SubscriberBase(std::move(node)), callback_(std::move(callback)), max_size_(queue_size), queue_(queue_size) {
            auto& core = node_->core();
            auto domain = core.getDomain<T>(topic);
            if (!domain) {
                domain = core.createDomain<T>(topic);
            }

            auto payload = std::make_unique<SubscriberRequestPayload<T>>();
            payload->object = this;
            auto future = domain->request<ETopicDomainEvent::SubscriberJoin>(std::move(payload));
            future.wait();

            domain_ = std::move(domain);
        }

        ~Subscriber() override{
            queue_.close();
            auto payload = std::make_unique<SubscriberRequestPayload<T>>();
            payload->object = this;
            
            auto future = domain_->request<ETopicDomainEvent::SubscriberLeave>(std::move(payload));
            
            future.wait();
        };

        const size_t capacity() const {
            return max_size_;
        }

        std::shared_ptr<TopicDomainBase> domain() override {
            return domain_;
        }

        bool push(message_t<T> message) {
            return queue_.try_push(std::move(message));
        }

        void popAndDoCallback() override {
            message_t<T> message;
            if(queue_.try_pop(message) && callback_) {
                callback_(std::move(message));
            }
        }

    private:
        callback_t                                   callback_;
        domain_ptr_t                                 domain_;
        size_t                                       max_size_;
        lux::cxx::BlockingQueue<std::unique_ptr<T>>  queue_;
    };

    template<typename T> std::shared_ptr<Subscriber<T>>
    Node::createSubscriber(std::string_view topic, callback_t<T> cb, size_t queue_size) {
        auto new_subscriber = std::make_shared<Subscriber<T>>(
            this,
            topic, std::move(cb), queue_size
        );

        return new_subscriber;
    }
}