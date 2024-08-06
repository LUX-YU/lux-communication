#pragma once
#include "UdpMultiCast.hpp"
#include <array>
#include <atomic>
#include <thread>
#include <chrono>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <uuid.h>

namespace lux::communication
{
	static inline constexpr int				 default_listen_port		= 23000;
	static inline constexpr std::string_view default_multicast_group    = "239.255.0.1";
	static inline constexpr int				 expiry_time                = 5000;
	static inline constexpr int				 ping_interval              = 1000;

	enum ENodeActionType : uint16_t
	{
		Joined,
		Pinged,
		Left,
		LeftNoRecord,
		Expired,
		Unknown
	};

#pragma pack(push, 1)
	struct NodeInfo
	{
		std::array<uint8_t, 4>  header;
		uint16_t				version;
		std::array<uint8_t, 16> uuid;
		int64_t					port;
	};

	struct NodeAction
	{
		NodeInfo		info;
		ENodeActionType type;
	};
#pragma pack(pop)

	struct NodeInfoHash {
		size_t operator()(const NodeInfo& info) const
		{
			static std::hash<uuids::uuid> hash{};
			uuids::uuid id{info.uuid};
			return hash(id);
		}
	};

	static inline bool operator==(const NodeInfo& lhs, const NodeInfo& rhs)
	{
		return lhs.uuid == rhs.uuid;
	}

	class NodeGroup
	{
	public:
		NodeGroup(const NodeInfo& info, std::string_view group, int port)
			: _self_info(info)
		{
			_multicast = std::make_unique<UdpMultiCast>(group, port);
			_multicast->bind();
		}

		NodeGroup(const NodeInfo& info)
			: _self_info(info) 
		{
			_multicast = std::make_unique<UdpMultiCast>(default_multicast_group, default_listen_port);
			_multicast->bind();
		}

		~NodeGroup()
		{
			stop();
		}

		void start()
		{
			std::thread([this] { send_thread(); }).swap(_ping_thread);
			std::thread([this] { receive_thread(); }).swap(_receive_thread);
			_running = true;
		}

		void stop()
		{
			_running = false;
			if (_receive_thread.joinable())
				_receive_thread.join();
			if (_ping_thread.joinable())
				_ping_thread.join();
		}

		using NodeActionCallback = std::function<void(const NodeInfo&, ENodeActionType)>;

		template<class Func>
		void setOnNodeAction(Func&& func)
		{
			_on_node_action = std::forward<Func>(func);
		}

	private:
		void receive_thread()
		{
			char buffer[2048];
			std::memset(buffer, 0, sizeof(buffer));
			while(_running)
			{
				SockAddr addr;
				int len = _multicast->recvFrom(buffer, sizeof(buffer), addr);
				
				if(len != sizeof(NodeAction))
					continue;

				NodeAction* action = reinterpret_cast<NodeAction*>(buffer);

				switch(action->type)
				{
					case ENodeActionType::Pinged:
						handle_ping_action(action->info);
						break;
					case ENodeActionType::Left:
						handle_left_action(action->info);
						break;
					default:
						break;
				}
			}
		}

		void handle_ping_action(const NodeInfo& info)
		{
			std::lock_guard<std::mutex> lock(_mutex);
			if(_node_map.count(info) == 0)
			{
				_node_map.insert({info, std::chrono::high_resolution_clock::now().time_since_epoch().count()});
				if(_on_node_action)
					_on_node_action(info, ENodeActionType::Joined);
				return;
			}

			_node_map[info] = std::chrono::high_resolution_clock::now().time_since_epoch().count();
			_on_node_action(info, ENodeActionType::Pinged);
		}

		void handle_left_action(const NodeInfo& info)
		{
			std::lock_guard<std::mutex> lock(_mutex);
			if (_node_map.count(info) == 0)
			{
				if (_on_node_action)
					_on_node_action(info, ENodeActionType::LeftNoRecord);
				return;	
			}

			_node_map.erase(info);
			if(_on_node_action)
				_on_node_action(info, ENodeActionType::Left);
		}

		void send_thread()
		{
			NodeAction action;
			action.type = ENodeActionType::Pinged;
			action.info = _self_info;

			while(_running)
			{
				_multicast->send(reinterpret_cast<void*>(&action), sizeof(action));
				std::this_thread::sleep_for(std::chrono::milliseconds(ping_interval));
			}
		}

		using NodeMap = std::unordered_map<NodeInfo, uint64_t, NodeInfoHash>;

		NodeInfo						_self_info;
		std::atomic<bool>				_running{false};
		std::unique_ptr<UdpMultiCast>	_multicast;
		std::thread						_receive_thread;
		std::thread 					_ping_thread;
		std::mutex						_mutex;
		NodeMap							_node_map;
		NodeActionCallback				_on_node_action;
	};
}
