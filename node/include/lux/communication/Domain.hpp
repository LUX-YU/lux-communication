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
#include <lux/communication/introprocess/ITopicHolder.hpp>
#include <lux/communication/introprocess/Topic.hpp>

namespace lux::communication
{
    class Domain
    {
    public:
        explicit Domain(int domainId)
            : domain_id_(domainId){}

        int getDomainId() const
        {
            return domain_id_;
        }

        /**
         * @brief Create or get a Topic of type T with the specified name
         */
        template <typename T>
        introprocess::Topic<T> *createOrGetTopic(const std::string &topicName)
        {
            std::lock_guard<std::mutex> lock(mutex_);

            auto key = std::make_pair(topicName, introprocess::Topic<T>::type_info);
            auto it = topic_index_map_.find(key);
            if (it != topic_index_map_.end())
            {
                // Already exists
                size_t idx = it->second;
                auto ptr = dynamic_cast<introprocess::Topic<T> *>(topics_[idx].get());
                assert(ptr && "Topic type mismatch!");
                ptr->incRef();
                return ptr;
            }
            else
            {
                // Create a new Topic
                auto new_topic = std::make_unique<introprocess::Topic<T>>(topicName, this);
                new_topic->incRef(); // Initially has 1 reference (managed by Domain)
                topics_.push_back(std::move(new_topic));
                size_t newIdx = topics_.size() - 1;
                topic_index_map_[key] = newIdx;

                return dynamic_cast<introprocess::Topic<T> *>(topics_[newIdx].get());
            }
        }

        /**
         * @brief When the reference count of Topic<T> reaches zero, notify the Domain to remove it
         */
        void removeTopic(introprocess::ITopicHolder *topicPtr)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = std::find_if(
                topics_.begin(), topics_.end(),
                [topicPtr](const std::unique_ptr<introprocess::ITopicHolder> &p)
                {
                    return p.get() == topicPtr;
                }
            );

            if (it == topics_.end())
            {
                return; // Theoretically should not happen
            }
            size_t idx_to_remove = std::distance(topics_.begin(), it);

            std::string name = (*it)->getTopicName();
            lux::cxx::basic_type_info ti = (*it)->getType();
            auto key = std::make_pair(name, ti);

            // Remove from map
            auto map_it = topic_index_map_.find(key);
            if (map_it != topic_index_map_.end())
            {
                topic_index_map_.erase(map_it);
            }

            // swap-and-pop
            if (idx_to_remove != topics_.size() - 1)
            {
                // Move the last one to idx_to_remove
                std::swap(topics_[idx_to_remove], topics_.back());

                // Update the index of the last one in the map
                std::string moved_name            = topics_[idx_to_remove]->getTopicName();
                lux::cxx::basic_type_info movedTi = topics_[idx_to_remove]->getType();
                auto moved_key = std::make_pair(moved_name, movedTi);

                auto moved_map_it = topic_index_map_.find(moved_key);
                if (moved_map_it != topic_index_map_.end())
                {
                    moved_map_it->second = idx_to_remove;
                }
            }
            topics_.pop_back();
        }

    private:
        int domain_id_;

        // Store all Topics, use unique_ptr to manage their lifetimes
        std::vector<std::unique_ptr<introprocess::ITopicHolder>> topics_;

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
    void introprocess::Topic<T>::onNoRef()
    {
        // Call domain->removeTopic(this)
        if (domain_)
        {
            domain_->removeTopic(this);
        }
    }
}
