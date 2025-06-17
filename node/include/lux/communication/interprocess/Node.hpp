#pragma once

#include <memory>
#include <string>

#include <vector>
#include <functional>
#include <atomic>

#include <lux/communication/NodeBase.hpp>
#include <lux/communication/CallbackGroup.hpp>
#include <lux/communication/interprocess/Publisher.hpp>
#include <lux/communication/interprocess/Subscriber.hpp>
#include <lux/communication/Executor.hpp>
#include <lux/communication/Domain.hpp>
#include <lux/communication/visibility.h>

namespace lux::communication::interprocess 
{
    class LUX_COMMUNICATION_PUBLIC Node
		: public lux::communication::TNodeBase<Node>, 
          public std::enable_shared_from_this<Node>
    {
    public:
        explicit Node(const std::string& name, std::shared_ptr<lux::communication::Domain> domain = default_domain());
    
        ~Node();
    
        template<typename T>
        std::shared_ptr<Publisher<T>> createPublisher(const std::string& topic)
        {
            return std::make_shared<Publisher<T>>(topic);
        }
    
        template<typename T, typename Callback>
        std::shared_ptr<Subscriber<T>> createSubscriber(
            const std::string& topic, Callback&& cb,
            std::shared_ptr<lux::communication::CallbackGroup> group = nullptr)
        {
            if (!group)
            {
                group = default_callback_group();
            }

			auto topic_ptr = domain_->createOrGetTopic<TopicBase, T>(topic);

            auto sub = std::make_shared<Subscriber<T>>(
                shared_from_this(),
                topic_ptr,
                std::forward<Callback>(cb), 
                group
            );
           
            subscriber_stoppers_.emplace_back([s=sub]{ s->stop(); });
            return sub;
        }
    
        void stop();
    
    private:
        std::string name_;
        std::vector<std::function<void()>> subscriber_stoppers_;
        int next_sub_id_{0};
    }; 

} // namespace lux::communication::interprocess
