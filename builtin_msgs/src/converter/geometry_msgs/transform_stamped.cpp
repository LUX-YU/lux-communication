#include "lux/communication/builtin_msgs/pb_st_converter.hpp"
#include "lux/communication/builtin_msgs/geometry_msgs/transform_stamped.st.h"
#include "lux/communication/builtin_msgs/geometry_msgs/transform_stamped.pb.h"

namespace lux::communication::builtin_msgs
{
	namespace
	{
		using PBTransformStamped = builtin_msgs::geometry_msgs::TransformStamped;
		using STTransformStamped = builtin_msgs::geometry_msgs::TransformStampedS;
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::pb2st(const PBTransformStamped& in, STTransformStamped& out)
	{
		pb_st_converter::pb2st(in.timestamp(), out.timestamp);
		pb_st_converter::pb2st(in.transform(), out.transform);
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::st2pb(const STTransformStamped& in, PBTransformStamped& out)
	{
		pb_st_converter::st2pb(in.timestamp, *out.mutable_timestamp());
		pb_st_converter::st2pb(in.transform, *out.mutable_transform());
	}
}