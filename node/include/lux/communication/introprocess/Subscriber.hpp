#pragma once
#include <cstddef>
#include <functional>

#include "lux/communication/introprocess/Core.hpp"
#include "lux/communication/introprocess/Node.hpp"

namespace lux::communication::introprocess {
    template<class T>
    class Subscriber : public CrtpSubscriber<Subscriber<T>, T>{
        friend class Node;
        friend class CrtpSubscriber<Subscriber<T>, T>;
    public:
        using domain_t      = TopicDomain<T>;
        using domain_ptr_t  = std::shared_ptr<domain_t>;
        using callback_t    = typename Node::callback_t<T>;
        using parent_t      = CrtpSubscriber<Subscriber<T>, T>;
        using node_ptr_t    = typename parent_t::node_ptr_t;

        Subscriber(node_ptr_t node, std::string_view topic, callback_t callback,  size_t queue_size)
            : parent_t(std::move(node)), callback_(std::move(callback)), max_size_(queue_size), queue_(queue_size) {
            auto& core = parent_t::node_->core();
            auto domain = core.template getDomain<T>(topic);
            if (!domain) {
                domain = core.template createDomain<T>(topic);
            }

            auto payload = std::make_unique<SubscriberRequestPayload>();
            payload->object = this;
            auto future = domain->template request<ECommunicationEvent::SubscriberJoin>(std::move(payload));
            future.wait();

            domain_ = std::move(domain);
        }

        ~Subscriber() override {
            queue_close(queue_);
            auto payload = std::make_unique<SubscriberRequestPayload>();
            payload->object = this;

            auto future = domain_->template request<ECommunicationEvent::SubscriberLeave>(std::move(payload));
            
            future.wait();
        };

        const size_t capacity() const {
            return max_size_;
        }

        std::shared_ptr<TopicDomainBase> domain() override {
            return domain_;
        }

        void popAndDoCallback() override {
            std::vector<message_t<T>> messages;
            size_t count = queue_pop_bulk(queue_, messages, queue_batch_size);
            if (count > 0 && callback_) {
                for (auto& message : messages) {
                    callback_(std::move(message));
                }
            }
        }

    private:
        void stop() override {
            queue_close(queue_);
        }

        // crtp
        void push_bulk(std::vector<message_t<T>>& messages) {
            queue_push_bulk(queue_, messages);
            auto payload = std::make_unique<SubscriberPayload>();
            payload->object = this;
            parent_t::node_->template notify<ECommunicationEvent::SubscriberNewData>(std::move(payload));
        }

        // crtp
        bool push(message_t<T> message) {
            if (queue_try_push(queue_, std::move(message))) {
                auto payload = std::make_unique<SubscriberPayload>();
                payload->object = this;
                parent_t::node_->template notify<ECommunicationEvent::SubscriberNewData>(std::move(payload));
            }

            return false;
        }

        callback_t                   callback_;
        domain_ptr_t                 domain_;
        size_t                       max_size_;
        queue_t<std::unique_ptr<T>>  queue_;
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