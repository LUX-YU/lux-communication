#include "lux/communication/builtin_msgs/pb_st_converter.hpp"
#include "lux/communication/builtin_msgs/sensor_msgs/imu_parameters.st.h"
#include "lux/communication/builtin_msgs/sensor_msgs/imu_parameters.pb.h"

namespace lux::communication::builtin_msgs
{
	namespace
	{
		using PBImuParameters  = builtin_msgs::sensor_msgs::ImuParameters;
		using STImuParametersS = builtin_msgs::sensor_msgs::ImuParametersS;
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::pb2st(const PBImuParameters& in, STImuParametersS& out)
	{
		pb_st_converter::pb2st(in.extrinsics(), out.extrinsics);
		out.gyroscope_noise_density = in.gyroscope_noise_density();
		out.gyroscope_random_walk = in.gyroscope_random_walk();
		out.accelerometer_noise_density = in.accelerometer_noise_density();
		out.accelerometer_random_walk = in.accelerometer_random_walk();

	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::st2pb(const STImuParametersS& in, PBImuParameters& out)
	{
		pb_st_converter::st2pb(in.extrinsics, *out.mutable_extrinsics());
		out.set_gyroscope_noise_density(in.gyroscope_noise_density);
		out.set_gyroscope_random_walk(in.gyroscope_random_walk);
		out.set_accelerometer_noise_density(in.accelerometer_noise_density);
		out.set_accelerometer_random_walk(in.accelerometer_random_walk);
	}
}
