#include "lux/communication/builtin_msgs/pb_st_converter.hpp"
#include "lux/communication/builtin_msgs/geometry_msgs/twist.st.h"
#include "lux/communication/builtin_msgs/geometry_msgs/twist.pb.h"

namespace lux::communication::builtin_msgs
{
	namespace
	{
		using PBTwist = builtin_msgs::geometry_msgs::Twist;
		using STTwist = builtin_msgs::geometry_msgs::TwistS;
	}

	template<> void pb_st_converter::pb2st(const PBTwist& in, STTwist& out)
	{
		pb_st_converter::pb2st(in.linear(), out.linear);
		pb_st_converter::pb2st(in.angular(), out.angular);
	}

	template<> void pb_st_converter::st2pb(const STTwist& in, PBTwist& out)
	{
		pb_st_converter::st2pb(in.linear, *out.mutable_linear());
		pb_st_converter::st2pb(in.angular, *out.mutable_angular());
	}
}
