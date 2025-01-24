#pragma once
#include <functional>
#include <memory>
#include "Queue.hpp"
#include "Topic.hpp"
#include "SubscriberBase.hpp"
#include "CallbackGroup.hpp"

namespace lux::communication::introprocess
{
    template <typename T>
    using SubscriberCallback = std::function<void(std::unique_ptr<T, RcDeleter<T>>)>;

    template <typename T>
    struct SubscriberData
    {
        bool inUse{false};
        int nextFree{-1};
        SubscriberCallback<T> callback; // Callback
    };

    class Node; // Forward declaration
    template <typename T>
    class Subscriber : public ISubscriberBase
    {
    public:
        using Callback = std::function<void(const T &)>;

        friend class Node; // or friend class Node (depending on design)

        Subscriber(class Node *node, int sub_id, Topic<T> *topic, Callback cb, std::shared_ptr<CallbackGroup> callback_group)
            : node_(node), sub_id_(sub_id), topic_(topic), callback_(std::move(cb)), callback_group_(std::move(callback_group))
        {
            topic_->incRef();
            topic_->addSubscriber(this);
        }

        ~Subscriber();

        Subscriber(const Subscriber &) = delete;
        Subscriber &operator=(const Subscriber &) = delete;

        Subscriber(Subscriber &&rhs) noexcept
        {
            moveFrom(std::move(rhs));
        }

        Subscriber &operator=(Subscriber &&rhs) noexcept
        {
            if (this != &rhs)
            {
                cleanup();
                moveFrom(std::move(rhs));
            }
            return *this;
        }

        // The interface called by Topic when a new message arrives
        void enqueue(std::unique_ptr<T, RcDeleter<T>> msg)
        {
            push(queue_, std::move(msg));

            if (callback_group_ && setReadyIfNot())
            {
                callback_group_->notify(this);
            }
        }

        // Called by Node spinOnce()
        void takeAll() override
        {
            std::unique_ptr<T, RcDeleter<T>> msg;
            while (try_pop(queue_, msg))
            {
                if (callback_)
                {
                    callback_(*msg);
                }
            }

            clearReady();
        }

        int getId() const { return sub_id_; }

        bool setReadyIfNot() override
        {
            bool expected = false;
            // If originally false, set to true and return true
            // If already true, return false
            return ready_flag_.compare_exchange_strong(expected, true,
                    std::memory_order_acq_rel, std::memory_order_acquire);
        }

        void clearReady() override
        {
            ready_flag_.store(false, std::memory_order_release);
        }

    private:
        void cleanup();

        void moveFrom(Subscriber &&rhs)
        {
            node_      = rhs.node_;
            sub_id_    = rhs.sub_id_;
            topic_     = rhs.topic_;
            callback_  = std::move(rhs.callback_);

            rhs.node_  = nullptr;
            rhs.topic_ = nullptr;
            rhs.sub_id_ = -1;
        }

    private:
        class Node* node_{nullptr};
        int         sub_id_{-1};
        Topic<T>*   topic_{nullptr};
        Callback    callback_;
        std::atomic<bool> ready_flag_{false};
        
        std::shared_ptr<CallbackGroup> callback_group_;

        queue_t<T>  queue_;
    };
}
