#include "lux/communication/SubscriberBase.hpp"

namespace lux::communication {

bool TimeExecEntry::operator<(const TimeExecEntry &rhs) const
{
    return timestamp_ns > rhs.timestamp_ns;
}

ISubscriberBase::ISubscriberBase(int id) : id_(id) {}

int ISubscriberBase::getId() const { return id_; }

} // namespace lux::communication
