#pragma once

#include <vector>
#include <mutex>
#include <atomic>
#include <functional>
#include <memory>
#include <lux/communication/visibility.h>
#include <lux/communication/Domain.hpp>
#include "Publisher.hpp"
#include "Subscriber.hpp"
#include <lux/communication/Executor.hpp>

#include <lux/cxx/compile_time/type_info.hpp>
#include <lux/cxx/container/SparseSet.hpp>

namespace lux::communication::introprocess
{
    using lux::communication::Domain;
    using lux::communication::CallbackGroup;
    using lux::communication::CallbackGroupType;
    class Node
    {
    public:
        // Basic record structures remain the same
        struct PublisherRecord
        {
            int                       id;
            std::string               topic_name;
            lux::cxx::basic_type_info type_info;
        };

        struct SubscriberRecord
        {
            int                       id;
            std::string               topic_name;
            lux::cxx::basic_type_info type_info;
            std::function<void()>     spin_fn;
        };

        explicit Node(const std::string& nodeName, std::shared_ptr<Domain> domain)
            : node_name_(nodeName), domain_(domain), running_(false)
        {
            default_callback_group_ = std::make_shared<CallbackGroup>(CallbackGroupType::MutuallyExclusive);
        }

        ~Node()
        {
            stop();
        }

        int getDomainId() const { return domain_->getDomainId(); }
        const std::string& getName() const { return node_name_; }

        std::shared_ptr<CallbackGroup> getDefaultCallbackGroup() const
        {
            return default_callback_group_;
        }

        /**
         * @brief Create a Publisher
         *
         * Rewritten to use SparseSet for storing the PublisherRecord.
         * We continue to reuse publisher IDs via `free_pub_ids_`.
         */
        template <typename T>
        std::shared_ptr<Publisher<T>> createPublisher(const std::string& topic_name)
        {
            std::lock_guard<std::mutex> lock(mutex_pub_);

            // 1) Allocate or reuse a publisher ID
            int pub_id = 0;
            if (!free_pub_ids_.empty())
            {
                pub_id = free_pub_ids_.back();
                free_pub_ids_.pop_back();
            }
            else
            {
                // If no free IDs, use the size of the sparse set as a new ID
                pub_id = static_cast<int>(pub_set_.size());
            }

            // 2) Create or get the Topic from the Domain
            auto topic_ptr = domain_->createOrGetTopic<T>(topic_name);

            // 3) Construct the Publisher
            auto pub = std::make_shared<Publisher<T>>(this, pub_id, topic_ptr);

            // 4) Record in the SparseSet
            PublisherRecord pub_record{
                .id = pub_id,
                .topic_name = topic_name,
                .type_info = lux::cxx::make_basic_type_info<T>()
            };
            pub_set_.insert(pub_id, std::move(pub_record));

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
            std::lock_guard<std::mutex> lock(mutex_sub_);

            // 1) Allocate or reuse a subscriber ID
            int sub_id = 0;
            if (!free_sub_ids_.empty())
            {
                sub_id = free_sub_ids_.back();
                free_sub_ids_.pop_back();
            }
            else
            {
                // If no free IDs, use the size of the sparse set as a new ID
                sub_id = static_cast<int>(sub_set_.size());
            }

            // 2) Create or get the Topic from the Domain
            auto topic_ptr = domain_->createOrGetTopic<T>(topic_name);

            // 3) If no specific callback group provided, use the default
            if (!group)
            {
                group = default_callback_group_;
            }

            // 4) Construct the Subscriber
            auto sub = std::make_shared<Subscriber<T>>(this, sub_id, topic_ptr, std::forward<Func>(cb), group);

            // 5) Record in the SparseSet
            SubscriberRecord sub_record{
                .id = sub_id,
                .topic_name = topic_name,
                .type_info = lux::cxx::make_basic_type_info<T>(),
                .spin_fn = [sub_ptr = sub.get()]()
                {
                    sub_ptr->takeAll();
                }
            };
            sub_set_.insert(sub_id, std::move(sub_record));

            return sub;
        }

        /**
         * @brief Remove a Publisher by ID.
         *
         * Now we just call `erase` on the SparseSet and store the ID into free_pub_ids_.
         */
        void removePublisher(int pub_id)
        {
            std::lock_guard<std::mutex> lock(mutex_pub_);
            if (pub_set_.contains(pub_id))
            {
                pub_set_.erase(pub_id);
                free_pub_ids_.push_back(pub_id);
            }
        }

        /**
         * @brief Remove a Subscriber by ID.
         *
         * Same idea for the subscriber SparseSet.
         */
        void removeSubscriber(int sub_id)
        {
            std::lock_guard<std::mutex> lock(mutex_sub_);
            if (sub_set_.contains(sub_id))
            {
                sub_set_.erase(sub_id);
                free_sub_ids_.push_back(sub_id);
            }
        }

        // Stop the Node's activities (placeholder)
        void stop()
        {
            running_ = false;
        }

    private:
        // Basic Node info
        std::string             node_name_;
        std::shared_ptr<Domain> domain_;

        // We replace the raw vectors with two SparseSets, keyed by int ID:
        lux::cxx::SparseSet<int, PublisherRecord>  pub_set_;
        lux::cxx::SparseSet<int, SubscriberRecord> sub_set_;

        // We keep free_*_ids_ to reuse IDs exactly as before
        std::vector<int> free_pub_ids_;
        std::vector<int> free_sub_ids_;

        // Mutexes to protect the publisher/subscriber data structures
        std::mutex mutex_pub_;
        std::mutex mutex_sub_;

        // Example: Node "running" state
        std::atomic<bool> running_;

        // Default callback group
        std::shared_ptr<CallbackGroup> default_callback_group_;
    };


    template <typename T>
    Publisher<T>::~Publisher()
    {
        if (topic_)
        {
            if (node_)
            {
                node_->removePublisher(pub_id_);
            }
            topic_->decRef(); // Reduce reference to Topic
            topic_ = nullptr;
        }
    }

    template <typename T>
    void Publisher<T>::cleanup()
    {
        if (topic_)
        {
            if (node_)
            {
                node_->removePublisher(pub_id_);
                node_ = nullptr;
            }
            topic_->decRef();
            topic_ = nullptr;
        }
    }

    template <typename T>
    Subscriber<T>::~Subscriber()
    {
        cleanup();
    }

    template <typename T>
    void Subscriber<T>::cleanup()
    {
        if (topic_)
        {
            if (node_)
            {
                node_->removeSubscriber(sub_id_);
            }
            topic_->removeSubscriber(this);
            topic_->decRef();
            topic_ = nullptr;
        }
        close(queue_);

        if (callback_group_)
        {
            callback_group_->removeSubscriber(this);
        }
    }

}

inline void lux::communication::Executor::addNode(std::shared_ptr<lux::communication::introprocess::Node> node)
{
    if (!node) return;
    // For example, add the node's default callback group to the Executor
    auto default_group = node->getDefaultCallbackGroup();
    if (default_group)
    {
        addCallbackGroup(default_group);
    }
    // If you want to add all subscriber callback groups under the node more granularly,
    // you can implement more complex interfaces in Node.
}
