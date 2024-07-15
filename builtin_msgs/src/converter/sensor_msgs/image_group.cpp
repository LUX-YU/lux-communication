#include "lux/communication/builtin_msgs/pb_st_converter.hpp"
#include "lux/communication/builtin_msgs/sensor_msgs/image_group.st.h"
#include "lux/communication/builtin_msgs/sensor_msgs/image_group.pb.h"

namespace lux::communication::builtin_msgs
{
	namespace
	{
		using STImage      = builtin_msgs::sensor_msgs::ImageS;
		using PBImageGroup = builtin_msgs::sensor_msgs::ImageGroup;
		using STImageGroup = builtin_msgs::sensor_msgs::ImageGroupS;
	}

	// Image Group
	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::pb2st(const PBImageGroup& in, STImageGroup& out)
	{
		out.clear();
		for (const auto& image : in.images())
		{
			out.emplace_back(image.width(), image.height(), image.channels(), (const void*)image.data().data());
		}
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::st2pb(const STImageGroup& in, PBImageGroup& out)
	{
		out.Clear();
		for (const auto& image : in)
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

	namespace
	{
		using STImagePair = builtin_msgs::sensor_msgs::ImagePairS;
	}

	// Image Pair
	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::pb2st(const PBImageGroup& in, STImagePair& out)
	{
		auto& images = in.images();
		auto& first_image = images[0];
		auto& second_image = images[1];
		out[0] = STImage(first_image.width(), first_image.height(), first_image.channels(), (const void*)first_image.data().data());
		out[1] = STImage(second_image.width(), second_image.height(), second_image.channels(), (const void*)second_image.data().data());
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::st2pb(const STImagePair& in, PBImageGroup& out)
	{
		out.Clear();
		auto* pbImage = out.add_images();
		pbImage->set_width(in[0].width());
		pbImage->set_height(in[0].height());
		pbImage->set_channels(in[0].channels());
		pbImage->mutable_data()->resize(in[0].width() * in[0].height() * in[0].channels());
		size_t image_size = in[0].width() * in[0].height() * in[0].channels();
		memcpy(
			pbImage->mutable_data()->data(),
			in[0].data(),
			image_size
		);

		pbImage = out.add_images();
		pbImage->set_width(in[1].width());
		pbImage->set_height(in[1].height());
		pbImage->set_channels(in[1].channels());
		pbImage->mutable_data()->resize(in[1].width() * in[1].height() * in[1].channels());
		image_size = in[1].width() * in[1].height() * in[1].channels();
		memcpy(
			pbImage->mutable_data()->data(),
			in[1].data(),
			image_size
		);
	}
}
