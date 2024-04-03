#pragma once
#include "lux/communication/builtin_msgs/geometry_msgs/quaternion.st.h"
#include "lux/communication/builtin_msgs/common_msgs/matrix3.st.h"
#include "lux/communication/builtin_msgs/common_msgs/vector3.st.h"

namespace lux::communication::builtin_msgs::sensor_msgs
{
	struct ImuS
	{
		geometry_msgs::QuaternionS	orientation;
		common_msgs::Matrix3S		orientation_covariance;
		common_msgs::Vector3S		angular_velocity;
		common_msgs::Matrix3S		angular_velocity_covariance;
		common_msgs::Vector3S		linear_acceleration;
		common_msgs::Matrix3S		linear_acceleration_covariance;
	};
}
