#include "lux/communication/builtin_msgs/pb_st_converter.hpp"
#include "lux/communication/builtin_msgs/sensor_msgs/image_group_stamped.st.h"
#include "lux/communication/builtin_msgs/sensor_msgs/image_group_stamped.pb.h"

namespace lux::communication::builtin_msgs
{
	namespace
	{
		using PBImageGroupStamped = builtin_msgs::sensor_msgs::ImageGroupStamped;
		using STImageGroupStamped = builtin_msgs::sensor_msgs::ImageGroupStampedS;
	}

	template<> void pb_st_converter::pb2st(const PBImageGroupStamped& in, STImageGroupStamped& out)
	{
		pb_st_converter::pb2st(in.timestamp(), out.timestamp);
		for (const auto& image : in.images())
		{
			out.images.emplace_back(image.width(), image.height(), image.channels(), (const void*)image.data().data());
		}
	}

	template<> void pb_st_converter::st2pb(const STImageGroupStamped& in, PBImageGroupStamped& out)
	{
		pb_st_converter::st2pb(in.timestamp, *out.mutable_timestamp());
		for (const auto& image : in.images)
		{
			auto* pbImage = out.add_images();
			pbImage->set_width(image.width());
			pbImage->set_height(image.height());
			pbImage->set_channels(image.channels());
			pbImage->mutable_data()->resize(image.width() * image.height() * image.channels());
			size_t image_size = image.width() * image.height() * image.channels();
			memcpy(
				pbImage->mutable_data()->data(),
				image.data(),
				image_size
			);
		}
	}
}
