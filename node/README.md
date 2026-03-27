# lux-communication / node

## 概述

`node` 模块是 **lux-communication** 的核心，实现了一套 **统一的发布/订阅通信框架**。它能透明地在三种传输层之间自动选择路由：

| 传输层 | 适用场景 | 机制 |
|--------|---------|------|
| **Intra-process** | 同进程内的 Publisher ↔ Subscriber | 零拷贝 `shared_ptr` / 值传递 |
| **SHM (共享内存)** | 同机器、跨进程 | SPSC 环形缓冲 + 大消息池 |
| **Network (网络)** | 跨机器 | UDP (散射聚集 / 分片) + TCP (握手 / 心跳) |

所有传输路径最终汇聚到同一个有序消息队列中，由可配置的 Executor 调度回调执行。

---

## 使用说明

### 快速开始

只需包含三个头文件即可使用核心功能：

```cpp
#include <lux/communication/Node.hpp>                          // Node
#include <lux/communication/executor/SingleThreadedExecutor.hpp> // Executor
```

**最小示例 — 进程内发布/订阅：**

```cpp
#include <iostream>
#include <lux/communication/Node.hpp>
#include <lux/communication/executor/SingleThreadedExecutor.hpp>

int main() {
    namespace comm = lux::communication;

    // 1. 创建节点（使用默认 Domain）
    comm::Node node("my_node");

    // 2. 创建 Publisher 和 Subscriber
    auto pub = node.createPublisher<int>("/counter");
    auto sub = node.createSubscriber<int>("/counter",
        [](const int& value) {
            std::cout << "Received: " << value << "\n";
        });

    // 3. 创建 Executor 并驱动回调
    comm::SingleThreadedExecutor executor;
    executor.addNode(&node);
    std::thread spin_thread([&] { executor.spin(); });

    // 4. 发布消息
    for (int i = 0; i < 10; ++i)
        pub->publish(i);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    executor.stop();
    spin_thread.join();
    node.stop();
}
```

---

### CMake 集成

```cmake
find_package(lux_communication REQUIRED)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE lux::communication::node)
```

如需使用 builtin 消息类型（如 `ImuStampedS`、`ImageStampedS`），额外链接：

```cmake
target_link_libraries(my_app PRIVATE lux::communication::builtin_msgs)
```

---

### 核心 API

#### Node — 创建节点

```cpp
// 使用默认 Domain（所有默认节点在同一个 Domain 中互相可见）
comm::Node node("node_name");

// 使用自定义 Domain（不同 Domain 的节点互相隔离）
comm::Domain domain(1);
comm::Node node("node_name", domain);

// 使用自定义选项
comm::NodeOptions opts;
opts.enable_shm = false;       // 禁用共享内存传输
opts.enable_net = false;       // 禁用网络传输（纯进程内）
comm::Node node("node_name", domain, opts);
```

#### Publisher — 发布消息

```cpp
// 创建
auto pub = node.createPublisher<MyMsg>("topic/name");

// 三种发布方式
pub->publish(msg);                                // 值传递（SmallValueMsg 类型自动优化）
pub->publish(std::make_shared<MyMsg>(args...));   // 零拷贝：shared_ptr 直传给 Subscriber
pub->emplace(arg1, arg2);                         // 就地构造，避免拷贝
```

#### Subscriber — 订阅消息

回调签名根据消息类型自动推导：

```cpp
// SmallValueMsg 类型（≤128B trivially-copyable）：回调参数为 const T&
auto sub = node.createSubscriber<int>("topic/name",
    [](const int& value) { /* 处理 */ });

auto sub = node.createSubscriber<double>("sensor/data",
    [](const double& value) { /* 处理 */ });

// 非 SmallValueMsg 类型（如含 std::string 的结构体）：回调参数为 shared_ptr<T>
struct MyMsg { std::string text; int id; };
auto sub = node.createSubscriber<MyMsg>("topic/name",
    [](std::shared_ptr<MyMsg> msg) { /* 处理 */ });
```

#### Executor — 驱动回调执行

```cpp
// 单线程 — 最低延迟
comm::SingleThreadedExecutor exec;
exec.addNode(&node);
exec.spin();       // 阻塞直到 exec.stop()

// 多线程 — 高吞吐
comm::MultiThreadedExecutor exec(4);   // 4 个工作线程
exec.addNode(&node);
exec.spin();

// 非阻塞轮询（适合集成到已有事件循环）
while (running) {
    exec.spinSome();   // 处理当前就绪的回调，立即返回
}
```

---

### 进阶用法

#### Domain 隔离

不同 Domain ID 的节点完全隔离，互不可见：

```cpp
comm::Domain domain_sensor(1);
comm::Domain domain_control(2);

comm::Node sensor_node("sensor", domain_sensor);
comm::Node control_node("control", domain_control);

// sensor_node 上的 Publisher 发布的消息
// 不会被 control_node 上的 Subscriber 接收到
```

#### 多节点通信

同一 Domain 内的多个节点可互相通信：

```cpp
comm::Domain domain(1);
comm::Node node_a("node_a", domain);
comm::Node node_b("node_b", domain);

auto pub = node_a.createPublisher<StringMsg>("chat");
auto sub = node_b.createSubscriber<StringMsg>("chat",
    [](std::shared_ptr<StringMsg> msg) {
        std::cout << msg->text << "\n";
    });

// 为 node_b 创建 Executor
comm::SingleThreadedExecutor exec;
exec.addNode(&node_b);
std::thread t([&] { exec.spin(); });

pub->publish(StringMsg{"Hello from A!"});   // node_b 的回调将被触发

// 清理
exec.stop();
t.join();
node_a.stop();
node_b.stop();
```

#### CallbackGroup — 回调并发控制

使用 `MultiThreadedExecutor` 时，可通过 CallbackGroup 控制回调的并发行为：

```cpp
comm::Node node("cg_node", domain);

// MutuallyExclusive 组：组内回调严格串行执行（默认行为）
comm::CallbackGroupBase exclusive_group(&node, comm::CallbackGroupType::MutuallyExclusive);

// Reentrant 组：组内回调允许并发执行
comm::CallbackGroupBase reentrant_group(&node, comm::CallbackGroupType::Reentrant);

auto pub = node.createPublisher<int>("data");

// 将 Subscriber 关联到指定的回调组
auto sub1 = node.createSubscriber<int>("data",
    [](const int& v) { /* 可并行执行 */ },
    &reentrant_group);

auto sub2 = node.createSubscriber<int>("data",
    [](const int& v) { /* 可并行执行 */ },
    &reentrant_group);

auto sub3 = node.createSubscriber<int>("data",
    [](const int& v) { /* 串行执行 */ },
    &exclusive_group);

comm::MultiThreadedExecutor exec(4);
exec.addNode(&node);
exec.spin();
// sub1 和 sub2 的回调可能同时在不同线程上执行
// sub3 的回调同一时刻只有一个在执行
```

#### QoS 配置

##### 使用预设配置

```cpp
#include <lux/communication/QoSProfiles.hpp>

// 传感器数据：只保留最新值，允许丢失
comm::SubscribeOptions sensor_opts;
sensor_opts.qos = comm::QoSProfiles::SensorData;

// 可靠指令：保证所有消息送达
comm::SubscribeOptions cmd_opts;
cmd_opts.qos = comm::QoSProfiles::ReliableCommand;

// 实时控制：低延迟 + 截止期监控
comm::SubscribeOptions rt_opts;
rt_opts.qos = comm::QoSProfiles::RealtimeControl;

auto sub = node.createSubscriber<SensorData>("sensor/imu",
    callback, nullptr, sensor_opts);
```

##### 自定义 QoS — KeepLast

```cpp
comm::SubscribeOptions opts;
opts.qos.history = comm::History::KeepLast;
opts.qos.depth   = 5;   // 最多保留最新 5 条消息

auto sub = node.createSubscriber<int>("topic",
    [](const int& v) { /* ... */ },
    nullptr, opts);
```

##### 自定义 QoS — Lifespan（消息过期丢弃）

```cpp
comm::SubscribeOptions opts;
opts.qos.lifespan = std::chrono::milliseconds{50};   // 50ms 内未消费则丢弃

auto sub = node.createSubscriber<int>("topic",
    [](const int& v) { /* 只会收到 50ms 内的新鲜消息 */ },
    nullptr, opts);
```

##### 自定义 QoS — Deadline（超时告警）

```cpp
comm::SubscribeOptions opts;
opts.qos.deadline = std::chrono::milliseconds{100};
opts.on_deadline_missed = []() {
    std::cerr << "WARNING: 超过 100ms 未收到消息!\n";
};

auto sub = node.createSubscriber<int>("sensor/heartbeat",
    [](const int&) { /* 正常处理 */ },
    nullptr, opts);
```

##### 内容过滤器 (ContentFilter)

```cpp
auto sub = node.createSubscriber<int>("numbers",
    [](const int& v) {
        std::cout << "Even: " << v << "\n";
    },
    nullptr,                            // 使用默认 CallbackGroup
    comm::SubscribeOptions{},           // 默认 QoS
    [](const int& v) { return v % 2 == 0; }  // 只接收偶数
);

// 发布 0-9 → Subscriber 仅收到 0, 2, 4, 6, 8
```

#### Executor 变体选择

##### SeqOrderedExecutor — 严格全局序列号排序

适用于多个 Topic 的消息需要按全局发布顺序处理的场景：

```cpp
#include <lux/communication/executor/SeqOrderedExecutor.hpp>

comm::Node node("ordered_node", domain);

auto pub_a = node.createPublisher<Cmd>("topic/a");
auto pub_b = node.createPublisher<Cmd>("topic/b");

std::vector<int> order;
auto sub_a = node.createSubscriber<Cmd>("topic/a",
    [&](const Cmd& c) { order.push_back(c.id); });
auto sub_b = node.createSubscriber<Cmd>("topic/b",
    [&](const Cmd& c) { order.push_back(c.id); });

comm::SeqOrderedExecutor executor;
executor.addNode(&node);

// 交替发布 → 回调严格按全局序列号顺序执行
pub_a->publish(Cmd{1});
pub_b->publish(Cmd{2});
pub_a->publish(Cmd{3});
// order == {1, 2, 3}  (保证)
```

##### TimeOrderedExecutor — 按时间戳排序

适用于传感器融合等需要按采集时间排序的场景：

```cpp
#include <lux/communication/executor/TimeOrderedExecutor.hpp>

// 20ms 的重排窗口
auto offset = std::chrono::milliseconds(20);
comm::TimeOrderedExecutor executor(offset);

executor.addNode(&imu_node);
executor.addNode(&camera_node);

// IMU 100Hz + Camera 30Hz 消息将按时间戳统一排序后执行回调
std::thread t([&] { executor.spin(); });
```

#### 传输层选项

##### 强制使用特定传输

```cpp
// Publisher 只通过网络传输
comm::PublishOptions pub_opts;
pub_opts.transport_hint = comm::PublishTransportHint::NetOnly;
auto pub = node.createPublisher<Msg>("topic", pub_opts);

// Subscriber 只接收进程内消息
comm::SubscribeOptions sub_opts;
sub_opts.transport_hint = comm::SubscribeTransportHint::IntraOnly;
auto sub = node.createSubscriber<Msg>("topic", callback, nullptr, sub_opts);
```

##### SHM 调优

```cpp
comm::PublishOptions opts;
opts.shm_ring_slot_count = 32;                  // 更多缓冲槽位
opts.shm_ring_slot_size  = 4 * 1024 * 1024;     // 每个 4MB（适合大图像）
opts.shm_pool_capacity   = 256 * 1024 * 1024;   // 256MB 共享池
opts.shm_reliable_timeout = std::chrono::milliseconds{50};  // Reliable 模式超时

auto pub = node.createPublisher<Image>("camera/image", opts);
```

#### 自定义序列化

框架自动检测并选择序列化策略。除了内置的 `memcpy`（POD 类型）和 Protobuf 支持外，可通过 ADL 自由函数实现自定义序列化：

```cpp
struct MyMsg {
    std::string name;
    std::vector<float> data;
};

// 在与 MyMsg 相同的命名空间中定义这三个函数
size_t lux_serialized_size(const MyMsg& msg) {
    return sizeof(uint32_t) + msg.name.size()
         + sizeof(uint32_t) + msg.data.size() * sizeof(float);
}

void lux_serialize(const MyMsg& msg, void* buf, size_t size) {
    auto* p = static_cast<uint8_t*>(buf);
    uint32_t name_len = static_cast<uint32_t>(msg.name.size());
    memcpy(p, &name_len, 4); p += 4;
    memcpy(p, msg.name.data(), name_len); p += name_len;
    uint32_t data_len = static_cast<uint32_t>(msg.data.size());
    memcpy(p, &data_len, 4); p += 4;
    memcpy(p, msg.data.data(), data_len * sizeof(float));
}

void lux_deserialize(MyMsg& msg, const void* buf, size_t size) {
    auto* p = static_cast<const uint8_t*>(buf);
    uint32_t name_len;
    memcpy(&name_len, p, 4); p += 4;
    msg.name.assign(reinterpret_cast<const char*>(p), name_len); p += name_len;
    uint32_t data_len;
    memcpy(&data_len, p, 4); p += 4;
    msg.data.resize(data_len);
    memcpy(msg.data.data(), p, data_len * sizeof(float));
}

// 现在可以跨进程/跨机器使用 MyMsg
auto pub = node.createPublisher<MyMsg>("custom/topic");
```

> **注意：** 仅在进程内（Intra）使用时不需要实现序列化；跨进程（SHM）或跨机器（Net）通信时必须满足 `HasSerializer<T>`。

---

### 完整示例：多传感器融合系统

```cpp
#include <lux/communication/Node.hpp>
#include <lux/communication/QoSProfiles.hpp>
#include <lux/communication/executor/TimeOrderedExecutor.hpp>

namespace comm = lux::communication;

struct ImuData   { double ax, ay, az, gx, gy, gz; uint64_t timestamp_ns; };
struct CameraFrame { int width, height; uint64_t timestamp_ns; /* pixels... */ };
struct FusedOutput { double x, y, z, roll, pitch, yaw; };

int main() {
    comm::Domain domain(1);

    // ── 传感器节点 ──
    comm::Node imu_node("imu", domain);
    comm::Node cam_node("camera", domain);

    comm::PublishOptions imu_pub_opts;
    imu_pub_opts.qos = comm::QoSProfiles::SensorData;
    auto imu_pub  = imu_node.createPublisher<ImuData>("/sensor/imu", imu_pub_opts);

    comm::PublishOptions cam_pub_opts;
    cam_pub_opts.qos = comm::QoSProfiles::SensorData;
    auto cam_pub  = cam_node.createPublisher<CameraFrame>("/sensor/camera", cam_pub_opts);

    // ── 融合节点 ──
    comm::Node fusion_node("fusion", domain);

    comm::SubscribeOptions sub_opts;
    sub_opts.qos = comm::QoSProfiles::SensorData;
    sub_opts.qos.deadline = std::chrono::milliseconds{50};
    sub_opts.on_deadline_missed = []() {
        std::cerr << "Sensor timeout!\n";
    };

    auto imu_sub = fusion_node.createSubscriber<ImuData>("/sensor/imu",
        [](const ImuData& d) {
            // IMU 预积分...
        },
        nullptr, sub_opts);

    auto cam_sub = fusion_node.createSubscriber<CameraFrame>("/sensor/camera",
        [](std::shared_ptr<CameraFrame> frame) {
            // 视觉特征提取...
        },
        nullptr, sub_opts);

    // ── 输出节点 ──
    auto fused_pub = fusion_node.createPublisher<FusedOutput>("/fusion/output");

    // ── 按时间戳排序执行 ──
    comm::TimeOrderedExecutor executor(std::chrono::milliseconds{20});
    executor.addNode(&fusion_node);
    std::thread exec_thread([&] { executor.spin(); });

    // ... 传感器线程发布数据 ...

    // 清理
    executor.stop();
    exec_thread.join();
    imu_node.stop();
    cam_node.stop();
    fusion_node.stop();
}
```

---

### API 速查表

| 操作 | 代码 |
|------|------|
| 创建节点 | `comm::Node node("name");` |
| 创建节点（自定义 Domain） | `comm::Domain d(1); comm::Node node("name", d);` |
| 创建 Publisher | `auto pub = node.createPublisher<T>("topic");` |
| 创建 Subscriber | `auto sub = node.createSubscriber<T>("topic", callback);` |
| 发布消息（值） | `pub->publish(msg);` |
| 发布消息（零拷贝） | `pub->publish(std::make_shared<T>(...));` |
| 就地发布 | `pub->emplace(args...);` |
| 带 QoS 的 Subscriber | `node.createSubscriber<T>("topic", cb, nullptr, opts);` |
| 带 ContentFilter | `node.createSubscriber<T>("topic", cb, nullptr, opts, filter);` |
| 带 CallbackGroup | `node.createSubscriber<T>("topic", cb, &group);` |
| 单线程 Executor | `comm::SingleThreadedExecutor exec;` |
| 多线程 Executor | `comm::MultiThreadedExecutor exec(N);` |
| 序列号排序 Executor | `comm::SeqOrderedExecutor exec;` |
| 时间排序 Executor | `comm::TimeOrderedExecutor exec(offset);` |
| 驱动回调 | `exec.addNode(&node); exec.spin();` |
| 非阻塞轮询 | `exec.spinSome();` |
| 停止 Executor | `exec.stop();` |
| 停止 Node | `node.stop();` |

---

## 目录结构

```
node/
├── CMakeLists.txt              # 构建配置（target: node, namespace: lux::communication）
├── README.md                   # 本文档
├── include/                    # 公开头文件
│   └── lux/communication/
│       ├── Node.hpp                # 统一入口（转发到 unified/Node.hpp）
│       ├── Publisher.hpp           # 转发到 unified/Publisher.hpp
│       ├── Subscriber.hpp         # 转发到 unified/Subscriber.hpp
│       ├── Domain.hpp             # Domain：Topic 容器 + 全局序列号分配
│       ├── NodeBase.hpp           # 基类：管理 Publisher/Subscriber/CallbackGroup
│       ├── PublisherBase.hpp      # Publisher 基类
│       ├── SubscriberBase.hpp     # Subscriber 基类
│       ├── TopicBase.hpp          # Topic 基类（CoW 订阅者快照）
│       ├── CallbackGroupBase.hpp  # 回调组（MutuallyExclusive / Reentrant）
│       ├── ExecutorBase.hpp       # Executor 基类（spin-then-block 调度）
│       ├── MessageTraits.hpp      # SmallValueMsg concept + 条件类型别名
│       ├── QoSProfile.hpp         # QoS 参数定义
│       ├── QoSProfiles.hpp        # QoS 预设配置
│       ├── QoSChecker.hpp         # QoS 兼容性检查
│       ├── NodeOptions.hpp        # Node 选项（发现、SHM、网络开关等）
│       ├── PublishOptions.hpp     # Publisher 选项（SHM Ring/Pool 参数、网络端口等）
│       ├── SubscribeOptions.hpp   # Subscriber 选项（QoS、deadline 回调等）
│       ├── ChannelKind.hpp        # 传输类型枚举：Intra / Shm / Net
│       ├── TransportSelector.hpp  # 根据 PID/hostname 选择传输层
│       ├── IoThread.hpp           # IO 线程（SHM 轮询 + IoReactor）
│       ├── TokenBucket.hpp        # 令牌桶限流器
│       ├── Hash.hpp               # FNV-1a 64-bit 哈希
│       ├── Queue.hpp              # 队列抽象（moodycamel / BlockingQueue）
│       ├── ExecEntry.hpp          # Executor 执行条目
│       ├── TimeExecEntry.hpp      # 时间排序执行条目
│       ├── ReorderBuffer.hpp      # 序列号重排缓冲
│       │
│       ├── executor/              # Executor 变体
│       │   ├── SingleThreadedExecutor.hpp
│       │   ├── MultiThreadedExecutor.hpp
│       │   ├── SeqOrderedExecutor.hpp
│       │   └── TimeOrderedExecutor.hpp
│       │
│       ├── unified/               # 统一传输层实现
│       │   ├── Node.hpp           # 创建 Publisher<T> / Subscriber<T>
│       │   ├── Publisher.hpp      # 自动路由到 Intra / SHM / Net
│       │   └── Subscriber.hpp    # 多路径接收 + 有序队列
│       │
│       ├── intraprocess/          # 进程内组件
│       │   └── Topic.hpp          # Topic<T>：零拷贝广播
│       │
│       ├── discovery/             # 对等发现
│       │   └── DiscoveryService.hpp
│       │
│       ├── transport/             # 传输层协议
│       │   ├── FrameHeader.hpp         # 48 字节帧头
│       │   ├── ShmRingBuffer.hpp       # SHM SPSC 环形缓冲布局
│       │   ├── ShmRingWriter.hpp       # 环形缓冲写端
│       │   ├── ShmRingReader.hpp       # 环形缓冲读端
│       │   ├── ShmDataPool.hpp         # SHM 大消息池
│       │   ├── ShmNotify.hpp           # 跨进程通知（futex / Event）
│       │   ├── LoanedMessage.hpp       # 零拷贝 Placement-new 借用
│       │   ├── IoReactor.hpp           # IO 反应器（epoll / IOCP）
│       │   ├── Handshake.hpp           # TCP 握手协议
│       │   ├── UdpTransportWriter.hpp  # UDP 发送（散射聚集）
│       │   ├── UdpTransportReader.hpp  # UDP 接收
│       │   ├── TcpTransportWriter.hpp  # TCP 发送（含心跳）
│       │   ├── TcpTransportReader.hpp  # TCP 接收（含心跳响应）
│       │   ├── FragmentSender.hpp      # UDP 分片发送
│       │   ├── FragmentAssembler.hpp   # UDP 分片重组
│       │   └── NetConstants.hpp        # 网络常量
│       │
│       ├── serialization/         # 序列化
│       │   └── Serializer.hpp     # 编译期分派: RawMemcpy / Protobuf / Custom
│       │
│       └── platform/              # 平台抽象
│           ├── PlatformDefs.hpp   # PID、hostname、时钟
│           ├── SharedMemory.hpp   # SHM 创建/打开/映射
│           └── NetSocket.hpp      # 套接字创建/绑定
│
├── pinclude/                      # 私有头文件（库内部可见）
│   └── lux/communication/discovery/
│       ├── DiscoveryPacket.hpp    # Announce/Withdraw/Heartbeat 数据包
│       ├── MulticastAnnouncer.hpp # 组播播报器
│       ├── ShmRegistry.hpp        # SHM 本地注册表
│       └── ShmRegistryDefs.hpp    # 注册表常量和布局
│
├── src/                           # 实现文件
│   ├── Domain.cpp, TopicBase.cpp, NodeBase.cpp ...
│   ├── executor/                  # 四种 Executor 实现
│   ├── unified/                   # 统一 Node 实现
│   ├── discovery/                 # 发现服务实现
│   ├── transport/                 # 传输层实现（各平台）
│   ├── platform/                  # 平台实现（Win / Posix）
│   └── interprocess/             # UdpMultiCast 遗留实现
│
└── test/                          # 测试
    ├── simple_test.cpp            # 基本发布/订阅
    ├── node_test.cpp              # 域隔离、多节点、性能基准
    ├── seq_order_test.cpp         # SeqOrderedExecutor 排序验证
    ├── timeorder_executor_test.cpp# TimeOrderedExecutor
    ├── unified_transport_test.cpp # 统一传输集成测试
    ├── qos_test.cpp               # QoS 行为测试
    ├── discovery_test.cpp         # 发现服务测试
    ├── shm_transport_test.cpp     # SHM Ring / Pool 读写测试
    ├── net_transport_test.cpp     # UDP / TCP / 分片测试
    └── loopback_optimization_test.cpp # 回环优化性能测试
```

---

## 核心架构

### 1. 消息流水线

```
┌─────────────┐              ┌──────────────┐
│ Publisher<T> │─ publish() ──│  Topic<T>    │── enqueue() ──┐
│              │              │  (Intra)     │               │
│              │              └──────────────┘               │
│              │                                             │    ┌──────────────┐
│              │─ SHM rings ──→ ShmRingWriter ───→ ShmRingReader ─→│ Subscriber<T>│
│              │                                             │    │              │
│              │─ UDP/TCP ────→ Transport Writers ──→ IoReactor ──→│  ordered     │
│              │                                             │    │  queue       │
└─────────────┘                                             │    │              │
                                                            ▼    │  callback()  │
                                          ┌──────────────┐  ◄────│              │
                                          │  Executor    │──────→ └──────────────┘
                                          │  (spin loop) │
                                          └──────────────┘
```

**Intra-process 路径：**
```
Publisher::publish(msg)
  → publishIntra(msg)
    → Topic<T>::getSubscriberSnapshot()    // atomic load (CoW)
    → Domain::allocateSeqRange(n)          // atomic fetch_add
    → Subscriber::enqueue(seq, msg)        // lock-free queue push
      → CallbackGroup::notify()            // test_and_set + enqueueReady
        → Executor::ready_queue            // ConcurrentQueue
          → Executor spin loop → takeAll() → invoke callback
```

**SHM 路径：**
```
Publisher::publish(msg)
  → Serializer<T>::serialize(msg, slot)    // 序列化到 SHM Ring 槽位
  → ShmRingWriter::commitSlot()            // 标记 Ready + 通知

IoThread (每100μs轮询)
  → Subscriber::pollShmReaders()
    → ShmRingReader::acquireReadView()     // 自旋等待 Ready 状态
    → Serializer<T>::deserialize()
    → enqueue(seq, stored_msg_t<T>)        // 进入统一队列
```

**网络路径：**
```
Publisher::publish(msg)
  → TCP/UDP TransportWriter::send()       // 散射聚集避免中间拷贝
  → FragmentSender (若 > MTU)             // 应用层分片

IoReactor (epoll / IOCP)
  → TcpTransportReader::onReadReady()
    → Subscriber::processNetFrame()       // 反序列化、QoS、入队
    → Subscriber::enqueue()               // 进入统一队列
```

### 2. 传输选择

传输层的选择由 `TransportSelector` 根据对等方信息自动决定：

| 条件 | 传输 | 原因 |
|------|------|------|
| 同进程 (相同 PID) | Intra | 零拷贝，零序列化 |
| 同机器、不同进程 (相同 hostname) | SHM | 纳秒级延迟，无网络栈开销 |
| 不同机器 | Net (UDP / TCP) | 跨网络通信 |

用户可通过 `PublishTransportHint` / `SubscribeTransportHint` 覆盖自动选择。

### 3. 对等发现 (Discovery)

发现服务实现节点间的自动感知，由两个子系统组成：

- **ShmRegistry**：每个 Domain 一个共享内存段，存储 `(topic_hash, pid, role)` → `TopicEndpoint` 的映射。用于同机器跨进程的快速发现。
- **MulticastAnnouncer**：UDP 组播发送 Announce / Withdraw / Heartbeat 数据包，用于跨机器发现。

**生命周期：**
1. Publisher / Subscriber 注册时发送 Announce
2. 发现对端后触发 `onPeerDiscovered` 回调 → 创建 SHM Ring 或 TCP/UDP 连接
3. 周期性 Heartbeat（默认 2s），超时 GC（默认 6s）
4. 节点退出时发送 Withdraw

**心跳机制（两层设计）：**
- **发现层**：组播 Heartbeat 用于检测远端节点存活
- **传输层**：TCP Ping/Pong（默认 1s 间隔，3s 超时）用于检测 TCP 连接健康

---

## 核心组件详解

### Publisher\<T\>

```cpp
// 创建
auto pub = node.createPublisher<MyMsg>("topic/name", publish_options);

// 三种发布方式
pub->publish(msg);                        // 拷贝构造 (SmallValueMsg: 值传递)
pub->publish(std::make_shared<MyMsg>(…)); // 零拷贝 (shared_ptr 直传, 非 SmallValueMsg)
pub->emplace(arg1, arg2);                // 就地构造

// 零拷贝借用 (仅限 TriviallyCopyableMsg + SHM 路径)
auto loaned = pub->loan();
loaned->field = value;
pub->publish(std::move(loaned));          // 直接在 SHM 槽位中构造
```

**内部成员：**
- `Topic<T>` 引用 → Intra 路径
- `vector<ShmPeer>` → 每个跨进程订阅者一个 ShmRingWriter
- `vector<NetPeer>` → 每个远端订阅者一对 UDP+TCP Writer
- `ShmDataPool` → 大消息共享池（>64KB 且多订阅者时启用）
- `TokenBucket` → 带宽限制（可选）
- `intra_only_` 快速路径标志 → 跳过 SHM/Net 的互斥锁检查

### Subscriber\<T\>

```cpp
auto sub = node.createSubscriber<MyMsg>(
    "topic/name",
    [](const MyMsg& msg) { /* 处理消息 */ },     // SmallValueMsg 类型
    // [](std::shared_ptr<MyMsg> msg) { … },     // 非 SmallValueMsg 类型
    callback_group,     // 可选：指定回调组
    subscribe_options,  // 可选：QoS 参数
    content_filter      // 可选：内容过滤器
);
```

**内部队列：** `moodycamel::ConcurrentQueue<OrderedItem>`，其中 `OrderedItem` 包含：
- `uint64_t seq` — 全局序列号
- `uint64_t timestamp_ns` — 发布时的单调时钟（仅 lifespan > 0 时采集）
- `stored_msg_t<T> msg` — SmallValueMsg 为 `T`，否则为 `shared_ptr<T>`

### Executor 变体

| Executor | 回调顺序保证 | 线程模型 | 适用场景 |
|----------|------------|----------|---------|
| **SingleThreadedExecutor** | FIFO（按 notify 顺序） | 单线程 | 通用、最低延迟 |
| **MultiThreadedExecutor** | 无全局保证 | 线程池 (N 线程) | 高吞吐、CPU 密集回调 |
| **SeqOrderedExecutor** | **严格全局序列号顺序** | 单线程 | 多 Topic 消息需要全序 |
| **TimeOrderedExecutor** | 按消息时间戳排序 | 单线程 | 传感器融合、回放 |

**SeqOrderedExecutor 内部：**
- 环形缓冲 (65536 槽, O(1) 平均) + hashmap fallback（覆盖极端乱序）
- 策略："先执行，遇空隙再 drain" — 最大化执行而非阻塞
- 每个 Subscriber 每次最多 drain 16 条 → 保持 reorder 窗口较小

**Spin-then-block 优化（所有 Executor 共享）：**
```
waitOneReady():
  1. spinning_in_userspace_ = true
  2. 循环 4096 次 → 尝试 try_dequeue(ready_queue_)
     - 每次迭代执行 _mm_pause() (x86) 或 yield (ARM)
  3. spinning_in_userspace_ = false
  4. 末次 drain（捕获 flag 清除期间的写入）
  5. 若仍无消息 → sem.acquire()（内核阻塞）

enqueueReady(sub):
  1. ready_queue_.enqueue(sub)
  2. 若 spinning_in_userspace_ == false → sem.release()
     否则跳过内核调用（executor 会在 spin 中发现）
```

### 序列化系统

编译期自动分派，零运行时开销：

| 策略 | 检测条件 | 方式 |
|------|---------|------|
| **RawMemcpy** | `trivially_copyable && standard_layout && !pointer` | `memcpy` |
| **Protobuf** | 鸭子类型：`ByteSizeLong` / `SerializeToArray` / `ParseFromArray` | Protocol Buffers |
| **Custom** | ADL 自由函数：`lux_serialize` / `lux_deserialize` / `lux_serialized_size` | 用户自定义 |

若以上均不满足，触发 `static_assert` 编译错误。

`HasSerializer<T>` concept 用于在 Publisher/Subscriber 中 `if constexpr` 决定是否启用 SHM/Net 路径。仅支持序列化的类型才能跨进程/跨机器通信。

### SmallValueMsg 优化

```cpp
template<typename T>
concept SmallValueMsg =
    std::is_trivially_copyable_v<T> &&
    std::is_standard_layout_v<T> &&
    (sizeof(T) <= 128) &&           // 可通过 LUX_SMALL_VALUE_MSG_THRESHOLD 自定义
    !std::is_pointer_v<T>;
```

当 `T` 满足 `SmallValueMsg` 时：
- **Publisher** 直接按值传递，不调用 `std::make_shared`
- **Subscriber 回调** 签名为 `void(const T&)` 而非 `void(std::shared_ptr<T>)`
- **内部队列** 存储 `T` 值而非 `shared_ptr<T>`
- **性能影响**：消除堆分配和原子引用计数，对 `int`/`double` 等小类型吞吐提升约 138%

---

## QoS 系统

### 参数

```cpp
struct QoSProfile {
    Reliability reliability = Reliability::BestEffort;  // BestEffort | Reliable
    History    history      = History::KeepAll;          // KeepAll | KeepLast
    uint32_t   depth        = 0;                         // KeepLast 的深度
    std::chrono::milliseconds lifespan{0};               // 消息生存期（0 = 不限）
    std::chrono::milliseconds deadline{0};               // 消息截止期（0 = 不监控）
    std::chrono::microseconds latency_budget{0};         // 传输选择提示
    uint64_t   bandwidth_limit = 0;                      // 字节/秒（0 = 不限）
};
```

### 预设配置

| 预设 | Reliability | History | depth | lifespan | deadline | latency_budget | bandwidth |
|------|------------|---------|-------|----------|----------|---------------|-----------|
| **SensorData** | BestEffort | KeepLast | 1 | — | — | 1ms | — |
| **ReliableCommand** | Reliable | KeepAll | 100 | — | — | — | — |
| **LargeTransfer** | Reliable | KeepLast | 2 | — | — | 10ms | — |
| **KeepLatest** | BestEffort | KeepLast | 1 | — | — | — | — |
| **RealtimeControl** | BestEffort | KeepLast | 1 | 50ms | 10ms | 500µs | — |

### QoS 行为

| 特性 | 实现方式 |
|------|---------|
| **KeepLast(N)** | 入队时若 `queue.size_approx() > depth` 则弹出最旧消息 |
| **Lifespan** | 出队时检查 `now - item.timestamp_ns > lifespan` 则丢弃 |
| **Deadline** | IoThread 周期检查 `now - last_message_time > deadline`，触发 `on_deadline_missed` 回调 |
| **Bandwidth** | Publisher 通过 `TokenBucket` 限流；Reliable 模式下阻塞等待，BestEffort 模式下直接丢弃 |
| **ContentFilter** | 入队前调用 `content_filter_(msg)`，返回 false 则跳过 |
| **QoSChecker** | 创建 Topic 时检查 Publisher/Subscriber QoS 兼容性（诊断警告，不阻断） |

---

## SHM 传输详解

### Ring Buffer 布局

```
┌─────────────────────────── SHM 段 ────────────────────────────┐
│ Writer CacheLine (64B)  │ Reader CacheLine (64B) │ Notify (64B) │
│  magic, version         │  read_seq               │  futex/event │
│  slot_count, slot_size  │  reader_pid             │              │
│  write_seq, writer_pid  │                         │              │
├─────────────────────────┴──────────────────────────┴──────────────┤
│ Slot 0: [SlotHeader(16B)] [FrameHeader(48B)] [Payload...]       │
│ Slot 1: [SlotHeader(16B)] [FrameHeader(48B)] [Payload...]       │
│ ...                                                              │
│ Slot N-1: ...                                                    │
└──────────────────────────────────────────────────────────────────┘
```

**SlotHeader 状态机：** `Free → Writing → Ready → Reading → Free`

**默认参数：**
- 16 个 slot，每个 1MB → 总计约 16MB / Ring
- 大消息阈值：64KB → 超过此大小且多订阅者时使用 ShmDataPool

### ShmDataPool

- 单独的 SHM 段（默认 64MB），所有订阅者共享读取
- Free-list 分配器，每个 Block 含引用计数
- Ring 中仅存 `PoolDescriptor`（offset + size + ref_count_offset）
- Publisher 设置 `ref_count = subscriber_count`；最后一个 Reader release 时归还 free-list

### FrameHeader (48B)

| 偏移 | 字段 | 类型 | 说明 |
|------|------|------|------|
| 0x00 | magic | u32 | `0x4C555846` ("LUXF") |
| 0x04 | version | u16 | 协议版本 (1) |
| 0x06 | flags | u16 | 压缩/加密/格式/借用/池/重组/可靠/Ping/Pong |
| 0x08 | topic_hash | u64 | FNV-1a 64-bit 话题名哈希 |
| 0x10 | seq_num | u64 | 全局序列号 |
| 0x18 | timestamp_ns | u64 | 发布时单调时钟（纳秒） |
| 0x20 | payload_size | u32 | 负载大小（不含帧头） |
| 0x24 | header_crc | u32 | 可选 CRC 校验 |
| 0x28 | reserved | u64 | 保留 / 对齐 |

**Flags 位定义：**
| 位 | 含义 |
|----|------|
| 0 | Compressed（保留） |
| 1 | Encrypted（保留） |
| 2-3 | 序列化格式 (0=RawMemcpy, 1=Protobuf, 2=Custom) |
| 4 | Loaned（零拷贝借用消息） |
| 5 | Pooled（数据在 ShmDataPool 中） |
| 6 | Reassembled（UDP 分片重组） |
| 7 | Reliable（可靠传输标志） |
| 8 | Ping（TCP 心跳请求） |
| 9 | Pong（TCP 心跳响应） |

---

## 网络传输详解

### 常量

| 常量 | 值 | 说明 |
|------|---|------|
| `kMaxUdpPayload` | 1472B | 1500 (Ethernet MTU) - 28 (IP+UDP) |
| `kNetSmallMsgThreshold` | 1424B | 1472 - 48 (FrameHeader) |
| `kMaxFragPayload` | 1448B | 1472 - 24 (FragHeader) |
| `kMaxFragmentedMsgSize` | 64MB | UDP 分片上限 |
| `kTcpBufferSize` | 2MB | TCP 发送/接收缓冲 |
| `kUdpRecvBufferSize` | 4MB | UDP 接收缓冲 |
| `kFragmentTimeoutMs` | 200ms | 分片重组超时 |

### UDP 路径

- **小消息 (≤ 1424B)：** FrameHeader + Payload 通过 `sendToV()` 散射聚集发送，避免中间拷贝
- **大消息 (> 1424B)：** `FragmentSender` 将消息分片为 ≤ 1448B 的 UDP 包，接收端 `FragmentAssembler` 重组

### TCP 路径

1. **握手：** Subscriber 发起 `HandshakeRequest`（topic_hash, type_hash, pid, hostname）→ Publisher 验证并回复 `HandshakeResponse`
2. **数据传输：** FrameHeader + Payload 流式发送
3. **心跳：** Publisher 每 1s 发送 Ping → Subscriber 回复 Pong → 3s 超时断开死连接

---

## 平台抽象

| 组件 | Linux / POSIX | Windows |
|------|--------------|---------|
| 共享内存 | `shm_open` + `mmap` | `CreateFileMapping` + `MapViewOfFile` |
| 跨进程通知 | `futex` | `Named Event` |
| IO 多路复用 | `epoll` | `IOCP` + WSAPoll 混合 |
| 套接字 | POSIX sockets | WinSock2 |
| PID | `getpid()` | `GetCurrentProcessId()` |
| 主机名 | `gethostname()` | `GetComputerNameExA()` |
| 单调时钟 | `clock_gettime(CLOCK_MONOTONIC)` | `QueryPerformanceCounter` |

---

## 性能优化汇总

| 优化 | 机制 | 影响 |
|------|------|------|
| **SmallValueMsg 值传递** | ≤128B trivially-copyable 类型按值传递，跳过 `shared_ptr` | 消除堆分配 + 原子引用计数 |
| **Spin-then-block Executor** | 4096× `_mm_pause` 用户态自旋后回退到内核信号量 | 避免高频场景下的上下文切换 |
| **Intra-only 快速路径** | `has_shm_peers_` / `has_net_peers_` 原子标志 | 纯进程内场景跳过互斥锁 |
| **惰性时间戳** | 仅在 `lifespan > 0` 时调用 `steadyNowNs()` | 消除无条件 syscall |
| **惰性 IoThread** | 首次 `registerPoller()` 时才启动 | 纯进程内节点无多余线程 |
| **CoW 订阅者快照** | `atomic<shared_ptr<vector>>` 读无锁 | 发布路径无互斥锁 |
| **Lock-free 队列** | `moodycamel::ConcurrentQueue` | O(1) 无锁入队/出队 |
| **SHM 缓存行对齐** | Writer/Reader 各自占一个缓存行 | 减少跨进程 false sharing |
| **散射聚集 I/O** | FrameHeader + Payload 合并为一次 `sendV()` | 消除中间 memcpy |
| **零拷贝借用 (Loan)** | 在 SHM 槽位中 placement-new | 消除序列化拷贝 |
| **有界 drain** | SeqOrderedExecutor 每次 ≤16 条 | 控制重排窗口 |

---

## 构建配置

### 依赖

| 依赖 | 类型 | 说明 |
|------|------|------|
| `lux::cxx::concurrent` | 必需 | 线程池、阻塞队列 |
| `lux::cxx::compile_time` | 必需 | 编译期类型信息 |
| `lux::cxx::algorithm` | 必需 | 算法工具 |
| `stduuid` | 必需 (private) | UUID 生成 |
| `cameron314/concurrentqueue` | 推荐 | lock-free 队列（fallback: BlockingQueue） |
| `cppzmq` | 可选 | ZMQ 支持（定义 `LUX_HAS_ZMQ`） |
| `ws2_32` | 平台 (Windows) | WinSock2 |
| `rt` | 平台 (POSIX) | POSIX 实时库 (shm_open 等) |

### CMake 选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `USE_SHARED_MESSAGE_MODE` | OFF | 强制所有类型使用 `shared_ptr` 路径 |
| `USE_LOCKFREE_QUEUE` | ON | 启用 `moodycamel::ConcurrentQueue` |
| `ENABLE_NODE_TEST` | ON | 构建测试可执行文件 |

---

## 默认参数速查

### NodeOptions

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `enable_discovery` | `true` | 启用对等发现 |
| `enable_shm` | `true` | 启用 SHM 传输 |
| `enable_net` | `true` | 启用网络传输 |
| `enable_intra` | `true` | 启用进程内传输 |
| `shm_poll_interval_us` | `100` | SHM 轮询间隔（微秒） |
| `reactor_timeout_ms` | `10` | IoReactor poll 超时（毫秒） |
| `discovery_heartbeat_interval_ms` | `2000` | 发现心跳间隔 |
| `discovery_heartbeat_timeout_ms` | `6000` | 发现心跳超时（GC 阈值） |
| `tcp_ping_interval_ms` | `1000` | TCP 心跳间隔 |
| `tcp_ping_timeout_ms` | `3000` | TCP 心跳超时 |

### PublishOptions

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `shm_ring_slot_count` | `16` | SHM Ring 槽位数 |
| `shm_ring_slot_size` | `1 MB` | 每个槽位大小 |
| `shm_pool_capacity` | `64 MB` | ShmDataPool 总容量 |
| `net_udp_port` | `0` (自动) | UDP 绑定端口 |
| `net_tcp_port` | `0` (自动) | TCP 绑定端口 |
| `net_large_threshold` | `64 KB` | 超此大小优先用 TCP |
| `transport_hint` | `Auto` | 传输层选择提示 |
| `shm_reliable_timeout` | `10 ms` | SHM Reliable 模式自旋超时 |

---

## 测试覆盖

| 测试 | 覆盖内容 | 用例数 |
|------|---------|--------|
| `simple_test` | 基本进程内 pub/sub | 基础功能 |
| `node_test` | 域隔离、多节点通信、性能基准（吞吐 / 延迟 / 大消息）、内存泄漏检测 | 12 项 |
| `qos_test` | KeepLast、KeepAll、Lifespan、Deadline、ContentFilter、Bandwidth、组合 QoS | 53 项 |
| `unified_transport_test` | TransportSelector、IoThread、统一 pub/sub、多 Topic、零拷贝、stop()、emplace | 23 项 |
| `seq_order_test` | 10M 消息全序验证 + 重排缓冲统计 | 1 项 (10M msgs) |
| `timeorder_executor_test` | 多传感器时间排序 | 功能验证 |
| `discovery_test` | ShmRegistry + MulticastAnnouncer | 发现流程 |
| `shm_transport_test` | Ring 读写 + DataPool 操作 | 传输层 |
| `net_transport_test` | UDP/TCP 发送接收 + 分片重组 | 330 项 |
| `loopback_optimization_test` | 回环优化性能 | 性能 |

---

## 已知不完善之处

### 功能缺失

1. **压缩和加密支持**
   - *现状：* `FrameHeader.flags` 中保留了 Compressed 和 Encrypted 位，但未实现
   - *改进方向：* 实现 LZ4/zstd 压缩 + TLS/AES 加密

### QoS 执行完整度

| QoS 特性 | 状态 | 说明 |
|----------|------|------|
| KeepLast / KeepAll | ✅ 完整实现 | 入队时裁剪 |
| Lifespan | ✅ 完整实现 | 出队时过滤 |
| ContentFilter | ✅ 完整实现 | 入队前过滤 |
| Bandwidth Limit | ✅ 完整实现 | TokenBucket 限流 |
| Deadline 监控 | ⚠️ 基础实现 | 回调可触发，但仅在 IoThread 运行时有效 |
| Reliability | ⚠️ 部分实现 | SHM: 自旋等待 slot；Net: TCP 保证；但无端到端 ACK/重传 |
| QoS 兼容性检查 | ⚠️ 仅诊断 | QoSChecker 输出警告，不阻断不匹配的连接 |
| Latency Budget | ⚠️ 仅提示 | 影响 TransportSelector，不强制延迟保证 |

### 性能基准 (参考)

测试环境：Windows, Release 构建, moodycamel::ConcurrentQueue

| 场景 | 吞吐 | 备注 |
|------|------|------|
| 单 pub/sub (10M double, intra) | ~4.2M msg/s | SmallValueMsg + spin-then-block |
| 5 subs × 5M msgs (intra) | ~1.6M msg/s (每 sub) | Multi-subscriber 开销: CoW snapshot + 多次 enqueue |
| 大消息 (1MB × 1000, intra) | ~3.7K msg/s | 受限于内存带宽和 shared_ptr 开销 |
| SeqOrdered (10M msgs, 2 topics) | ~1.1M msg/s | 包含重排开销 |
| 单条延迟 (avg) | ~4.4 μs | Min: 1μs, Max: ~150μs |
