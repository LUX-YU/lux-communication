#pragma once
#include <cstddef>

#include "lux/communication/introprocess/Core.hpp"
#include "lux/communication/introprocess/Node.hpp"

namespace lux::communication::introprocess{
    template<class T>
    class Publisher : public PubSubBase, public CrtpPublisher<Publisher<T>, T>{
        friend class IntroProcessNode;
    public:
        using domain_t      = TopicDomain<T>;
        using domain_ptr_t  = std::shared_ptr<domain_t>;

        Publisher(node_ptr_t node, std::string_view topic, size_t queue_size)
            : PubSubBase(std::move(node), EPubSub::Publisher), max_size_(queue_size), queue_(queue_size) {
            auto& core = node_->core();
            auto domain = core.getDomain<T>(topic);
            if (!domain) {
                domain = core.createDomain<T>(topic);
            }

            auto payload = std::make_unique<PublisherRequestPayload<T>>();
            payload->object = this;
            auto future = domain->request<ETopicDomainEvent::PublisherJoin>(std::move(payload));
            future.wait();

            domain_ = std::move(domain);
        }

        ~Publisher() override {
            queue_.close();
            auto payload = std::make_unique<PublisherRequestPayload<T>>();
            payload->object = this;

            auto future = domain_->request<ETopicDomainEvent::PublisherLeave>(std::move(payload));

            future.wait();
        };

        void pub(message_t<T> message) {
            queue_.push(std::move(message));
        }

        template<typename U>
        void pub(U&& message) {
            if (!queue_.push(make_message(T, std::forward<U>(message)))) {
                return;
            }
            auto payload = std::make_unique<PublisherPayload<T>>();
            payload->object = this;
            domain_->template notify<ETopicDomainEvent::PublisherNewData>(std::move(payload));
        }

        // crtp implement, do not invoke
        bool pop(message_t<T>& message) {
            return queue_.try_pop(message);
        }

        const size_t capacity() const {
            return max_size_;
        }

        std::shared_ptr<TopicDomainBase> domain() override {
            return domain_;
        }

    private:
        domain_ptr_t                                domain_;
        size_t                                      max_size_;
        lux::cxx::BlockingQueue<message_t<T>>       queue_;
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