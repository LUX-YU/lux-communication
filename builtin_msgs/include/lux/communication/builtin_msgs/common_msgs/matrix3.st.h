#pragma once
#include <Eigen/Core>
#include <lux/communication/pb_st_converter.hpp>
#include <lux/communication/builtin_msgs/common_msgs/matrix3.pb.h>

namespace lux::communication::builtin_msgs::common_msgs
{
	using Matrix3S = Eigen::Matrix3d;
}

namespace lux::communication
{
	namespace
	{
		using PBMatrix3 = lux::communication::builtin_msgs::common_msgs::Matrix3;
		using STMatrix3 = lux::communication::builtin_msgs::common_msgs::Matrix3S;
	}

	template<> void pb_st_converter::pb2st(const PBMatrix3& in, STMatrix3& out)
	{
		out(0, 0) = in.x1();
		out(0, 1) = in.y1();
		out(0, 2) = in.z1();

		out(1, 0) = in.x2();
		out(1, 1) = in.y2();
		out(1, 2) = in.z2();

		out(2, 0) = in.x3();
		out(2, 1) = in.y3();
		out(2, 2) = in.z3();
	}

	template<> void pb_st_converter::st2pb(const STMatrix3& in, PBMatrix3& out)
	{
		out.set_x1(in.coeff(0, 0));
		out.set_y1(in.coeff(0, 1));
		out.set_z1(in.coeff(0, 2));

		out.set_x2(in.coeff(1, 0));
		out.set_y2(in.coeff(1, 1));
		out.set_z2(in.coeff(1, 2));

		out.set_x3(in.coeff(2, 0));
		out.set_y3(in.coeff(2, 1));
		out.set_z3(in.coeff(2, 2));
	}
}