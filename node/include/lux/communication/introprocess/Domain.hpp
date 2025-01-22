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
#include "ITopicHolder.hpp"
#include "Topic.hpp"

namespace lux::communication::introprocess
{
    class Domain
    {
    public:
        explicit Domain(int domainId)
            : domain_id_(domainId)
        {
        }

        int getDomainId() const
        {
            return domain_id_;
        }

        /**
         * @brief 创建或获取指定名字、类型 T 的 Topic
         */
        template <typename T>
        Topic<T> *createOrGetTopic(const std::string &topicName)
        {
            std::lock_guard<std::mutex> lock(mutex_);

            auto key = std::make_pair(topicName, Topic<T>::type_info);
            auto it = topic_index_map_.find(key);
            if (it != topic_index_map_.end())
            {
                // 已存在
                size_t idx = it->second;
                auto ptr = dynamic_cast<Topic<T> *>(topics_[idx].get());
                assert(ptr && "Topic type mismatch!");
                ptr->incRef();
                return ptr;
            }
            else
            {
                // 创建新Topic
                auto new_topic = std::make_unique<Topic<T>>(topicName, this);
                new_topic->incRef(); // 初始拥有1个引用(由 Domain 管理)
                topics_.push_back(std::move(new_topic));
                size_t newIdx = topics_.size() - 1;
                topic_index_map_[key] = newIdx;

                return dynamic_cast<Topic<T> *>(topics_[newIdx].get());
            }
        }

        /**
         * @brief 当 Topic<T> 的引用计数归零时，通知 Domain 移除
         */
        void removeTopic(ITopicHolder *topicPtr)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = std::find_if(
                topics_.begin(), topics_.end(),
                [topicPtr](const std::unique_ptr<ITopicHolder> &p)
                {
                    return p.get() == topicPtr;
                }
            );

            if (it == topics_.end())
            {
                return; // 理论上不该发生
            }
            size_t idx_to_remove = std::distance(topics_.begin(), it);

            std::string name = (*it)->getTopicName();
            lux::cxx::basic_type_info ti = (*it)->getType();
            auto key = std::make_pair(name, ti);

            // 从 map 中移除
            auto map_it = topic_index_map_.find(key);
            if (map_it != topic_index_map_.end())
            {
                topic_index_map_.erase(map_it);
            }

            // swap-and-pop
            if (idx_to_remove != topics_.size() - 1)
            {
                // 把最后一个移动到 idx_to_remove
                std::swap(topics_[idx_to_remove], topics_.back());

                // 更新 map 中最后一个的索引
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

        // 存放所有Topic，使用 unique_ptr 管理其生命周期
        std::vector<std::unique_ptr<ITopicHolder>> topics_;

        using topic_key_t = std::pair<std::string, lux::cxx::basic_type_info>;

        // 自定义哈希函数
        struct PairHash
        {
            size_t operator()(const topic_key_t &pairKey) const
            {
                auto h1 = std::hash<std::string>{}(pairKey.first);
                auto h2 = pairKey.second.hash();
                return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
            }
        };

        // 自定义相等比较器
        struct PairEqual
        {
            bool operator()(const topic_key_t &lhs, const topic_key_t &rhs) const
            {
                return lhs.first == rhs.first && lhs.second == rhs.second;
            }
        };

        // 使用自定义哈希函数和比较器
        std::unordered_map<topic_key_t, size_t, PairHash, PairEqual> topic_index_map_;

        std::mutex mutex_; // 保护上述容器
    };

    // 由于 Topic<T>::onNoRef() 需要调用 domain->removeTopic(this)，
    template <typename T>
    void Topic<T>::onNoRef()
    {
        // 调用 domain->removeTopic(this)
        if (domain_)
        {
            domain_->removeTopic(this);
        }
    }
}