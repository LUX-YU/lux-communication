#pragma once
#include "lux/communication/builtin_msgs/common_msgs/vector3.st.h"

namespace lux::communication::builtin_msgs::geometry_msgs
{
	// linear velocity and angular velocity
	struct TwistS
	{
		STVector3	linear;
		STVector3	angular;
	};
}

#include "lux/communication/builtin_msgs/geometry_msgs/twist.pb.h"

namespace lux::communication
{
	namespace
	{
		using PBTwist = builtin_msgs::geometry_msgs::Twist;
		using STTwist = builtin_msgs::geometry_msgs::TwistS;
	}

	template<> void pb_st_converter::pb2st(const PBTwist& in, STTwist& out)
	{
		pb_st_converter::pb2st(in.linear(),  out.linear);
		pb_st_converter::pb2st(in.angular(), out.angular);
	}

	template<> void pb_st_converter::st2pb(const STTwist& in, PBTwist& out)
	{
		pb_st_converter::st2pb(in.linear,  *out.mutable_linear());
		pb_st_converter::st2pb(in.angular, *out.mutable_angular());
	}
}
