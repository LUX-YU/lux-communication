#pragma once
#include <zmq.hpp>
#include <uuid.h>
#include <cassert>
#include <lux/communication/NodeGroup.hpp>
#include <lux/cxx/concurrent/ThreadPool.hpp>

namespace lux::communication
{
	static bool has_instance = false;
	class NodeImpl
	{
	public:
		NodeImpl(std::string_view node, int argc, char* argv[], size_t thread_num)
			: _name(node), _context(thread_num), _thread_pool(thread_num)
		{
			assert(!has_instance);
			has_instance = true;
			init_info(_info);
			_group = std::make_unique<NodeGroup>(_info);
		}

		~NodeImpl() = default;

	private:
		static void init_info(NodeInfo& info)
		{
			info.header		 = { 'L', 'U', 'X', 'N' };
			info.version	 = 1;
			uuids::uuid const id = uuids::uuid_system_generator{}();
			assert(!id.is_nil());
			assert(id.version() == uuids::uuid_version::random_number_based);
			assert(id.variant() == uuids::uuid_variant::rfc);
			assert(sizeof(NodeInfo::uuid) == id.as_bytes().size());
			std::memcpy(info.uuid.data(), id.as_bytes().data(), info.uuid.size());
		}

		NodeInfo					_info;
		zmq::context_t				_context;
		std::string					_name;
		std::unique_ptr<NodeGroup>	_group;
		lux::cxx::ThreadPool		_thread_pool;
	};
}
