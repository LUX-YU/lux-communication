#pragma once

#include <string>
#include <memory>
#include <cassert>
#include <lux/communication/Domain.hpp>
#include <lux/communication/NodeBase.hpp>
#include <lux/communication/PublisherBase.hpp>
#include <lux/communication/visibility.h>
#include "Topic.hpp"

namespace lux::communication::intraprocess
{
    class Node;
    template <typename T>
    class Publisher : public lux::communication::PublisherBase
    {
        static TopicSptr getTopic(Node* node, const std::string& topic)
        {
            return node->domain().createOrGetTopic<Topic<T>, T>(topic);
        }

    public:
        Publisher(const std::string& topic, Node* node)
            : PublisherBase(getTopic(node, topic), node) { }

		~Publisher() = default;

        // Copy is disabled
        Publisher(const Publisher &) = delete;
        Publisher &operator=(const Publisher &) = delete;

        // Move is allowed
        Publisher(Publisher&& rhs) = delete;
        
        Publisher& operator=(Publisher&& rhs) = delete;

        // Publish a message with in-place construction
        template <class... Args>
        void emplace(Args&&... args)
        {
            auto ptr = std::make_shared<T>(std::forward<Args>(args)...);
            topicAs<Topic<T>>().publish(std::move(ptr));
        }

        // Send message, perfect forwarding
        template<typename U>
        void publish(U&& msg)
        {
            auto ptr = std::make_shared<T>(std::forward<U>(msg));
            topicAs<Topic<T>>().publish(std::move(ptr));
        }

        template<typename U>
        void publish(std::shared_ptr<U> msg)
        {
            topicAs<Topic<T>>().publish(std::move(msg));
        }
    };
}
