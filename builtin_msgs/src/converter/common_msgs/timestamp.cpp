#include "lux/communication/builtin_msgs/common_msgs/timestamp.st.h"
#include "lux/communication/builtin_msgs/common_msgs/timestamp.pb.h"
#include "lux/communication/builtin_msgs/pb_st_converter.hpp"

namespace lux::communication::builtin_msgs::common_msgs
{
#define ONE_E_NINE 1000000000
	/*
	void timestamp_from_ns(common_msgs::Timestamp& timestamp, uint64_t time_ns)
	{
		timestamp.set_secs(time_ns / ONE_E_NINE);
		timestamp.set_nsecs(time_ns % ONE_E_NINE);
	}

	uint64_t timestamp_to_ns(const common_msgs::Timestamp& timestamp)
	{
		return timestamp.secs() * ONE_E_NINE + timestamp.nsecs();
	}
	*/

	void timestamp_from_ns(common_msgs::TimestampS& timestamp, uint64_t time_ns)
	{
		timestamp.secs = (time_ns / ONE_E_NINE);
		timestamp.nsecs = (time_ns % ONE_E_NINE);
	}

	uint64_t timestamp_to_ns(const common_msgs::TimestampS& timestamp)
	{
		return timestamp.secs * ONE_E_NINE + timestamp.nsecs;
	}
}

namespace lux::communication::builtin_msgs
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