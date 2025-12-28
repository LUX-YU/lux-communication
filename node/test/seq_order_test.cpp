/**
 * @file seq_order_test.cpp
 * @brief Test to verify that SeqOrderedExecutor maintains strict global ordering
 *        across multiple publishers and subscribers with different frequencies.
 * 
 * Test scenario:
 * - 2 Topics (TopicA and TopicB)
 * - 2 Publishers (one for each topic) with different publish frequencies
 * - 2 Subscribers (one for each topic)
 * - Publishers run in a separate thread
 * - Executor runs in the main thread
 * - Verify that callbacks are executed in strict sequence order
 * - Measure performance (messages per second)
 */

#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <chrono>
#include <cassert>
#include <iomanip>

#include <lux/communication/intraprocess/Node.hpp>
#include <lux/communication/intraprocess/Publisher.hpp>
#include <lux/communication/intraprocess/Subscriber.hpp>
#include <lux/communication/executor/SeqOrderedExecutor.hpp>

struct TestMessage {
    int publisher_id;   // Which publisher sent this
    int message_id;     // Message sequence within publisher
    uint64_t publish_order;  // Global publish order (for verification)
};

int main()
{
    using namespace lux::communication::intraprocess;
    using namespace lux::communication;

    std::cout << "=== SeqOrderedExecutor Order & Performance Test ===" << std::endl;

    // Track received messages
    std::mutex received_mutex;
    std::vector<uint64_t> received_orders;  // Global publish orders as received
    std::atomic<int> total_received{0};

    // Global publish counter (to track actual publish order)
    std::atomic<uint64_t> global_publish_order{0};

    // Configuration - high volume for performance testing
    const int pub_a_count = 5000000;   // Publisher A sends 5M messages
    const int pub_b_count = 5000000;   // Publisher B sends 5M messages
    const int total_messages = pub_a_count + pub_b_count;  // 10M total

    // Reserve space to avoid reallocation during test
    received_orders.reserve(total_messages);

    // Create node
    Node node("test_node");

    // Create publishers
    Publisher<TestMessage> pub_a("/topic_a", &node);
    Publisher<TestMessage> pub_b("/topic_b", &node);

    // Create subscribers
    Subscriber<TestMessage> sub_a(
        "/topic_a", &node,
        [&](const std::shared_ptr<TestMessage> msg)
        {
            std::lock_guard<std::mutex> lock(received_mutex);
            received_orders.push_back(msg->publish_order);
            total_received++;
        }
    );

    Subscriber<TestMessage> sub_b(
        "/topic_b", &node,
        [&](const std::shared_ptr<TestMessage> msg)
        {
            std::lock_guard<std::mutex> lock(received_mutex);
            received_orders.push_back(msg->publish_order);
            total_received++;
        }
    );

    // Create executor (SeqOrderedExecutor for strict ordering)
    SeqOrderedExecutor executor;
    executor.addNode(&node);

    // Flag to signal publisher thread to stop
    std::atomic<bool> publishing_done{false};

    std::cout << "Publishing " << total_messages << " messages..." << std::endl;

    // Record start time
    auto start_time = std::chrono::high_resolution_clock::now();

    // Publisher thread - no delays, maximum speed
    std::thread publisher_thread([&]()
    {
        int a_sent = 0;
        int b_sent = 0;

        // Interleave publishing with different "frequencies"
        // Publisher B publishes ~1.5x more often than Publisher A
        while (a_sent < pub_a_count || b_sent < pub_b_count)
        {
            // Publisher B: publish 3 messages
            for (int i = 0; i < 3 && b_sent < pub_b_count; ++i)
            {
                TestMessage msg;
                msg.publisher_id = 2;  // Publisher B
                msg.message_id = b_sent;
                msg.publish_order = global_publish_order.fetch_add(1);
                pub_b.publish(msg);
                b_sent++;
            }

            // Publisher A: publish 2 messages
            for (int i = 0; i < 2 && a_sent < pub_a_count; ++i)
            {
                TestMessage msg;
                msg.publisher_id = 1;  // Publisher A
                msg.message_id = a_sent;
                msg.publish_order = global_publish_order.fetch_add(1);
                pub_a.publish(msg);
                a_sent++;
            }
        }

        publishing_done = true;
    });

    // Executor thread (main) - spin until all messages received
    const auto timeout = std::chrono::seconds(300);  // 5 minutes for 10M messages
    auto timeout_start = std::chrono::steady_clock::now();

    while (total_received < total_messages)
    {
        executor.spinSome();

        // Timeout check
        auto elapsed = std::chrono::steady_clock::now() - timeout_start;
        if (elapsed > timeout)
        {
            std::cerr << "ERROR: Timeout waiting for messages. Received: " 
                      << total_received << "/" << total_messages << std::endl;
            break;
        }
    }

    // Record end time
    auto end_time = std::chrono::high_resolution_clock::now();

    executor.stop();
    executor.removeNode(&node);
    publisher_thread.join();

    // Calculate performance metrics
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    double duration_sec = duration.count() / 1000000.0;
    double msgs_per_sec = total_received / duration_sec;

    std::cout << "\n=== Performance Results ===" << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Total messages:    " << total_received << std::endl;
    std::cout << "Total time:        " << duration_sec << " seconds" << std::endl;
    std::cout << "Throughput:        " << msgs_per_sec << " messages/second" << std::endl;
    std::cout << "Avg latency:       " << (duration.count() / static_cast<double>(total_received)) << " μs/message" << std::endl;

    // Print ReorderBuffer diagnostics
    std::cout << "\n=== ReorderBuffer Diagnostics ===" << std::endl;
    const auto& stats = executor.stats();
    std::cout << "Ring put OK:       " << stats.ring_put_ok << std::endl;
    std::cout << "Ring reject (far): " << stats.ring_reject_too_far << std::endl;
    std::cout << "Ring reject (col): " << stats.ring_reject_collision << std::endl;
    std::cout << "Fallback put:      " << stats.fallback_put << std::endl;
    std::cout << "Max window:        " << stats.max_window << std::endl;
    std::cout << "Discarded old:     " << stats.discarded_old << std::endl;
    std::cout << "Final pending:     " << executor.pending_size() << std::endl;
    std::cout << "Final fallback:    " << executor.fallback_size() << std::endl;

    // Calculate ring hit rate
    uint64_t total_puts = stats.ring_put_ok + stats.fallback_put;
    if (total_puts > 0) {
        double ring_hit_rate = 100.0 * stats.ring_put_ok / total_puts;
        std::cout << "Ring hit rate:     " << std::fixed << std::setprecision(2) 
                  << ring_hit_rate << "%" << std::endl;
    }

    // Verify ordering
    std::cout << "\n=== Order Verification ===" << std::endl;

    bool order_correct = true;
    int first_error_idx = -1;
    int error_count = 0;

    for (size_t i = 1; i < received_orders.size(); ++i)
    {
        if (received_orders[i] < received_orders[i-1])
        {
            error_count++;
            if (first_error_idx < 0)
            {
                first_error_idx = static_cast<int>(i);
                order_correct = false;
            }
        }
    }

    if (order_correct && received_orders.size() == static_cast<size_t>(total_messages))
    {
        std::cout << "✅ SUCCESS: All " << total_messages 
                  << " messages received in correct sequence order!" << std::endl;
        
        // Additional check: verify sequence is strictly increasing
        bool strictly_increasing = true;
        int non_increasing_count = 0;
        for (size_t i = 1; i < received_orders.size(); ++i)
        {
            if (received_orders[i] <= received_orders[i-1])
            {
                strictly_increasing = false;
                non_increasing_count++;
            }
        }
        
        if (strictly_increasing)
        {
            std::cout << "✅ Sequence is strictly increasing." << std::endl;
        }
        else
        {
            std::cout << "⚠️  Sequence has " << non_increasing_count 
                      << " non-increasing transitions (possible duplicates)." << std::endl;
        }

        return 0;  // Test passed
    }
    else
    {
        std::cout << "❌ FAILURE: Messages received out of order!" << std::endl;
        std::cout << "   Total order violations: " << error_count << std::endl;
        
        if (first_error_idx >= 0)
        {
            std::cout << "   First error at index " << first_error_idx 
                      << ": order " << received_orders[first_error_idx] 
                      << " came after " << received_orders[first_error_idx - 1] << std::endl;
        }
        
        // Print first few errors for debugging
        std::cout << "\nFirst 10 order violations:" << std::endl;
        int shown = 0;
        for (size_t i = 1; i < received_orders.size() && shown < 10; ++i)
        {
            if (received_orders[i] < received_orders[i-1])
            {
                std::cout << "   [" << i << "] " << received_orders[i-1] 
                          << " -> " << received_orders[i] << std::endl;
                shown++;
            }
        }

        return 1;  // Test failed
    }
}
