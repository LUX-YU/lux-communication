#include "lux/communication/builtin_msgs/pb_st_converter.hpp"
#include "lux/communication/builtin_msgs/sensor_msgs/image_group_stamped.st.h"
#include "lux/communication/builtin_msgs/sensor_msgs/image_group_stamped.pb.h"

namespace lux::communication::builtin_msgs
{
	namespace
	{
		using STImage			  = builtin_msgs::sensor_msgs::ImageS;
		using PBImageGroupStamped = builtin_msgs::sensor_msgs::ImageGroupStamped;
		using STImageGroupStamped = builtin_msgs::sensor_msgs::ImageGroupStampedS;
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::pb2st(const PBImageGroupStamped& in, STImageGroupStamped& out)
	{
		pb_st_converter::pb2st(in.timestamp(), out.timestamp);
		for (const auto& image : in.images())
		{
			out.images.emplace_back(image.width(), image.height(), image.channels(), (const void*)image.data().data());
		}
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::st2pb(const STImageGroupStamped& in, PBImageGroupStamped& out)
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

	namespace
	{
		using STImagePairStamped = builtin_msgs::sensor_msgs::ImagePairStampedS;
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::pb2st(const PBImageGroupStamped& in, STImagePairStamped& out)
	{
		pb_st_converter::pb2st(in.timestamp(), out.timestamp);
		auto& images		= in.images();
		auto& first_image	= images[0];
		auto& second_image	= images[1];
		out.images[0] = STImage(first_image.width(), first_image.height(), first_image.channels(), (const void*)first_image.data().data());
		out.images[1] = STImage(second_image.width(), second_image.height(), second_image.channels(), (const void*)second_image.data().data());
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::st2pb(const STImagePairStamped& in, PBImageGroupStamped& out)
	{
		pb_st_converter::st2pb(in.timestamp, *out.mutable_timestamp());
		auto* images		= out.mutable_images();
		auto& first_image	= in.images[0];
		auto& second_image	= in.images[1];

		auto* pbImage = images->Add();
		pbImage->set_width(first_image.width());
		pbImage->set_height(first_image.height());
		pbImage->set_channels(first_image.channels());
		pbImage->mutable_data()->resize(first_image.width() * first_image.height() * first_image.channels());
		size_t image_size = first_image.width() * first_image.height() * first_image.channels();
		memcpy(
			pbImage->mutable_data()->data(),
			first_image.data(),
			image_size
		);

		pbImage = images->Add();
		pbImage->set_width(second_image.width());
		pbImage->set_height(second_image.height());
		pbImage->set_channels(second_image.channels());
		pbImage->mutable_data()->resize(second_image.width() * second_image.height() * second_image.channels());
		image_size = second_image.width() * second_image.height() * second_image.channels();
		memcpy(
			pbImage->mutable_data()->data(),
			second_image.data(),
			image_size
		);
	}
}
