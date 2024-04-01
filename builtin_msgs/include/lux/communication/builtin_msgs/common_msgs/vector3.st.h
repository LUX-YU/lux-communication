#pragma once
#include <Eigen/Core>

namespace lux::communication::builtin_msgs::common_msgs
{
	using Vector3S = Eigen::Vector3d;
}

#include <lux/communication/pb_st_converter.hpp>
#include "lux/communication/builtin_msgs/common_msgs/vector3.pb.h"

namespace lux::communication
{
	namespace
	{
		using PBVector3 = lux::communication::builtin_msgs::common_msgs::Vector3;
		using STVector3 = lux::communication::builtin_msgs::common_msgs::Vector3S;
	}

	template<> void pb_st_converter::pb2st(const PBVector3& in, STVector3& out)
	{
		out[0] = in.x();
		out[1] = in.y();
		out[2] = in.z();
	}

	template<> void pb_st_converter::st2pb(const STVector3& in, PBVector3& out)
	{
		out.set_x(in.x());
		out.set_y(in.y());
		out.set_z(in.z());
	}
}
