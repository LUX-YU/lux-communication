#include "lux/communication/builtin_msgs/pb_st_converter.hpp"
#include "lux/communication/builtin_msgs/common_msgs/vector2.st.h"
#include "lux/communication/builtin_msgs/common_msgs/vector2.pb.h"

namespace lux::communication::builtin_msgs
{
	namespace
	{
		using PBVector2 = lux::communication::builtin_msgs::common_msgs::Vector2;
		using STVector2 = lux::communication::builtin_msgs::common_msgs::Vector2S;
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::pb2st(const PBVector2& in, STVector2& out)
	{
		out[0] = in.x();
		out[1] = in.y();
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::st2pb(const STVector2& in, PBVector2& out)
	{
		out.set_x(in.x());
		out.set_y(in.y());
	}
}
