#pragma once

#include <vector>
#include <mutex>
#include <atomic>
#include <functional>
#include <memory>
#include <lux/communication/visibility.h>
#include <lux/communication/Domain.hpp>
#include <lux/communication/Executor.hpp>
#include <lux/communication/NodeBase.hpp>
#include "Publisher.hpp"
#include "Subscriber.hpp"


#include <lux/cxx/compile_time/type_info.hpp>
#include <lux/cxx/container/SparseSet.hpp>

namespace lux::communication::intraprocess
{
    using lux::communication::Domain;
    using lux::communication::CallbackGroup;
    using lux::communication::CallbackGroupType;

    class LUX_COMMUNICATION_PUBLIC Node 
        : public NodeBase, public std::enable_shared_from_this<Node>
    {
    public:
        explicit Node(
            const std::string& node_name, 
            std::shared_ptr<Domain> domain = default_domain()
        );

        ~Node();

        /**
         * @brief Create a Publisher
         *
         * Rewritten to use SparseSet for storing the PublisherRecord.
         * We continue to reuse publisher IDs via `free_pub_ids_`.
         */
        template <typename T>
        std::shared_ptr<Publisher<T>> createPublisher(const std::string& topic_name)
        {
            // 1) Create or get the Topic from the Domain
            auto topic_ptr = domain().createOrGetTopic<Topic<T>, T>(topic_name);

            // 2) Construct the Publisher
            auto pub = std::make_shared<Publisher<T>>(
                shared_from_this(),
                topic_ptr
            );

            return pub;
        }

        /**
         * @brief Create a Subscriber
         *
         * Rewritten to use SparseSet for storing the SubscriberRecord.
         * We continue to reuse subscriber IDs via `free_sub_ids_`.
         */
        template <typename T, typename Func>
        std::shared_ptr<Subscriber<T>> createSubscriber(
            const std::string& topic_name, Func&& cb, std::shared_ptr<CallbackGroup> group = nullptr)
        {
            // 1) Create or get the Topic from the Domain
            auto topic_ptr = domain().createOrGetTopic<Topic<T>, T>(topic_name);

            // 2) If no specific callback group provided, use the default
            if (!group)
            {
                group = default_callback_group();
            }

            // 4) Construct the Subscriber
            auto sub = std::make_shared<Subscriber<T>>(
                shared_from_this(),
                std::move(topic_ptr), 
                std::forward<Func>(cb), 
                group
            );
            
            addCallbackGroup(group.get());

            return sub;
        }

        // Stop the Node's activities (placeholder)
        void stop();

    private:
        // Example: Node "running" state
        std::atomic<bool> running_;
    };
}

