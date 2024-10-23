#pragma once
#include <cstddef>

#include "lux/communication/introprocess/Core.hpp"
#include "lux/communication/introprocess/Node.hpp"

namespace lux::communication::introprocess{
    template<class T>
    class Publisher : public CrtpPublisher<Publisher<T>, T>{
        friend class CrtpPublisher<Publisher<T>, T>;
    public:
        using domain_t      = TopicDomain<T>;
        using domain_ptr_t  = std::shared_ptr<domain_t>;
        using parent_t      = CrtpPublisher<Publisher<T>, T>;
        using node_ptr_t    = typename parent_t::node_ptr_t;

        Publisher(node_ptr_t node, std::string_view topic, size_t queue_size)
            : parent_t(std::move(node), EPubSub::Publisher), max_size_(queue_size), queue_(queue_size) {
            auto& core = parent_t::node_->core();
            auto domain = core.getDomain<T>(topic);
            if (!domain) {
                domain = core.createDomain<T>(topic);
            }

            auto payload = std::make_unique<PublisherRequestPayload>();
            payload->object = this;
            auto future = domain->request<ECommunicationEvent::PublisherJoin>(std::move(payload));
            future.wait();

            domain_ = std::move(domain);
        }

        ~Publisher() override {
            queue_close(queue_);
            auto payload = std::make_unique<PublisherRequestPayload>();
            payload->object = this;

            auto future = domain_->request<ECommunicationEvent::PublisherLeave>(std::move(payload));

            future.get();
        };

        void pub(message_t<T> message) {
            if (!queue_push(queue_, std::move(message))) {
                return;
            }
            auto payload = std::make_unique<PublisherPayload>();
            payload->object = this;
            domain_->notify<ECommunicationEvent::PublisherNewData>(std::move(payload));
        }

        void pub_bulk(std::vector<message_t<T>>& messages) {
            if (messages.empty()) return;
            if (!queue_push_bulk(queue_, messages)) {
                return;
            }
            auto payload = std::make_unique<PublisherPayload>();
            payload->object = this;
            domain_->notify<ECommunicationEvent::PublisherNewData>(std::move(payload));
        }

        template<typename U>
        void pub(U&& message) {
            pub(make_message(T, std::forward<U>(message)));
        }

        const size_t capacity() const {
            return max_size_;
        }

        std::shared_ptr<TopicDomainBase> domain() override {
            return domain_;
        }

    private:

        // crtp
        size_t pop_bulk(std::vector<message_t<T>>& messages, size_t max_count) {
            return queue_pop_bulk(queue_, messages, max_count);
        }

        // crtp implement
        bool pop(message_t<T>& message) {
            return queue_try_pop(queue_, message);
        }

        domain_ptr_t          domain_;
        size_t                max_size_;
        queue_t<message_t<T>> queue_;
    };

    template<typename T> std::shared_ptr<Publisher<T>>
    Node::createPublisher(std::string_view topic, size_t queue_size) {
        auto new_publisher = std::make_shared<Publisher<T>>(
            this,
            topic, queue_size
        );

        return new_publisher;
    }
}