#pragma once

#include <vector>
#include <mutex>
#include <atomic>
#include <functional>
#include <memory>
#include "Domain.hpp"
#include "Publisher.hpp"
#include "Subscriber.hpp"
#include "Executor.hpp"

#include <lux/cxx/compile_time/type_info.hpp>
#include <exec/static_thread_pool.hpp>

namespace lux::communication::introprocess
{
    class Node
    {
    public:
        explicit Node(const std::string &nodeName, std::shared_ptr<Domain> domain)
            : node_name_(nodeName), domain_(domain), running_(false)
        {
            default_callback_group_ = std::make_shared<CallbackGroup>(CallbackGroupType::MutuallyExclusive);
        }

        ~Node()
        {
            stop();
        }

        int getDomainId() const { return domain_->getDomainId(); }
        const std::string &getName() const { return node_name_; }

        std::shared_ptr<CallbackGroup> getDefaultCallbackGroup() const
        {
            return default_callback_group_;
        }

        // 创建 Publisher
        template <typename T>
        std::shared_ptr<Publisher<T>> createPublisher(const std::string &topic_name)
        {
            std::lock_guard<std::mutex> lock(mutex_pub_);
            int pub_id = 0;
            if(!free_pub_ids_.empty())
            {
                pub_id = free_pub_ids_.back();
                free_pub_ids_.pop_back();
            }
            else
            {
                pub_id = (int)pub_sparse_index_.size();
                pub_sparse_index_.push_back(-1);
            }

            int dense_index = (int)pub_records_.size();

            // 在 domain 中创建或获取 topic
            auto topic_ptr = domain_->createOrGetTopic<T>(topic_name);

            // 构造 Publisher
            auto pub = std::make_shared<Publisher<T>>(this, pub_id, topic_ptr);

            // 记录到 Node 的 pub_records_ 中
            PublisherRecord pub_record{
                .id         = pub_id,
                .topic_name = topic_name,
                .type_info  = lux::cxx::make_basic_type_info<T>()
            };

            pub_records_.push_back(std::move(pub_record));
            pub_sparse_index_[pub_id] = dense_index;

            return pub;
        }

        // 创建 Subscriber
        template <typename T, typename Func>
        std::shared_ptr<Subscriber<T>> createSubscriber(
            const std::string &topic_name, Func&& cb, std::shared_ptr<CallbackGroup> group = nullptr)
        {
            std::lock_guard<std::mutex> lock(mutex_sub_);
            int sub_id = 0;
            if(!free_sub_ids_.empty())
            {
                sub_id = free_sub_ids_.back();
                free_sub_ids_.pop_back();
            }
            else
            {
                sub_id = (int)sub_sparse_index_.size();
                sub_sparse_index_.push_back(-1);
            }

            auto topic_ptr = domain_->createOrGetTopic<T>(topic_name);
            if (!group)
            {
                group = default_callback_group_;
            }

            auto sub = std::make_shared<Subscriber<T>>(this, sub_id, topic_ptr, std::forward<Func>(cb), std::move(group));

            // 记录到 Node 的 sub_records_
            SubscriberRecord sub_record{
                .id         = sub_id,
                .topic_name = topic_name,
                .type_info  = lux::cxx::make_basic_type_info<T>(),
                .spin_fn    = [sub_ptr = sub.get()]()
                    {
                        sub_ptr->takeAll();
                    }
            };

            sub_records_.push_back(std::move(sub_record));
            sub_sparse_index_[sub_id] = (int)sub_records_.size() - 1;

            return sub;
        }

        // 移除一个 Publisher
        void removePublisher(int pub_id)
        {
            std::lock_guard<std::mutex> lock(mutex_pub_);
            int dense_index = pub_sparse_index_[pub_id];
            assert(dense_index >= 0 && dense_index < (int)pub_records_.size());

            int last_index = (int)pub_records_.size() - 1;
            if(dense_index != last_index)
            {
                std::swap(pub_records_[dense_index], pub_records_[last_index]);
                int moved_id = pub_records_[dense_index].id;
                pub_sparse_index_[moved_id] = dense_index;
            }

            pub_records_.pop_back();
            pub_sparse_index_[pub_id] = -1;
            free_pub_ids_.push_back(pub_id);
        }

        // 移除一个 Subscriber
        void removeSubscriber(int sub_id)
        {
            std::lock_guard<std::mutex> lock(mutex_sub_);
            int dense_index = sub_sparse_index_[sub_id];
            assert(dense_index >= 0 && dense_index < (int)sub_records_.size());

            int last_index = (int)sub_records_.size() - 1;
            if(dense_index != last_index)
            {
                std::swap(sub_records_[dense_index], sub_records_[last_index]);
                int moved_id = sub_records_[dense_index].id;
                sub_sparse_index_[moved_id] = dense_index;
            }

            sub_records_.pop_back();
            sub_sparse_index_[sub_id] = -1;
            free_sub_ids_.push_back(sub_id);
        }

        // spinOnce：调用所有 sub 的 spin_fn
        // void spinOnce()
        // {
        //     // 为了减少锁的时间，先复制出一份 function
        //     std::vector<std::function<void()>> localSpin_fns;
        //     {
        //         std::lock_guard<std::mutex> lock(mutex_sub_);
        //         localSpin_fns.reserve(sub_records_.size());
        //         for (auto &r : sub_records_)
        //         {
        //             localSpin_fns.push_back(r.spin_fn);
        //         }
        //     }
        //     // 逐个调用
        //     for (auto &fn : localSpin_fns)
        //     {
        //         fn();
        //     }
        // }

        // // 简单的 spin
        // void spin()
        // {
        //     running_ = true;
        //     while (running_)
        //     {
        //         spinOnce();
        //     }
        // }

        void stop()
        {
            running_ = false;
        }

    private:
        struct PublisherRecord
        {
            int                         id;
            std::string                 topic_name;
            lux::cxx::basic_type_info   type_info;
        };

        struct SubscriberRecord
        {
            int                         id;
            std::string                 topic_name;
            lux::cxx::basic_type_info   type_info;
            // spinOnce时需要调用的函数
            std::function<void()>       spin_fn;
        };

    private:
        std::string                   node_name_;
        std::shared_ptr<Domain>       domain_;

        std::vector<SubscriberRecord> sub_records_;
        std::vector<int>              sub_sparse_index_;
        std::vector<int>              free_sub_ids_;

        std::vector<PublisherRecord>  pub_records_;
        std::vector<int>              pub_sparse_index_;
        std::vector<int>              free_pub_ids_;

        std::mutex                    mutex_pub_;
        std::mutex                    mutex_sub_;
        std::atomic<bool>             running_;

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
            topic_->decRef(); // 减少对 Topic 的引用
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
        if (topic_)
        {
            // 通知 Node 将我移除
            if (node_)
            {
                node_->removeSubscriber(sub_id_);
                node_ = nullptr;
            }
            topic_->removeSubscriber(this);
            topic_->decRef();
            topic_ = nullptr;
        }
        close(queue_);
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

    void Executor::addNode(std::shared_ptr<Node> node)
    {
        if (!node) return;
        // 比如把 node 的默认回调组添加到 Executor
        auto default_group = node->getDefaultCallbackGroup();
        if (default_group)
        {
            addCallbackGroup(default_group);
        }
        // 如果你想更细粒度地把该 node 下所有 subscriber 的
        // callbackGroup 都加进来，可以在 Node 里做更复杂的接口。
    }
}