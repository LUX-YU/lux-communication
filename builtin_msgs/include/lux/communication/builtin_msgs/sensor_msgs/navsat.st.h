#pragma once
#include "lux/communication/builtin_msgs/common_msgs/matrix3.st.h"

namespace lux::communication::builtin_msgs::sensor_msgs
{
	enum class EStatus
	{
		NO_FIX = 0,
		FIX,
		SBAS_FIX,
		GBAS_FIX
	};

	enum class EService 
	{
		UNKNOWN = 0,
		GPS,
		GLONASS,
		BEIDOU,
		GALILEO
	};

	struct NavsatS
	{
		common_msgs::Matrix3S position_covariance;

		double  latitude;
		double  longitude;
		double  altitude;

		EStatus  status;
		EService service;
	};
}
