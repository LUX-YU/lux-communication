#pragma once
#include "lux/communication/builtin_msgs/common_msgs/vector3.st.h"

namespace lux::communication::builtin_msgs::geometry_msgs
{
	struct AccelS
	{
		common_msgs::Vector3S linear;
		common_msgs::Vector3S angular;
	};
}

#include <lux/communication/pb_st_converter.hpp>
#include "lux/communication/builtin_msgs/geometry_msgs/accel.pb.h"

namespace lux::communication
{
	namespace
	{
		using PBAccel = builtin_msgs::geometry_msgs::Accel;
		using STAccel = builtin_msgs::geometry_msgs::AccelS;
	}

	template<> void pb_st_converter::pb2st(const PBAccel& in, STAccel& out)
	{
		pb_st_converter::pb2st(in.angular(), out.angular);
		pb_st_converter::pb2st(in.linear(),  out.linear);
	}

	template<> void pb_st_converter::st2pb(const STAccel& in, PBAccel& out)
	{
		pb_st_converter::st2pb(in.angular, *out.mutable_angular());
		pb_st_converter::st2pb(in.linear,  *out.mutable_linear());
	}
}
