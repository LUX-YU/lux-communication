#pragma once
#include <Eigen/Geometry>

namespace lux::communication::builtin_msgs::geometry_msgs
{
	using QuaternionS = Eigen::Quaterniond;
}

#include <lux/communication/pb_st_converter.hpp>
#include "lux/communication/builtin_msgs/geometry_msgs/quaternion.pb.h"

namespace lux::communication
{
	namespace
	{
		using PBQuaternion = builtin_msgs::geometry_msgs::Quaternion;
		using STQuaternion = builtin_msgs::geometry_msgs::QuaternionS;
	}

	template<> void pb_st_converter::pb2st(const PBQuaternion& in, STQuaternion& out)
	{
		out = { in.w(), in.x(), in.y(), in.z()};
	}

	template<> void pb_st_converter::st2pb(const STQuaternion& in, PBQuaternion& out)
	{
		out.set_x(in.x());
		out.set_y(in.y());
		out.set_z(in.z());
		out.set_w(in.w());
	}
}
