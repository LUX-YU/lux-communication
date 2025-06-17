#pragma once
#include <functional>
#include <cstddef>
#include <cstdint>

namespace lux::communication
{
	struct TimeExecEntry
	{
		uint64_t timestamp_ns;
		std::function<void()> invoker;

		bool operator<(const TimeExecEntry& rhs) const;
	};
}