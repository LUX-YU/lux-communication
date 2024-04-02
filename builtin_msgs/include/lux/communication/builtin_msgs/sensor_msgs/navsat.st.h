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

#include <lux/communication/pb_st_converter.hpp>
#include "lux/communication/builtin_msgs/sensor_msgs/navsat.pb.h"

namespace lux::communication
{
	namespace
	{
		using PBNavsat = builtin_msgs::sensor_msgs::Navsat;
		using STNavsat = builtin_msgs::sensor_msgs::NavsatS;
	}
	 
	template<> void pb_st_converter::pb2st(const PBNavsat& in, STNavsat& out)
	{
		pb2st(in.position_covariance(), out.position_covariance);
		out.latitude			= in.latitude();
		out.longitude			= in.longitude();
		out.altitude			= in.altitude();
		out.status				= static_cast<builtin_msgs::sensor_msgs::EStatus>(in.status());
		out.service				= static_cast<builtin_msgs::sensor_msgs::EService>(in.service());
	}
	 
	template<> void pb_st_converter::st2pb(const STNavsat& in, PBNavsat& out)
	{
		st2pb(in.position_covariance, *out.mutable_position_covariance());
		out.set_latitude(in.latitude);
		out.set_longitude(in.longitude);
		out.set_altitude(in.altitude);
		out.set_status(static_cast<::lux::communication::builtin_msgs::sensor_msgs::Navsat_Status>(in.status));
		out.set_service(static_cast<::lux::communication::builtin_msgs::sensor_msgs::Navsat_Service>(in.service));
	}
}
