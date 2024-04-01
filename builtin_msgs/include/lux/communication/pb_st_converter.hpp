#pragma once

namespace lux::communication
{
	struct pb_st_converter
	{
		template<typename IPB, typename OST> static void pb2st(const IPB&, OST&);
		template<typename IST, typename OPB> static void st2pb(const IST&, OPB&);
	};
}
