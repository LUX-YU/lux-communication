#pragma once
#include "imu.st.h"
#include "lux/communication/builtin_msgs/common_msgs/timestamp.st.h"

namespace lux::communication::builtin_msgs::sensor_msgs
{
	struct ImuStampedS
	{
		common_msgs::TimestampS timestamp;
		ImuS					imu;
	};
}

#include <lux/communication/pb_st_converter.hpp>
#include "lux/communication/builtin_msgs/sensor_msgs/imu_stamped.pb.h"

namespace lux::communication
{
	namespace
	{
		using PBImuStamped  = builtin_msgs::sensor_msgs::ImuStamped;
		using STImuStampedS = builtin_msgs::sensor_msgs::ImuStampedS;
	}

	template<> void pb_st_converter::pb2st(const PBImuStamped& in, STImuStampedS& out)
	{
		pb2st(in.timestamp(), out.timestamp);
		pb2st(in.imu(), out.imu);
	}

	template<> void pb_st_converter::st2pb(const STImuStampedS& in, PBImuStamped& out)
	{
		st2pb(in.timestamp, *out.mutable_timestamp());
		st2pb(in.imu, *out.mutable_imu());
	}
}
