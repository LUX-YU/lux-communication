#include "lux/communication/builtin_msgs/pb_st_converter.hpp"
#include "lux/communication/builtin_msgs/geometry_msgs/point.pb.h"
#include "lux/communication/builtin_msgs/geometry_msgs/point.st.h"

namespace lux::communication::builtin_msgs
{
	namespace
	{
		using PBPoint = builtin_msgs::geometry_msgs::Point;
		using STPoint = builtin_msgs::geometry_msgs::PointS;
	}

	template<> void pb_st_converter::pb2st(const PBPoint& in, STPoint& out)
	{
		out = { in.x() ,in.y() ,in.z() };
	}

	template<> void pb_st_converter::st2pb(const STPoint& in, PBPoint& out)
	{
		out.set_x(in.x());
		out.set_y(in.y());
		out.set_z(in.z());
	}
}
