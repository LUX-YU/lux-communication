#include "lux/communication/builtin_msgs/pb_st_converter.hpp"
#include "lux/communication/builtin_msgs/sensor_msgs/image_stamped.st.h"
#include "lux/communication/builtin_msgs/sensor_msgs/image_stamped.pb.h"

namespace lux::communication::builtin_msgs
{
	namespace
	{
		using PBImageStamped = builtin_msgs::sensor_msgs::ImageStamped;
		using STImageStamped = builtin_msgs::sensor_msgs::ImageStampedS;
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::pb2st(const PBImageStamped& in, STImageStamped& out)
	{
		pb_st_converter::pb2st(in.image(), out.image);
		pb_st_converter::pb2st(in.timestamp(), out.timestamp);
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::st2pb(const STImageStamped& in, PBImageStamped& out)
	{
		pb_st_converter::st2pb(in.image, *out.mutable_image());
		pb_st_converter::st2pb(in.timestamp, *out.mutable_timestamp());
	}
}
