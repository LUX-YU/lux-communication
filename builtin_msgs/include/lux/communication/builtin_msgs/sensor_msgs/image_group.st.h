#pragma once
#include "image.st.h"
#include "lux/communication/builtin_msgs/common_msgs/timestamp.st.h"
#include "lux/communication/builtin_msgs/sensor_msgs/image_group.pb.h"
#include <vector>

namespace lux::communication::builtin_msgs::sensor_msgs
{
	using ImageGroupS = std::vector<ImageS>;
}

namespace lux::communication
{
	namespace
	{
		using PBImageGroup = builtin_msgs::sensor_msgs::ImageGroup;
		using STImageGroup = builtin_msgs::sensor_msgs::ImageGroupS;
	}

	template<> void pb_st_converter::pb2st(const PBImageGroup& in, STImageGroup& out)
	{
		out.clear();
		for(const auto& image : in.images())
		{
			out.emplace_back(image.width(), image.height(), image.channels(), (const void*)image.data().data());
		}
	}

	template<> void pb_st_converter::st2pb(const STImageGroup& in, PBImageGroup& out)
	{
		out.Clear();
		for(const auto& image : in)
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
