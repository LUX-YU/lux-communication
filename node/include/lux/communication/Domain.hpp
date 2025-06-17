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
#include <lux/cxx/container/SparseSet.hpp>
#include <lux/communication/visibility.h>

namespace lux::communication
{
    class TopicBase;
    class LUX_COMMUNICATION_PUBLIC Domain
    {
    public:
        explicit Domain(size_t id);

        size_t id() const
        {
            return id_;
        }

        /**
         * @brief Create or get a Topic of type T with the specified name
         */
		template <typename TopicType, typename T, typename... Args>
        requires std::is_base_of_v<TopicBase, TopicType>
        std::shared_ptr<TopicType> createOrGetTopic(const std::string &topicName, Args&&... args)
        {
            std::lock_guard<std::mutex> lock(mutex_);

			constexpr auto type_info = lux::cxx::make_basic_type_info<T>();
            auto key = std::make_pair(topicName, type_info);
            auto it = topic_index_map_.find(key);
            if (it != topic_index_map_.end())
            {
                // Already exists
                size_t idx = it->second;
                auto ptr = std::static_pointer_cast<TopicType>(topics_[idx].lock());
                return ptr;
            }

            // Create a new Topic
            auto new_topic = std::make_shared<TopicType>(std::forward<Args>(args)...);
			new_topic->setDomain(this); // Set the Domain for the Topic
			new_topic->setTopicName(topicName);
			new_topic->setTypeInfo(type_info);
            auto topic_idx = topics_.insert(new_topic);
            new_topic->setIndex(topic_idx);
            topic_index_map_[key] = topic_idx;
            return new_topic;
        }

        /**
         * @brief When the reference count of Topic<T> reaches zero, notify the Domain to remove it
         */
        void removeTopic(TopicBase* topicPtr);

        static inline Domain& default_domain()
        {
            static Domain default_domain_instance{ 0 };
            return default_domain_instance;
        }

    private:
        size_t id_;

        // Store all Topics
        lux::cxx::AutoSparseSet<std::weak_ptr<TopicBase>> topics_;

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

        mutable std::mutex mutex_; // Protects the above containers
    };
}
