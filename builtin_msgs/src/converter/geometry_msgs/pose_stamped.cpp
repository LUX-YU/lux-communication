#include "lux/communication/builtin_msgs/pb_st_converter.hpp"
#include "lux/communication/builtin_msgs/geometry_msgs/pose_stamped.st.h"
#include "lux/communication/builtin_msgs/geometry_msgs/pose_stamped.pb.h"

namespace lux::communication::builtin_msgs
{
	namespace
	{
		using PBPoseStamped = builtin_msgs::geometry_msgs::PoseStamped;
		using STPoseStamped = builtin_msgs::geometry_msgs::PoseStampedS;
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::pb2st(const PBPoseStamped& in, STPoseStamped& out)
	{
		pb_st_converter::pb2st(in.timestamp(), out.timestamp);
		pb_st_converter::pb2st(in.pose(), out.pose);
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::st2pb(const STPoseStamped& in, PBPoseStamped& out)
	{
		pb_st_converter::st2pb(in.timestamp, *out.mutable_timestamp());
		pb_st_converter::st2pb(in.pose, *out.mutable_pose());
	}
}