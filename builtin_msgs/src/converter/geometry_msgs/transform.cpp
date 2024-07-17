#include "lux/communication/builtin_msgs/pb_st_converter.hpp"
#include "lux/communication/builtin_msgs/geometry_msgs/transform.st.h"
#include "lux/communication/builtin_msgs/geometry_msgs/transform.pb.h"

namespace lux::communication::builtin_msgs
{
	namespace
	{
		using PBTransform = builtin_msgs::geometry_msgs::Transform;
		using STTransform = builtin_msgs::geometry_msgs::TransformS;
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::pb2st(const PBTransform& in, STTransform& out)
	{
		pb_st_converter::pb2st(in.translation(), out.translation);
		pb_st_converter::pb2st(in.rotation(), out.rotation);
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::st2pb(const STTransform& in, PBTransform& out)
	{
		pb_st_converter::st2pb(in.translation, *out.mutable_translation());
		pb_st_converter::st2pb(in.rotation, *out.mutable_rotation());
	}
}
