#include "lux/communication/builtin_msgs/pb_st_converter.hpp"
#include "lux/communication/builtin_msgs/geometry_msgs/pose.st.h"
#include "lux/communication/builtin_msgs/geometry_msgs/pose.pb.h"

namespace lux::communication::builtin_msgs
{
	namespace
	{
		using PBPose = builtin_msgs::geometry_msgs::Pose;
		using STPose = builtin_msgs::geometry_msgs::PoseS;
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::pb2st(const PBPose& in, STPose& out)
	{
		pb_st_converter::pb2st(in.position(), out.position);
		pb_st_converter::pb2st(in.orientation(), out.orientation);
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::st2pb(const STPose& in, PBPose& out)
	{
		pb_st_converter::st2pb(in.position, *out.mutable_position());
		pb_st_converter::st2pb(in.orientation, *out.mutable_orientation());
	}
}
