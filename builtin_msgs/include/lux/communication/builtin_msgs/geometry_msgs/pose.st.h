#pragma once
#include "point.st.h"
#include "quaternion.st.h"

namespace lux::communication::builtin_msgs::geometry_msgs
{
	struct PoseS
	{
		PointS		position;
		QuaternionS	orientation;
	};
}

#include "lux/communication/builtin_msgs/geometry_msgs/pose.pb.h"

namespace lux::communication
{
	namespace
	{
		using PBPose = builtin_msgs::geometry_msgs::Pose;
		using STPose = builtin_msgs::geometry_msgs::PoseS;
	}

	template<> void pb_st_converter::pb2st(const PBPose& in, STPose& out)
	{
		pb_st_converter::pb2st(in.position(),	 out.position);
		pb_st_converter::pb2st(in.orientation(), out.orientation);
	}

	template<> void pb_st_converter::st2pb(const STPose& in, PBPose& out)
	{
		pb_st_converter::st2pb(in.position,		*out.mutable_position());
		pb_st_converter::st2pb(in.orientation,	*out.mutable_orientation());
	}
}
