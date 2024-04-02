#pragma once
#include "image.st.h"
#include "lux/communication/builtin_msgs/common_msgs/timestamp.st.h"
#include "lux/communication/builtin_msgs/sensor_msgs/image_stamped.pb.h"

namespace lux::communication::builtin_msgs::sensor_msgs
{
	struct ImageStampedS
	{
		common_msgs::TimestampS timestamp;
		ImageS					image;
	};
}

namespace lux::communication
{
	namespace
	{
		using PBImageStamped = builtin_msgs::sensor_msgs::ImageStamped;
		using STImageStamped = builtin_msgs::sensor_msgs::ImageStampedS;
	}

	template<> void pb_st_converter::pb2st(const PBImageStamped& in, STImageStamped& out)
	{
		pb_st_converter::pb2st(in.image(), out.image);
		pb_st_converter::pb2st(in.timestamp(), out.timestamp);
	}

	template<> void pb_st_converter::st2pb(const STImageStamped& in, PBImageStamped& out)
	{
		pb_st_converter::st2pb(in.image, *out.mutable_image());
		pb_st_converter::st2pb(in.timestamp, *out.mutable_timestamp());
	}
}
