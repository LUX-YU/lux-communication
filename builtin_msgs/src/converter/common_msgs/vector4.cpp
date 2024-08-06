#include "lux/communication/builtin_msgs/pb_st_converter.hpp"
#include "lux/communication/builtin_msgs/common_msgs/vector4.st.h"
#include "lux/communication/builtin_msgs/common_msgs/vector4.pb.h"

namespace lux::communication::builtin_msgs
{
	namespace
	{
		using PBVector4 = lux::communication::builtin_msgs::common_msgs::Vector4;
		using STVector4 = lux::communication::builtin_msgs::common_msgs::Vector4S;
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::pb2st(const PBVector4& in, STVector4& out)
	{
		out[0] = in.x();
		out[1] = in.y();
		out[2] = in.z();
		out[3] = in.w();
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::st2pb(const STVector4& in, PBVector4& out)
	{
		out.set_x(in.x());
		out.set_y(in.y());
		out.set_z(in.z());
		out.set_w(in.w());
	}
}
