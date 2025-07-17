#include "lux/communication/Domain.hpp"
#include "lux/communication/TopicBase.hpp"

namespace lux::communication {

    Domain::Domain(size_t id)
        : id_(id) {}
    
    
    void Domain::removeTopic(TopicBase* topic_ptr)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!topics_.contains(topic_ptr->idInDoamin()))
        {
            return;
        }

        auto key = std::make_pair(
            topic_ptr->name(),
            topic_ptr->typeInfo()
        );

        topic_index_map_.erase(key);
		topics_.erase(topic_ptr->idInDoamin());
        topic_ptr->setIdInDoamin(std::numeric_limits<size_t>::max());
    }
} // namespace lux::communication
