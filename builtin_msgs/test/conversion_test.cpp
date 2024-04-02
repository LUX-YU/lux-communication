#include "lux/communication/builtin_msgs/common_msgs/matrixn.st.h"

#include "lux/communication/builtin_msgs/geometry_msgs/accel.st.h"
#include "lux/communication/builtin_msgs/geometry_msgs/point.st.h"
#include "lux/communication/builtin_msgs/geometry_msgs/pose.st.h"
#include "lux/communication/builtin_msgs/geometry_msgs/quaternion.st.h"
#include "lux/communication/builtin_msgs/geometry_msgs/twist.st.h"

#include "lux/communication/builtin_msgs/sensor_msgs/image.st.h"
#include "lux/communication/builtin_msgs/sensor_msgs/image_stamped.st.h"
#include "lux/communication/builtin_msgs/sensor_msgs/image_group.st.h"
#include "lux/communication/builtin_msgs/sensor_msgs/image_group_stamped.st.h"

int main()
{
	using namespace ::lux::communication::builtin_msgs::sensor_msgs;
	ImageS image_st;
	Image  image_pb;

	lux::communication::pb_st_converter::st2pb(image_st, image_pb);

	return 0;
}