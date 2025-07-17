#include <lux/communication/TimeExecEntry.hpp>

namespace lux::communication
{
    bool TimeExecEntry::operator<(const TimeExecEntry& rhs) const
    {
        return timestamp_ns > rhs.timestamp_ns;
    }
}