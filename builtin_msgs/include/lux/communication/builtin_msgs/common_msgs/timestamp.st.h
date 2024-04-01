#pragma once
#include <cstdint>

namespace lux::communication::builtin_msgs::common_msgs
{
	struct TimestampS
	{
		uint64_t secs;
		uint64_t nsecs;
	};
}

#include <lux/communication/pb_st_converter.hpp>
#include "lux/communication/builtin_msgs/common_msgs/timestamp.pb.h"

namespace lux::communication
{
	namespace
	{
		using PBTimestamp = lux::communication::builtin_msgs::common_msgs::Timestamp;
		using STTimestamp = lux::communication::builtin_msgs::common_msgs::TimestampS;
	}

	template<> void pb_st_converter::pb2st(const PBTimestamp& in, STTimestamp& out)
	{
		out.secs = in.secs();
		out.nsecs = in.nsecs();
	}

	template<> void pb_st_converter::st2pb(const STTimestamp& in, PBTimestamp& out)
	{
		out.set_secs(in.secs);
		out.set_nsecs(in.nsecs);
	}
}