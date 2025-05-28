#pragma once

#include <string>
#include <memory>
#include <cassert>
#include <lux/communication/PublisherBase.hpp>
#include <lux/communication/visibility.h>
#include "Topic.hpp"

namespace lux::communication::intraprocess
{
    class Node;
    template <typename T>
    class Publisher : public lux::communication::PublisherBase
    {
    public:
        friend class Node;

        // Only Node can call this constructor
        Publisher(std::shared_ptr<Node> node, TopicHolderSptr topic)
            : PublisherBase(std::move(node), std::move(topic)){}

		~Publisher() = default;

        // Copy is disabled
        Publisher(const Publisher &) = delete;
        Publisher &operator=(const Publisher &) = delete;

        // Move is allowed
        Publisher(Publisher&& rhs) = delete;
        
        Publisher& operator=(Publisher&& rhs) = delete;

        // Publish a message with in-place construction
        template <class... Args>
        void emplacePublish(Args&&... args)
        {
            auto ptr = std::make_shared<T>(std::forward<Args>(args)...);
            topic().publish(std::move(ptr));
        }

        // Send message, perfect forwarding
        template<typename U>
        void publish(U&& msg)
        {
            auto ptr = std::make_shared<T>(std::forward<U>(msg));
            topic().publish(std::move(ptr));
        }

    private:

		Topic<T>& topic()
		{
			return static_cast<Topic<T>&>(PublisherBase::topic());
		}

		const Topic<T>& topic() const
		{
			return static_cast<const Topic<T>&>(PublisherBase::topic());
		}
    };
}
