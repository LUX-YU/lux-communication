#pragma once
#include "lux/communication/builtin_msgs/geometry_msgs/transform.st.h"

namespace lux::communication::builtin_msgs::sensor_msgs
{
	struct ImuParametersS
	{
		geometry_msgs::TransformS	extrinsics;
		double						rate_hz;                     // IMU data rate [Hz]
		double						gyroscope_noise_density;     // Gyroscope noise density [rad/s/sqrt(Hz)]
		double						gyroscope_random_walk;       // Gyroscope random walk [rad/s^2/sqrt(Hz)]
		double						accelerometer_noise_density; // Accelerometer noise density [m/s^2/sqrt(Hz)]
		double						accelerometer_random_walk;   // Accelerometer random walk [m/s^3/sqrt(Hz)]
	};
}
