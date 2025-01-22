#pragma once

#include <string>
#include <memory>
#include <cassert>
#include "RcBuffer.hpp"
#include "Topic.hpp"

namespace lux::communication::introprocess
{
    class Node;
    template <typename T>
    class Publisher
    {
    public:
        friend class Node;

        // 只有 Node 能调用的构造
        Publisher(class Node *node, int pubId, Topic<T> *topic)
            : node_(node), pub_id_(pubId), topic_(topic)
        {
            assert(topic_);
            topic_->incRef(); // 对应在析构时 decRef()
        }

        ~Publisher();

        // 禁用拷贝
        Publisher(const Publisher &) = delete;
        Publisher &operator=(const Publisher &) = delete;

        // 允许移动
        Publisher(Publisher &&rhs) noexcept
        {
            moveFrom(std::move(rhs));
        }
        
        Publisher &operator=(Publisher &&rhs) noexcept
        {
            if (this != &rhs)
            {
                cleanup();
                moveFrom(std::move(rhs));
            }
            return *this;
        }

        // 发消息
        template <class... Args>
        void emplacePublish(Args&&... args)
        {
            if (topic_)
            {
                auto ptr = makeRcUnique<T>(std::forward<Args>(args)...);
                topic_->publish(std::move(ptr));
            }
        }

        // send message, perfect forwarding
        template<typename U>
        void publish(U&& msg)
        {
            if (topic_)
            {
                auto ptr = makeRcUnique<T>(std::forward<U>(msg));
                topic_->publish(std::move(ptr));
            }
        }

        // 获取本 Publisher ID
        int getId() const { return pub_id_; }

    private:
        void cleanup();

        void moveFrom(Publisher &&rhs)
        {
            node_ = rhs.node_;
            pub_id_ = rhs.pub_id_;
            topic_ = rhs.topic_;

            rhs.node_ = nullptr;
            rhs.topic_ = nullptr;
            rhs.pub_id_ = -1;
        }

    private:
        class Node* node_{nullptr};
        int         pub_id_{-1};
        Topic<T>*   topic_{nullptr};
    };
}