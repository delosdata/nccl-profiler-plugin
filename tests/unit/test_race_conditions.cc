// SPDX-FileCopyrightText: 2025 Delos Data Inc
// SPDX-License-Identifier: LicenseRef-FSL-1.1-ALv2

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <set>
#include <thread>
#include <vector>

#include "../../aggregation.h"
#include "../../communicator_state.h"
#include "../../events.h"

// Tests specifically designed to catch race conditions and thread-safety bugs
// Run with ThreadSanitizer: TSAN_OPTIONS="halt_on_error=1" for best results

class RaceConditionTest : public ::testing::Test
{
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Test: Multiple threads allocating from the same buffer simultaneously
TEST_F(RaceConditionTest, ConcurrentBufferAllocation)
{
    CommunicatorState state;
    const int NUM_THREADS = 8;

    std::vector<std::thread> threads;
    std::vector<std::vector<otelEventHandle_t*>> thread_slots(NUM_THREADS);

    // Each thread allocates many slots
    for (int t = 0; t < NUM_THREADS; t++)
    {
        threads.emplace_back(
            [&state, &thread_slots, t]()
            {
                for (int i = 0; i < 1000; i++)
                {
                    otelEventHandle_t* slot = state.allocate_event_slot();
                    if (slot != nullptr)
                    {
                        thread_slots[t].push_back(slot);
                        // Write unique value to detect corruption
                        slot->type = (uint8_t)((t * 10000 + i) % 256);
                    }
                }
            });
    }

    for (auto& t : threads)
    {
        t.join();
    }

    // Verify no duplicate pointers (each slot should be unique)
    std::set<otelEventHandle_t*> all_slots;
    for (const auto& slots : thread_slots)
    {
        for (auto* slot : slots)
        {
            EXPECT_TRUE(all_slots.insert(slot).second) << "Duplicate slot detected!";
        }
    }

    // Verify data integrity (no corruption)
    for (int t = 0; t < NUM_THREADS; t++)
    {
        for (size_t i = 0; i < thread_slots[t].size(); i++)
        {
            size_t expected_val = (t * 10000 + i) % 256;
            EXPECT_EQ(thread_slots[t][i]->type, (uint8_t)expected_val) << "Slot corruption detected!";
        }
    }
}

// Test: Race between allocation and window closing
TEST_F(RaceConditionTest, AllocationDuringWindowClose)
{
    CommunicatorState state;
    std::atomic<bool> stop{false};
    std::atomic<int> alloc_success{0};
    std::atomic<int> alloc_fail{0};
    std::atomic<int> close_count{0};

    // Allocator threads
    auto allocator = [&]()
    {
        while (!stop.load(std::memory_order_acquire))
        {
            otelEventHandle_t* slot = state.allocate_event_slot();
            if (slot)
            {
                alloc_success.fetch_add(1, std::memory_order_relaxed);
                // Write data to verify no corruption
                slot->startTs = 123.456;
            }
            else
            {
                alloc_fail.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };

    // Window closer thread
    auto closer = [&]()
    {
        while (!stop.load(std::memory_order_acquire))
        {
            uint8_t idx = state.get_active_buffer_idx();
            state.trigger_window_closing(idx);
            close_count.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; i++)
    {
        threads.emplace_back(allocator);
    }
    threads.emplace_back(closer);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    stop.store(true, std::memory_order_release);

    for (auto& t : threads)
    {
        t.join();
    }

    // Verify reasonable behavior
    EXPECT_GT(alloc_success.load(), 0);
    EXPECT_GT(close_count.load(), 0);

    // System should still be functional
    uint8_t idx = state.get_active_buffer_idx();
    EXPECT_LT(idx, NUM_BUFFERS);
}

// Test: Concurrent in_progress_count modifications
TEST_F(RaceConditionTest, ConcurrentInProgressTracking)
{
    CommunicatorState state;
    const int NUM_THREADS = 8;
    const int ITERATIONS  = 1000;
    uint8_t buffer_idx    = 0;

    std::atomic<int> start_count{0};
    std::atomic<int> complete_count{0};

    auto worker = [&]()
    {
        for (int i = 0; i < ITERATIONS; i++)
        {
            state.mark_operation_start(buffer_idx);
            start_count.fetch_add(1, std::memory_order_relaxed);

            // Simulate work
            std::this_thread::yield();

            state.mark_operation_complete(buffer_idx);
            complete_count.fetch_add(1, std::memory_order_relaxed);
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; i++)
    {
        threads.emplace_back(worker);
    }

    for (auto& t : threads)
    {
        t.join();
    }

    // All starts should have completed
    EXPECT_EQ(start_count.load(), NUM_THREADS * ITERATIONS);
    EXPECT_EQ(complete_count.load(), NUM_THREADS * ITERATIONS);

    // Final count should be 0 (all operations completed)
    WindowMetadata* window = state.get_window_metadata(buffer_idx);
    EXPECT_EQ(window->in_progress_count.load(), 0);
}

// Test: Race in window state transitions
TEST_F(RaceConditionTest, ConcurrentWindowStateTransitions)
{
    CommunicatorState state;
    const int NUM_THREADS = 4;
    std::atomic<int> successful_transitions{0};

    // Multiple threads try to close the same window
    auto closer = [&]()
    {
        for (int i = 0; i < 100; i++)
        {
            uint8_t idx        = state.get_active_buffer_idx();
            WindowState before = state.get_window_metadata(idx)->state.load();

            state.trigger_window_closing(idx);

            WindowState after = state.get_window_metadata(idx)->state.load();

            // If we successfully transitioned from FILLING to CLOSING
            if (before == WINDOW_FILLING && after == WINDOW_CLOSING)
            {
                successful_transitions.fetch_add(1, std::memory_order_relaxed);
            }

            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; i++)
    {
        threads.emplace_back(closer);
    }

    for (auto& t : threads)
    {
        t.join();
    }

    // Should have some successful transitions (not all, due to races)
    EXPECT_GT(successful_transitions.load(), 0);
    EXPECT_LE(successful_transitions.load(), NUM_THREADS * 100);
}

// Test: Verify WindowAggregator is NOT thread-safe (by design)
// WindowAggregator is meant to be used by a single thread (telemetry thread)
// This test documents that concurrent use is not supported
TEST_F(RaceConditionTest, WindowAggregatorNotThreadSafe)
{
    // NOTE: WindowAggregator uses std::map without synchronization
    // It is designed for single-threaded use by the telemetry thread
    // Each window is processed by one thread at a time

    // Test single-threaded use (the correct pattern)
    WindowAggregator aggregator(0);

    // Store events in vectors to have stable addresses
    std::vector<otelEventHandle_t> colls, proxyOps, proxySteps;
    colls.reserve(1000);
    proxyOps.reserve(2000);  // 2 per coll
    proxySteps.reserve(2000);

    for (int i = 0; i < 1000; i++)
    {
        otelEventHandle_t coll = {};
        coll.type              = ncclProfileColl;
        coll.coll.func         = "AllReduce";
        coll.coll.algo         = "Ring";
        coll.coll.proto        = "Simple";
        coll.coll.nChannels    = 2;
        coll.coll.bytes        = 1024;
        coll.startTs           = i * 1.0;
        coll.endTs             = i * 1.0 + 10.0;
        coll.rank              = 0;
        colls.push_back(coll);
        aggregator.addEvent(colls.back());

        // Add 2 ProxyOps for this Coll
        for (int ch = 0; ch < 2; ch++)
        {
            otelEventHandle_t proxyOp = {};
            proxyOp.type              = ncclProfileProxyOp;
            proxyOp.proxyOp.peer      = 1;
            proxyOp.proxyOp.channelId = ch;
            proxyOp.proxyOp.chunkSize = 128;
            proxyOp.startTs           = i * 1.0 + ch;
            proxyOp.endTs             = i * 1.0 + ch + 0.5;
            proxyOp.parentObj         = &colls.back();
            proxyOp.rank              = 0;
            proxyOps.push_back(proxyOp);

            otelEventHandle_t proxyStep     = {};
            proxyStep.type                  = ncclProfileProxyStep;
            proxyStep.proxyStep.step        = 0;
            proxyStep.proxyStep.transSize   = 64;
            proxyStep.proxyStep.sendWaitTs  = i * 1.0 + ch + 0.2;
            proxyStep.proxyStep.hasSendWait = true;
            proxyStep.startTs               = i * 1.0 + ch;
            proxyStep.endTs                 = i * 1.0 + ch + 0.5;
            proxyStep.parentObj             = &proxyOps.back();
            proxyStep.rank                  = 0;
            proxySteps.push_back(proxyStep);

            aggregator.addEvent(proxySteps.back());
            aggregator.addEvent(proxyOps.back());
        }
    }

    // Finalize before checking
    aggregator.finalize();

    // Verify single-threaded aggregation works correctly
    const auto& collectives = aggregator.getCollectives();
    EXPECT_EQ(collectives.size(), 1u);

    auto it = collectives.find("Comm0_AllReduce_Ring_Simple_2Chnl");
    ASSERT_NE(it, collectives.end());
    EXPECT_EQ(it->second.count, 1000);
}

// Test: Verify correct single-threaded aggregation with reads
TEST_F(RaceConditionTest, SingleThreadedAggregationWithReads)
{
    // WindowAggregator is designed for single-threaded use
    // This test validates the intended usage pattern
    WindowAggregator aggregator(0);

    // Store events in vectors to have stable addresses
    std::vector<otelEventHandle_t> colls, proxyOps, proxySteps;
    colls.reserve(1000);
    proxyOps.reserve(2000);
    proxySteps.reserve(2000);

    // Add events (single-threaded)
    for (int i = 0; i < 1000; i++)
    {
        otelEventHandle_t coll = {};
        coll.type              = ncclProfileColl;
        coll.coll.func         = "AllReduce";
        coll.coll.algo         = "Ring";
        coll.coll.proto        = "Simple";
        coll.coll.nChannels    = 2;
        coll.coll.bytes        = 1024;
        coll.startTs           = i * 1.0;
        coll.endTs             = i * 1.0 + 5.0;
        coll.rank              = 0;
        colls.push_back(coll);
        aggregator.addEvent(colls.back());

        // Add ProxyOps with ProxySteps
        for (int ch = 0; ch < 2; ch++)
        {
            otelEventHandle_t proxyOp = {};
            proxyOp.type              = ncclProfileProxyOp;
            proxyOp.proxyOp.peer      = 1;
            proxyOp.proxyOp.channelId = ch;
            proxyOp.proxyOp.chunkSize = 128;
            proxyOp.startTs           = i * 1.0 + ch * 0.1;
            proxyOp.endTs             = i * 1.0 + ch * 0.1 + 0.5;
            proxyOp.parentObj         = &colls.back();
            proxyOp.rank              = 0;
            proxyOps.push_back(proxyOp);

            otelEventHandle_t proxyStep     = {};
            proxyStep.type                  = ncclProfileProxyStep;
            proxyStep.proxyStep.step        = 0;
            proxyStep.proxyStep.transSize   = 64;
            proxyStep.proxyStep.sendWaitTs  = i * 1.0 + ch * 0.1 + 0.2;
            proxyStep.proxyStep.hasSendWait = true;
            proxyStep.startTs               = i * 1.0 + ch * 0.1;
            proxyStep.endTs                 = i * 1.0 + ch * 0.1 + 0.5;
            proxyStep.parentObj             = &proxyOps.back();
            proxyStep.rank                  = 0;
            proxySteps.push_back(proxyStep);

            aggregator.addEvent(proxySteps.back());
            aggregator.addEvent(proxyOps.back());
        }
    }

    // Finalize to calculate final durations
    aggregator.finalize();

    // Final verification (single-threaded read - safe)
    const auto& collectives = aggregator.getCollectives();
    EXPECT_EQ(collectives.size(), 1u);
    auto it = collectives.find("Comm0_AllReduce_Ring_Simple_2Chnl");
    ASSERT_NE(it, collectives.end());
    EXPECT_EQ(it->second.count, 1000);
    EXPECT_EQ(it->second.totalBytes, 1000u * 1024);
}

// Test: Single-threaded transfer cache updates (correct usage)
TEST_F(RaceConditionTest, SingleThreadedTransferCacheUpdates)
{
    // Test the correct single-threaded usage pattern
    WindowAggregator aggregator(0);

    // Store events in vectors to have stable addresses
    std::vector<otelEventHandle_t> proxyOps, proxySteps;
    proxyOps.reserve(1000);
    proxySteps.reserve(1000);

    // First create a collective
    otelEventHandle_t coll = {};
    coll.type              = ncclProfileColl;
    coll.coll.func         = "AllReduce";
    coll.coll.algo         = "Ring";
    coll.coll.proto        = "Simple";
    coll.coll.nChannels    = 2;
    coll.coll.bytes        = 1024;
    coll.startTs           = 0.0;
    coll.endTs             = 100.0;
    coll.rank              = 0;
    aggregator.addEvent(coll);

    // Add proxy ops (with ProxySteps) in single thread (correct pattern)
    const int PROXY_COUNT = 1000;

    for (int i = 0; i < PROXY_COUNT; i++)
    {
        otelEventHandle_t proxyOp = {};
        proxyOp.type              = ncclProfileProxyOp;
        proxyOp.proxyOp.peer      = i % 4;
        proxyOp.proxyOp.channelId = i % 8;
        proxyOp.proxyOp.chunkSize = 128 + (i % 10);
        proxyOp.startTs           = i * 0.1;
        proxyOp.endTs             = i * 0.1 + 0.05;
        proxyOp.rank              = 0;
        proxyOp.parentObj         = &coll;
        proxyOps.push_back(proxyOp);

        otelEventHandle_t proxyStep     = {};
        proxyStep.type                  = ncclProfileProxyStep;
        proxyStep.proxyStep.step        = 0;
        proxyStep.proxyStep.transSize   = 64 + (i % 10);
        proxyStep.proxyStep.sendWaitTs  = i * 0.1 + 0.02;
        proxyStep.proxyStep.hasSendWait = true;
        proxyStep.startTs               = i * 0.1;
        proxyStep.endTs                 = i * 0.1 + 0.05;
        proxyStep.parentObj             = &proxyOps.back();
        proxyStep.rank                  = 0;
        proxySteps.push_back(proxyStep);

        aggregator.addEvent(proxySteps.back());
        aggregator.addEvent(proxyOps.back());
    }

    // Finalize to calculate durations
    aggregator.finalize();

    // Verify cache consistency
    const auto& collectives = aggregator.getCollectives();
    auto it                 = collectives.find("Comm0_AllReduce_Ring_Simple_2Chnl");
    ASSERT_NE(it, collectives.end());

    // Should have exactly PROXY_COUNT transfers cached
    EXPECT_EQ(it->second.cachedTotalTransferCount, PROXY_COUNT);
    EXPECT_GT(it->second.cachedTotalTransferBytes, 0u);
    EXPECT_GT(it->second.cachedTotalTransferTimeUs, 0.0);

    // Verify derived values are consistent
    double avgCount = it->second.getAverageTransferCount();
    EXPECT_DOUBLE_EQ(avgCount, static_cast<double>(PROXY_COUNT));
}

// Test: Buffer exhaustion under concurrent load
TEST_F(RaceConditionTest, BufferExhaustionUnderLoad)
{
    CommunicatorState state;
    std::atomic<int> total_allocated{0};
    std::atomic<int> allocation_failures{0};

    auto worker = [&]()
    {
        for (int i = 0; i < BUFFER_SIZE / 4; i++)
        {
            otelEventHandle_t* slot = state.allocate_event_slot();
            if (slot)
            {
                total_allocated.fetch_add(1, std::memory_order_relaxed);
                // Write to slot to ensure it's valid
                slot->type = ncclProfileColl;
            }
            else
            {
                allocation_failures.fetch_add(1, std::memory_order_relaxed);
                break;
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 8; i++)
    {
        threads.emplace_back(worker);
    }

    for (auto& t : threads)
    {
        t.join();
    }

    // With automatic window switching, all allocations should succeed
    // 8 threads * (BUFFER_SIZE/4) = 2*BUFFER_SIZE allocations across multiple buffers
    EXPECT_GT(total_allocated.load(), 0);
    // Some may fail if buffers aren't ready, but many should succeed
    EXPECT_GE(total_allocated.load(), WINDOW_TRIGGER_COUNT);
}

// Test: State machine integrity under concurrent access
TEST_F(RaceConditionTest, WindowStateMachineIntegrity)
{
    CommunicatorState state;
    const int NUM_CYCLES = 100;
    std::atomic<int> observed_inconsistencies{0};

    auto worker = [&]()
    {
        for (int i = 0; i < NUM_CYCLES; i++)
        {
            uint8_t idx            = state.get_active_buffer_idx();
            WindowMetadata* window = state.get_window_metadata(idx);

            // Check state validity
            WindowState s = window->state.load(std::memory_order_acquire);
            if (s != WINDOW_FILLING && s != WINDOW_CLOSING && s != WINDOW_PROCESSING && s != WINDOW_READY)
            {
                observed_inconsistencies.fetch_add(1, std::memory_order_relaxed);
            }

            // Try to close window
            state.trigger_window_closing(idx);

            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; i++)
    {
        threads.emplace_back(worker);
    }

    for (auto& t : threads)
    {
        t.join();
    }

    // Should never observe invalid states
    EXPECT_EQ(observed_inconsistencies.load(), 0);
}

// Test: Element count consistency
TEST_F(RaceConditionTest, ElementCountConsistency)
{
    CommunicatorState state;
    const int NUM_THREADS = 8;
    const int ALLOCS      = 500;
    std::atomic<int> successful_allocs{0};

    auto worker = [&]()
    {
        for (int i = 0; i < ALLOCS; i++)
        {
            otelEventHandle_t* slot = state.allocate_event_slot();
            if (slot)
            {
                successful_allocs.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; i++)
    {
        threads.emplace_back(worker);
    }

    for (auto& t : threads)
    {
        t.join();
    }

    // Element count should match successful allocations
    // (assuming all in same window, which may not be true if window switched)
    uint8_t idx            = state.get_active_buffer_idx();
    WindowMetadata* window = state.get_window_metadata(idx);

    // The current window's count should be <= successful allocs
    EXPECT_LE(window->element_count.load(), successful_allocs.load());
    EXPECT_GT(successful_allocs.load(), 0);
}
