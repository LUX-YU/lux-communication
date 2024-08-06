#include "lux/communication/builtin_msgs/pb_st_converter.hpp"
#include "lux/communication/builtin_msgs/sensor_msgs/camera_parameters.st.h"
#include "lux/communication/builtin_msgs/sensor_msgs/camera_parameters.pb.h"

namespace lux::communication::builtin_msgs
{
	namespace
	{
		using PBCameraParameters  = builtin_msgs::sensor_msgs::CameraParameters;
		using STCameraParametersS = builtin_msgs::sensor_msgs::CameraParametersS;
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::pb2st(const PBCameraParameters& in, STCameraParametersS& out)
	{
		pb_st_converter::pb2st(in.intrinsics(), out.intrinsics);
		pb_st_converter::pb2st(in.extrinsics(), out.extrinsics);
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::st2pb(const STCameraParametersS& in, PBCameraParameters& out)
	{
		pb_st_converter::st2pb(in.intrinsics, *out.mutable_intrinsics());
		pb_st_converter::st2pb(in.extrinsics, *out.mutable_extrinsics());
	}
}
