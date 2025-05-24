#include <chrono>
#include <thread>
#include <random>
#include <atomic>
#include <iostream>
#include <iomanip>

// Assume the following headers provide Domain, Node, TimeOrderedExecutor,
// Publisher, Subscriber, ImuStampedS, ImageStampedS, timestamp utilities, etc.
#include <lux/communication/intraprocess/Node.hpp>
#include "lux/communication/builtin_msgs/sensor_msgs/imu_stamped.st.h"
#include "lux/communication/builtin_msgs/sensor_msgs/image_stamped.st.h"
#include "lux/communication/builtin_msgs/common_msgs/timestamp.st.h"

static std::atomic<bool> g_running{true}; // control when publishing threads stop

// Function that runs a single test with a given time_offset for a period and counts out-of-order events
void run_test_with_offset(std::chrono::nanoseconds offset,
                          uint64_t test_duration_ms,
                          std::atomic<uint64_t> &imu_out_of_order_count,
                          std::atomic<uint64_t> &cam_out_of_order_count)
{
    // 1) Create Domain / Node
    auto domain = std::make_shared<lux::communication::intraprocess::Domain>(0);
    auto node   = std::make_shared<lux::communication::intraprocess::Node>("test_node", domain);

    // 2) Create TimeOrderedExecutor with a delay window
    auto timeExec = std::make_shared<lux::communication::TimeOrderedExecutor>(offset);
    // Add the node's default callback group to the executor
    timeExec->addNode(node);

    // 3) Create publishers (IMU / Camera)
    auto pub_imu = node->createPublisher<lux::communication::builtin_msgs::sensor_msgs::ImuStampedS>("/imu");
    auto pub_cam = node->createPublisher<lux::communication::builtin_msgs::sensor_msgs::ImageStampedS>("/camera");

    // 4) Stats variables
    std::atomic<uint64_t> last_imu_ts(0);
    std::atomic<uint64_t> last_cam_ts(0);

    // Counters for out-of-order events
    imu_out_of_order_count = 0;
    cam_out_of_order_count = 0;

    // 5) Create subscriber for IMU
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

    // 6) Create subscriber for camera
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

    // 7) Start two publishing threads: IMU (100Hz with jitter) / Camera (30Hz with jitter)
    g_running.store(true);

    std::thread t_imu([&]{
        std::mt19937 rng(std::random_device{}());
        // Random jitter: 0-5ms
        std::uniform_int_distribution<int> jitter(0, 5000);

        while(g_running.load())
        {
            // Build ImuStampedS
            lux::communication::builtin_msgs::sensor_msgs::ImuStampedS imu;
            auto now = std::chrono::steady_clock::now();
            auto ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
            lux::communication::builtin_msgs::common_msgs::timestamp_from_ns(imu.timestamp, ns);

            // Publish
            pub_imu->publish(imu);

            // Sleep about 10ms (100Hz)
            int extra_us = jitter(rng); // 0-5000 microseconds (~0-5ms)
            std::this_thread::sleep_for(std::chrono::milliseconds(10) + std::chrono::microseconds(extra_us));
        }
    });

    std::thread t_cam([&]{
        std::mt19937 rng(std::random_device{}());
        // Random jitter: 0-8ms
        std::uniform_int_distribution<int> jitter(0, 8000);

        while(g_running.load())
        {
            lux::communication::builtin_msgs::sensor_msgs::ImageStampedS img;
            auto now = std::chrono::steady_clock::now();
            auto ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
            lux::communication::builtin_msgs::common_msgs::timestamp_from_ns(img.timestamp, ns);

            pub_cam->publish(img);

            // Sleep about 33ms (~30Hz)
            int extra_us = jitter(rng); // 0~8000
            std::this_thread::sleep_for(std::chrono::milliseconds(33) + std::chrono::microseconds(extra_us));
        }
    });

    // 8) Start TimeOrderedExecutor spin in another thread
    std::thread t_exec([&]{
        timeExec->spin();
    });

    // 9) Let the test run for test_duration_ms
    std::this_thread::sleep_for(std::chrono::milliseconds(test_duration_ms));

    // 10) Stop
    g_running.store(false);
    node->stop();       // notify Node
    timeExec->stop();   // stop Executor

    // Wait for publishing threads and spin thread to exit
    t_imu.join();
    t_cam.join();
    t_exec.join();
}

int main()
{
    // Test several offset values (0, 5ms, 20ms, 50ms, ...)
    // Run each for 10 seconds and log the results
    const uint64_t test_duration_ms = 10'000; // 10 seconds
    for (auto offset_ms : {0, 5, 20, 50})
    {
        // Convert to nanoseconds
        auto offset_ns = std::chrono::milliseconds(offset_ms);

        std::atomic<uint64_t> imu_out_of_order = 0;
        std::atomic<uint64_t> cam_out_of_order = 0;

        // Run the test
        run_test_with_offset(offset_ns, test_duration_ms, imu_out_of_order, cam_out_of_order);

        std::cout << "[Test Offset = " << offset_ms << " ms]"
                  << " IMU out_of_order=" << imu_out_of_order
                  << " Camera out_of_order=" << cam_out_of_order
                  << std::endl;
    }

    return 0;
}
