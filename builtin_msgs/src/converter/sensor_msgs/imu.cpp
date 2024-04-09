#include "lux/communication/builtin_msgs/pb_st_converter.hpp"
#include "lux/communication/builtin_msgs/sensor_msgs/imu.st.h"
#include "lux/communication/builtin_msgs/sensor_msgs/imu.pb.h"

namespace lux::communication::builtin_msgs
{
	namespace
	{
		using PBImu = builtin_msgs::sensor_msgs::Imu;
		using STImuS = builtin_msgs::sensor_msgs::ImuS;
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::pb2st(const PBImu& in, STImuS& out)
	{
		pb_st_converter::pb2st(in.orientation(), out.orientation);
		pb_st_converter::pb2st(in.orientation_covariance(), out.orientation_covariance);
		pb_st_converter::pb2st(in.angular_velocity(), out.angular_velocity);
		pb_st_converter::pb2st(in.angular_velocity_covariance(), out.angular_velocity_covariance);
		pb_st_converter::pb2st(in.linear_acceleration(), out.linear_acceleration);
		pb_st_converter::pb2st(in.linear_acceleration_covariance(), out.linear_acceleration_covariance);
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::st2pb(const STImuS& in, PBImu& out)
	{
		pb_st_converter::st2pb(in.orientation, *out.mutable_orientation());
		pb_st_converter::st2pb(in.orientation_covariance, *out.mutable_orientation_covariance());
		pb_st_converter::st2pb(in.angular_velocity, *out.mutable_angular_velocity());
		pb_st_converter::st2pb(in.angular_velocity_covariance, *out.mutable_angular_velocity_covariance());
		pb_st_converter::st2pb(in.linear_acceleration, *out.mutable_linear_acceleration());
		pb_st_converter::st2pb(in.linear_acceleration_covariance, *out.mutable_linear_acceleration_covariance());
	}
}
