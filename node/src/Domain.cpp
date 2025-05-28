#include "lux/communication/Domain.hpp"

namespace lux::communication {

    Domain::Domain(int domainId)
        : domain_id_(domainId) {}
    
    int Domain::getDomainId() const {
        return domain_id_;
    }
    
    void Domain::removeTopic(ITopicHolder* topic_ptr)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!topics_.contains(topic_ptr->index()))
        {
            return;
        }

        auto key = std::make_pair(
            topic_ptr->name(),
            topic_ptr->typeInfo()
        );

        topic_index_map_.erase(key);
		topics_.erase(topic_ptr->index());
    }
} // namespace lux::communication
