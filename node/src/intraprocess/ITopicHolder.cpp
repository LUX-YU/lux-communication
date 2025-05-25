#include "lux/communication/intraprocess/ITopicHolder.hpp"

namespace lux::communication::intraprocess {

    void ITopicHolder::incRef()
    {
        ref_count_.fetch_add(1, std::memory_order_relaxed);
    }
    
    void ITopicHolder::decRef()
    {
        ref_count_.fetch_sub(1, std::memory_order_acq_rel);
        if (ref_count_.load(std::memory_order_acquire) == 0)
        {
            onNoRef();
        }
    }
    
    int ITopicHolder::refCount() const
    {
        return ref_count_;
    }

} // namespace lux::communication::intraprocess
