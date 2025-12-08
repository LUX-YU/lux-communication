#include "lux/communication/builtin_msgs/pb_st_converter.hpp"
#include "lux/communication/builtin_msgs/common_msgs/vector5.st.h"
#include "lux/communication/builtin_msgs/common_msgs/vector5.pb.h"

namespace lux::communication::builtin_msgs
{
	namespace
	{
		using PBVector5 = lux::communication::builtin_msgs::common_msgs::Vector5;
		using STVector5 = lux::communication::builtin_msgs::common_msgs::Vector5S;
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::pb2st(const PBVector5& in, STVector5& out)
	{
		out[0] = in.x();
		out[1] = in.y();
		out[2] = in.z();
		out[3] = in.a();
		out[4] = in.b();
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::st2pb(const STVector5& in, PBVector5& out)
	{
		out.set_x(in[0]);
		out.set_y(in[1]);
		out.set_z(in[2]);
		out.set_a(in[3]);
		out.set_b(in[4]);
	}
}
