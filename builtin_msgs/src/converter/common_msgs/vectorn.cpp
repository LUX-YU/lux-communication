#include "lux/communication/builtin_msgs/pb_st_converter.hpp"
#include "lux/communication/builtin_msgs/common_msgs/vectorn.st.h"
#include "lux/communication/builtin_msgs/common_msgs/vectorn.pb.h"

namespace lux::communication::builtin_msgs
{
	namespace
	{
		using PBVectorN = lux::communication::builtin_msgs::common_msgs::VectorN;
		using STVectorN = lux::communication::builtin_msgs::common_msgs::VectorNS;
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::pb2st(const PBVectorN& in, STVectorN& out)
	{
		if (out.size() != in.data_size())
		{
			out.resize(in.data_size());
		}
		std::copy(in.data().begin(), in.data().end(), out.data());
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::st2pb(const STVectorN& in, PBVectorN& out)
	{
		if (out.data_size() != in.size())
		{
			out.mutable_data()->Resize(in.size(), 0);
		}
		std::copy(in.data(), in.data() + in.size(), out.mutable_data()->mutable_data());
	}
}
