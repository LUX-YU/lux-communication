#pragma once

#include <memory>
#include <string>

#include <vector>
#include <functional>
#include <atomic>

#include <lux/communication/CallbackGroup.hpp>
#include <lux/communication/interprocess/Publisher.hpp>
#include <lux/communication/interprocess/Subscriber.hpp>
#include <lux/communication/Executor.hpp>
#include <lux/communication/Domain.hpp>
#include <lux/communication/visibility.h>

namespace lux::communication::interprocess 
{
    class LUX_COMMUNICATION_PUBLIC Node 
    {
    public:
        explicit Node(const std::string& name, std::shared_ptr<lux::communication::Domain> domain);
    
        ~Node();
    
        int getDomainId() const;
    
        std::shared_ptr<lux::communication::CallbackGroup> getDefaultCallbackGroup() const;
    
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
                group = default_group_;
            }
            auto sub = std::make_shared<Subscriber<T>>(topic, std::forward<Callback>(cb), group, next_sub_id_++);
            if (group)
            {
                group->addSubscriber(sub.get());
                bool exists = false;
                for (auto& g : callback_groups_)
                {
                    if (g == group)
                    {
                        exists = true;
                        break;
                    }
                }
                if (!exists)
                {
                    callback_groups_.push_back(group);
                }
            }
            subscriber_stoppers_.emplace_back([s=sub]{ s->stop(); });
            return sub;
        }
    
        void stop();
    
    private:
        std::string name_;
        std::shared_ptr<lux::communication::CallbackGroup> default_group_;
        std::vector<std::shared_ptr<lux::communication::CallbackGroup>> callback_groups_;
        std::vector<std::function<void()>> subscriber_stoppers_;
        int next_sub_id_{0};
        std::shared_ptr<lux::communication::Domain> domain_;
    }; 

} // namespace lux::communication::interprocess
