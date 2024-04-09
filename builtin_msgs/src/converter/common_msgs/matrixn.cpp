#include "lux/communication/builtin_msgs/pb_st_converter.hpp"
#include "lux/communication/builtin_msgs/common_msgs/matrixn.st.h"
#include "lux/communication/builtin_msgs/common_msgs/matrixn.pb.h"

namespace lux::communication::builtin_msgs
{
	namespace
	{
		using PBMatrixN = lux::communication::builtin_msgs::common_msgs::MatrixN;
		using STMatrixN = lux::communication::builtin_msgs::common_msgs::MatrixNS;
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::pb2st(const PBMatrixN& in, STMatrixN& out)
	{
		if (out.rows() != in.rows() || out.cols() != in.rows())
		{
			out.resize(in.rows(), in.cols());
		}
		memcpy(out.data(), in.data().data(), in.data_size() * sizeof(double));
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::st2pb(const STMatrixN& in, PBMatrixN& out)
	{
		if (out.rows() != in.rows() || out.cols() != in.rows())
		{
			out.mutable_data()->Resize(in.rows(), in.cols());
		}
		out.mutable_data()->Assign(in.data(), in.data() + in.size());
	}
}
