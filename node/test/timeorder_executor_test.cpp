#include <chrono>
#include <thread>
#include <random>
#include <atomic>
#include <iostream>
#include <iomanip>

// 假设以下头文件里有：Domain, Node, TimeOrderedExecutor, Publisher, Subscriber, 
// 以及 ImuStampedS, ImageStampedS, timestamp_from_ns, timestamp_to_ns, etc.
#include <lux/communication/introprocess/Node.hpp>
#include "lux/communication/builtin_msgs/sensor_msgs/imu_stamped.st.h"
#include "lux/communication/builtin_msgs/sensor_msgs/image_stamped.st.h"
#include "lux/communication/builtin_msgs/common_msgs/timestamp.st.h"

static std::atomic<bool> g_running{true}; // 用于控制发布线程何时停止

// 一个函数，用于演示单次测试，在指定的time_offset下运行一段时间，统计out-of-order情况
void run_test_with_offset(std::chrono::nanoseconds offset,
                          uint64_t test_duration_ms,
                          std::atomic<uint64_t> &imu_out_of_order_count,
                          std::atomic<uint64_t> &cam_out_of_order_count)
{
    // 1) 创建 Domain / Node
    auto domain = std::make_shared<lux::communication::introprocess::Domain>(0);
    auto node   = std::make_shared<lux::communication::introprocess::Node>("test_node", domain);

    // 2) 创建 TimeOrderedExecutor，设置延迟窗口
    auto timeExec = std::make_shared<lux::communication::introprocess::TimeOrderedExecutor>(offset);
    // 将 Node 的默认回调组加入 Executor
    timeExec->addNode(node);

    // 3) 创建 Publisher (IMU / Camera)
    auto pub_imu = node->createPublisher<lux::communication::builtin_msgs::sensor_msgs::ImuStampedS>("/imu");
    auto pub_cam = node->createPublisher<lux::communication::builtin_msgs::sensor_msgs::ImageStampedS>("/camera");

    // 4) 统计变量
    std::atomic<uint64_t> last_imu_ts(0);
    std::atomic<uint64_t> last_cam_ts(0);

    // 用来统计乱序次数
    imu_out_of_order_count = 0;
    cam_out_of_order_count = 0;

    // 5) 创建 Subscriber (IMU)
    auto sub_imu = node->createSubscriber<lux::communication::builtin_msgs::sensor_msgs::ImuStampedS>(
        "/imu",
        [&](const lux::communication::builtin_msgs::sensor_msgs::ImuStampedS & msg)
        {
            uint64_t t = lux::communication::builtin_msgs::common_msgs::timestamp_to_ns(msg.timestamp);
            uint64_t prev = last_imu_ts.load(std::memory_order_acquire);
            if (t < prev) {
                imu_out_of_order_count.fetch_add(1, std::memory_order_relaxed);
            }
            last_imu_ts.store(t, std::memory_order_release);
        }
    );

    // 6) 创建 Subscriber (Camera)
    auto sub_cam = node->createSubscriber<lux::communication::builtin_msgs::sensor_msgs::ImageStampedS>(
        "/camera",
        [&](const lux::communication::builtin_msgs::sensor_msgs::ImageStampedS & msg)
        {
            uint64_t t = lux::communication::builtin_msgs::common_msgs::timestamp_to_ns(msg.timestamp);
            uint64_t prev = last_cam_ts.load(std::memory_order_acquire);
            if (t < prev) {
                cam_out_of_order_count.fetch_add(1, std::memory_order_relaxed);
            }
            last_cam_ts.store(t, std::memory_order_release);
        }
    );

    // 7) 启动两个发布线程: IMU (100Hz 带抖动) / Camera (30Hz 带抖动)
    g_running.store(true);

    std::thread t_imu([&]{
        std::mt19937 rng(std::random_device{}());
        // 随机抖动：0~5ms
        std::uniform_int_distribution<int> jitter(0, 5000);

        while(g_running.load())
        {
            // 构造ImuStampedS
            lux::communication::builtin_msgs::sensor_msgs::ImuStampedS imu;
            auto now = std::chrono::steady_clock::now();
            auto ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
            lux::communication::builtin_msgs::common_msgs::timestamp_from_ns(imu.timestamp, ns);

            // 发布
            pub_imu->publish(imu);

            // 休眠 ~10ms(100Hz)
            int extra_us = jitter(rng); // 0~5000微秒 => 0~5ms
            std::this_thread::sleep_for(std::chrono::milliseconds(10) + std::chrono::microseconds(extra_us));
        }
    });

    std::thread t_cam([&]{
        std::mt19937 rng(std::random_device{}());
        // 随机抖动：0~8ms
        std::uniform_int_distribution<int> jitter(0, 8000);

        while(g_running.load())
        {
            lux::communication::builtin_msgs::sensor_msgs::ImageStampedS img;
            auto now = std::chrono::steady_clock::now();
            auto ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
            lux::communication::builtin_msgs::common_msgs::timestamp_from_ns(img.timestamp, ns);

            pub_cam->publish(img);

            // 休眠 ~33ms(约30Hz)
            int extra_us = jitter(rng); // 0~8000
            std::this_thread::sleep_for(std::chrono::milliseconds(33) + std::chrono::microseconds(extra_us));
        }
    });

    // 8) 在另一个线程中启动 TimeOrderedExecutor 的 spin
    std::thread t_exec([&]{
        timeExec->spin();
    });

    // 9) 让测试持续指定时间 test_duration_ms
    std::this_thread::sleep_for(std::chrono::milliseconds(test_duration_ms));

    // 10) 停止
    g_running.store(false);
    node->stop();       // 通知 Node
    timeExec->stop();   // 停止 Executor

    // 等待发布线程 & spin线程退出
    t_imu.join();
    t_cam.join();
    t_exec.join();
}

int main()
{
    // 我们测试多个offset值(0, 5ms, 20ms, 50ms等)
    // 每次跑10秒，统计一次结果
    const uint64_t test_duration_ms = 10'000; // 10秒
    for (auto offset_ms : {0, 5, 20, 50})
    {
        // 转成nanoseconds
        auto offset_ns = std::chrono::milliseconds(offset_ms);

        std::atomic<uint64_t> imu_out_of_order = 0;
        std::atomic<uint64_t> cam_out_of_order = 0;

        // 运行测试
        run_test_with_offset(offset_ns, test_duration_ms, imu_out_of_order, cam_out_of_order);

        std::cout << "[Test Offset = " << offset_ms << " ms]"
                  << " IMU out_of_order=" << imu_out_of_order
                  << " Camera out_of_order=" << cam_out_of_order
                  << std::endl;
    }

    return 0;
}
