#pragma once

#include <string>
#include <memory>
#include <cassert>
#include <lux/communication/core/PublisherInterface.hpp>
#include <lux/communication/core/Domain.hpp>
#include <lux/communication/visibility.h>
#include "Topic.hpp"

namespace lux::communication::intraprocess
{
    class Node;

    template <typename T>
    class Publisher : public lux::communication::PublisherInterface
    {
    public:
        friend class Node;

        Publisher(const std::string& topic, Node& node)
            : Publisher(node, topic){}

		~Publisher() = default;

        Publisher(const Publisher &) = delete;
        Publisher &operator=(const Publisher &) = delete;

        Publisher(Publisher&& rhs) = delete;
        
        Publisher& operator=(Publisher&& rhs) = delete;

        template<typename U>
        void publish(U&& msg)
        {
            auto ptr = std::make_shared<T>(std::forward<U>(msg));
            topic().publish(std::move(ptr));
        }

        template<typename U>
        void publish(std::shared_ptr<U> msg_ptr)
        {
            topic().publish(std::move(msg_ptr));
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
