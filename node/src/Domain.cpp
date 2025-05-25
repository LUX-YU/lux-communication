#include "lux/communication/Domain.hpp"

namespace lux::communication {

Domain::Domain(int domainId)
    : domain_id_(domainId) {}

int Domain::getDomainId() const {
    return domain_id_;
}

void Domain::removeTopic(intraprocess::ITopicHolder *topicPtr)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(
        topics_.begin(), topics_.end(),
        [topicPtr](const std::unique_ptr<intraprocess::ITopicHolder> &p)
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

} // namespace lux::communication
