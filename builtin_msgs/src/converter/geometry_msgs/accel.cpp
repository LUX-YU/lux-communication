#include "lux/communication/builtin_msgs/pb_st_converter.hpp"
#include "lux/communication/builtin_msgs/geometry_msgs/accel.pb.h"
#include "lux/communication/builtin_msgs/geometry_msgs/accel.st.h"

namespace lux::communication::builtin_msgs
{
	namespace
	{
		using PBAccel = builtin_msgs::geometry_msgs::Accel;
		using STAccel = builtin_msgs::geometry_msgs::AccelS;
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::pb2st(const PBAccel& in, STAccel& out)
	{
		pb_st_converter::pb2st(in.angular(), out.angular);
		pb_st_converter::pb2st(in.linear(), out.linear);
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::st2pb(const STAccel& in, PBAccel& out)
	{
		pb_st_converter::st2pb(in.angular, *out.mutable_angular());
		pb_st_converter::st2pb(in.linear, *out.mutable_linear());
	}
}
