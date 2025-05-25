#pragma once
#include <vector>
#include <memory>
#include <mutex>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <cassert>
#include <functional>
#include <lux/cxx/compile_time/type_info.hpp>
#include <lux/communication/visibility.h>
#include <lux/communication/intraprocess/ITopicHolder.hpp>
#include <lux/communication/intraprocess/Topic.hpp>

namespace lux::communication
{
    class LUX_COMMUNICATION_PUBLIC Domain
    {
    public:
        explicit Domain(int domainId);

        int getDomainId() const;

        /**
         * @brief Create or get a Topic of type T with the specified name
         */
        template <typename T>
        intraprocess::Topic<T> *createOrGetTopic(const std::string &topicName)
        {
            std::lock_guard<std::mutex> lock(mutex_);

            auto key = std::make_pair(topicName, intraprocess::Topic<T>::type_info);
            auto it = topic_index_map_.find(key);
            if (it != topic_index_map_.end())
            {
                // Already exists
                size_t idx = it->second;
                assert(topics_[idx]->getType() == intraprocess::Topic<T>::type_info && "Topic type mismatch!");
                auto ptr = static_cast<intraprocess::Topic<T> *>(topics_[idx].get());
                ptr->incRef();
                return ptr;
            }
            else
            {
                // Create a new Topic
                auto new_topic = std::make_unique<intraprocess::Topic<T>>(topicName, this);
                new_topic->incRef(); // Initially has 1 reference (managed by Domain)
                topics_.push_back(std::move(new_topic));
                size_t newIdx = topics_.size() - 1;
                topic_index_map_[key] = newIdx;

                return static_cast<intraprocess::Topic<T> *>(topics_[newIdx].get());
            }
        }

        /**
         * @brief When the reference count of Topic<T> reaches zero, notify the Domain to remove it
         */
        void removeTopic(intraprocess::ITopicHolder *topicPtr);

    private:
        int domain_id_;

        // Store all Topics, use unique_ptr to manage their lifetimes
        std::vector<std::unique_ptr<intraprocess::ITopicHolder>> topics_;

        using topic_key_t = std::pair<std::string, lux::cxx::basic_type_info>;

        // Custom hash function
        struct PairHash
        {
            size_t operator()(const topic_key_t &pairKey) const
            {
                auto h1 = std::hash<std::string>{}(pairKey.first);
                auto h2 = pairKey.second.hash();
                return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
            }
        };

        // Custom equality comparator
        struct PairEqual
        {
            bool operator()(const topic_key_t &lhs, const topic_key_t &rhs) const
            {
                return lhs.first == rhs.first && lhs.second == rhs.second;
            }
        };

        // Use custom hash function and comparator
        std::unordered_map<topic_key_t, size_t, PairHash, PairEqual> topic_index_map_;

        std::mutex mutex_; // Protects the above containers
    };

    // Because Topic<T>::onNoRef() needs to call domain->removeTopic(this),
    template <typename T>
    void intraprocess::Topic<T>::onNoRef()
    {
        // Call domain->removeTopic(this)
        if (domain_)
        {
            domain_->removeTopic(this);
        }
    }
}
