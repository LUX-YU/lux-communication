#pragma once
#include "point.st.h"
#include "quaternion.st.h"

namespace lux::communication::builtin_msgs::geometry_msgs
{
	struct PoseS
	{
		PointS		position;
		QuaternionS	orientation;

		Eigen::Matrix4d SE3() const
		{
			Eigen::Matrix4d mat = Eigen::Matrix4d::Identity();
			mat.block<3, 1>(0, 3) = position;
			mat.block<3, 3>(0, 0) = orientation.toRotationMatrix();
			return mat;
		}
	};
}
