#include "lux/communication/builtin_msgs/pb_st_converter.hpp"
#include "lux/communication/builtin_msgs/sensor_msgs/navsat_stamped.st.h"
#include "lux/communication/builtin_msgs/sensor_msgs/navsat_stamped.pb.h"

namespace lux::communication::builtin_msgs
{
	namespace
	{
		using PBNavsatStamped = builtin_msgs::sensor_msgs::NavsatStamped;
		using STNavsatStamped = builtin_msgs::sensor_msgs::NavsatStampedS;
	}

	template<> void pb_st_converter::pb2st(const PBNavsatStamped& in, STNavsatStamped& out)
	{
		pb2st(in.timestamp(), out.timestamp);
		pb2st(in.navsat(), out.navsat);
	}

	template<> void pb_st_converter::st2pb(const STNavsatStamped& in, PBNavsatStamped& out)
	{
		st2pb(in.timestamp, *out.mutable_timestamp());
		st2pb(in.navsat, *out.mutable_navsat());
	}
}
