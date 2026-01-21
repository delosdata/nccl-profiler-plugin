// SPDX-FileCopyrightText: 2025 Delos Data Inc
// SPDX-License-Identifier: LicenseRef-FSL-1.1-ALv2

#include <gtest/gtest.h>

#include <thread>
#include <vector>

#include "../../communicator_state.h"
#include "../test_helpers.h"

class CommunicatorStateTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        reset_notify_window_ready_tracking();  // Reset tracking for each test
        state            = new CommunicatorState();
        state->comm_name = "test_comm";
        state->comm_hash = 12345;
        state->nNodes    = 2;
        state->nranks    = 4;
        state->rank      = 0;
        state->commName  = "test_comm";
    }

    void TearDown() override
    {
        if (state)
        {
            delete state;
            state = nullptr;
        }
    }

    CommunicatorState* state = nullptr;
};

TEST_F(CommunicatorStateTest, Creation)
{
    ASSERT_NE(state, nullptr);
    EXPECT_STREQ(state->comm_name, "test_comm");
    EXPECT_EQ(state->comm_hash, 12345);
    EXPECT_EQ(state->rank, 0);
    EXPECT_EQ(state->nranks, 4);
    EXPECT_EQ(state->nNodes, 2);
}

TEST_F(CommunicatorStateTest, InitialState)
{
    EXPECT_EQ(state->get_active_buffer_idx(), 0);

    // First buffer should be FILLING
    WindowMetadata* window0 = state->get_window_metadata(0);
    ASSERT_NE(window0, nullptr);
    EXPECT_EQ(window0->state.load(), WINDOW_FILLING);
    EXPECT_EQ(window0->element_count.load(), 0);

    // Other buffers should be READY
    for (int i = 1; i < NUM_BUFFERS; i++)
    {
        WindowMetadata* window = state->get_window_metadata(i);
        ASSERT_NE(window, nullptr);
        EXPECT_EQ(window->state.load(), WINDOW_READY);
    }
}

TEST_F(CommunicatorStateTest, AllocateSlot)
{
    otelEventHandle_t* slot = state->allocate_event_slot();

    ASSERT_NE(slot, nullptr);

    // Element count should be incremented
    WindowMetadata* window = state->get_window_metadata(0);
    EXPECT_EQ(window->element_count.load(), 1);
}

TEST_F(CommunicatorStateTest, MultipleAllocations)
{
    std::vector<otelEventHandle_t*> slots;

    // Allocate 100 slots
    for (int i = 0; i < 100; i++)
    {
        otelEventHandle_t* slot = state->allocate_event_slot();
        ASSERT_NE(slot, nullptr);
        slots.push_back(slot);
    }

    // Element count should be 100
    WindowMetadata* window = state->get_window_metadata(0);
    EXPECT_EQ(window->element_count.load(), 100);

    // All slots should be unique
    for (size_t i = 0; i < slots.size(); i++)
    {
        for (size_t j = i + 1; j < slots.size(); j++)
        {
            EXPECT_NE(slots[i], slots[j]);
        }
    }
}

TEST_F(CommunicatorStateTest, WindowClosing)
{
    uint8_t buffer_idx = state->get_active_buffer_idx();
    EXPECT_EQ(buffer_idx, 0);

    // Trigger window closing
    state->trigger_window_closing(buffer_idx);

    // Window 0 should immediately transition to PROCESSING (since in_progress_count == 0)
    WindowMetadata* window0 = state->get_window_metadata(0);
    EXPECT_EQ(window0->state.load(), WINDOW_PROCESSING);

    // Active buffer should switch to 1
    EXPECT_EQ(state->get_active_buffer_idx(), 1);

    // Window 1 should be FILLING
    WindowMetadata* window1 = state->get_window_metadata(1);
    EXPECT_EQ(window1->state.load(), WINDOW_FILLING);

    // Telemetry should have been notified
    EXPECT_EQ(get_notify_window_ready_call_count(), 1);
}

TEST_F(CommunicatorStateTest, InProgressTracking)
{
    uint8_t buffer_idx = 0;

    state->mark_operation_start(buffer_idx);
    state->mark_operation_start(buffer_idx);

    WindowMetadata* window = state->get_window_metadata(buffer_idx);
    EXPECT_EQ(window->in_progress_count.load(), 2);

    state->mark_operation_complete(buffer_idx);
    EXPECT_EQ(window->in_progress_count.load(), 1);

    state->mark_operation_complete(buffer_idx);
    EXPECT_EQ(window->in_progress_count.load(), 0);
}

TEST_F(CommunicatorStateTest, TransitionToProcessing)
{
    uint8_t buffer_idx = 0;

    // Simulate that ProxyOps exist (normal network scenario)
    state->windows[buffer_idx].has_proxy_ops.store(true, std::memory_order_release);

    // Mark one operation in progress
    state->mark_operation_start(buffer_idx);

    // Trigger closing
    state->trigger_window_closing(buffer_idx);

    WindowMetadata* window = state->get_window_metadata(buffer_idx);
    EXPECT_EQ(window->state.load(), WINDOW_CLOSING);

    // Complete the operation
    state->mark_operation_complete(buffer_idx);

    // Should transition to PROCESSING
    EXPECT_EQ(window->state.load(), WINDOW_PROCESSING);
}

TEST_F(CommunicatorStateTest, ConcurrentAllocations)
{
    const int num_threads            = 4;
    const int allocations_per_thread = 100;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; t++)
    {
        threads.emplace_back(
            [this]()
            {
                for (int i = 0; i < 100; i++)
                {
                    otelEventHandle_t* slot = state->allocate_event_slot();
                    EXPECT_NE(slot, nullptr);
                }
            });
    }

    for (auto& thread : threads)
    {
        thread.join();
    }

    // Total element count should be num_threads * allocations_per_thread
    WindowMetadata* window = state->get_window_metadata(0);
    EXPECT_EQ(window->element_count.load(), num_threads * allocations_per_thread);
}

TEST_F(CommunicatorStateTest, BufferOverflow)
{
    // Try to allocate more than BUFFER_SIZE events
    int allocated = 0;
    for (int i = 0; i < BUFFER_SIZE + 1000; i++)
    {
        otelEventHandle_t* slot = state->allocate_event_slot();
        if (slot != nullptr)
        {
            allocated++;
        }
        else
        {
            break;
        }
    }

    // With automatic window switching at WINDOW_TRIGGER_COUNT,
    // we can allocate more than BUFFER_SIZE across multiple windows
    EXPECT_GE(allocated, WINDOW_TRIGGER_COUNT);
}

TEST_F(CommunicatorStateTest, WindowWrapAround)
{
    // Test that buffers wrap around correctly after NUM_BUFFERS
    for (int i = 0; i < NUM_BUFFERS * 2; i++)
    {
        uint8_t current = state->get_active_buffer_idx();
        EXPECT_LT(current, NUM_BUFFERS);
        state->trigger_window_closing(current);
    }

    // After wrapping, should be back to buffer 0 or near it
    uint8_t final_idx = state->get_active_buffer_idx();
    EXPECT_LT(final_idx, NUM_BUFFERS);
}

TEST_F(CommunicatorStateTest, GetWindowMetadataOutOfBounds)
{
    // Test out of bounds access (should return nullptr or handle gracefully)
    WindowMetadata* window = state->get_window_metadata(255);
    (void)window;  // Implementation dependent - may return nullptr or wrap
}

TEST_F(CommunicatorStateTest, MarkOperationCompleteWithoutStart)
{
    // Complete without start - should not crash (underflow protection)
    uint8_t buffer_idx     = 0;
    WindowMetadata* window = state->get_window_metadata(buffer_idx);

    state->mark_operation_complete(buffer_idx);

    // Should either stay at 0 or handle underflow gracefully
    int final_count = window->in_progress_count.load();
    EXPECT_GE(final_count, 0);  // Should not underflow
}

TEST_F(CommunicatorStateTest, MultipleWindowClosings)
{
    uint8_t buffer_idx = 0;

    // First closing
    state->trigger_window_closing(buffer_idx);
    EXPECT_EQ(state->get_active_buffer_idx(), 1);

    // Try to close same buffer again (should be no-op or handle gracefully)
    state->trigger_window_closing(buffer_idx);
    EXPECT_EQ(state->get_active_buffer_idx(), 1);  // Should not change
}

TEST_F(CommunicatorStateTest, AllocateAfterBufferFull)
{
    // Fill the buffer to trigger count (WINDOW_TRIGGER_COUNT elements) - will auto-switch buffers
    std::vector<otelEventHandle_t*> slots;
    for (int i = 0; i < WINDOW_TRIGGER_COUNT; i++)
    {
        otelEventHandle_t* slot = state->allocate_event_slot();
        if (slot != nullptr)
        {
            slots.push_back(slot);
        }
    }

    // Window should have auto-switched to buffer 1
    EXPECT_EQ(state->get_active_buffer_idx(), 1);

    // Should be able to continue allocating in the new buffer
    otelEventHandle_t* next_slot = state->allocate_event_slot();
    EXPECT_NE(next_slot, nullptr);
}

TEST_F(CommunicatorStateTest, RapidWindowTransitions)
{
    // Rapidly transition through windows
    for (int iteration = 0; iteration < 100; iteration++)
    {
        uint8_t current = state->get_active_buffer_idx();
        state->trigger_window_closing(current);

        // Verify transition happened
        uint8_t next = state->get_active_buffer_idx();
        EXPECT_NE(current, next);
    }
}

TEST_F(CommunicatorStateTest, ZeroElementWindowClosing)
{
    // Close a window with zero elements
    uint8_t buffer_idx     = 0;
    WindowMetadata* window = state->get_window_metadata(buffer_idx);
    EXPECT_EQ(window->element_count.load(), 0);

    state->trigger_window_closing(buffer_idx);

    // Should immediately transition to PROCESSING (no in-progress operations)
    EXPECT_EQ(window->state.load(), WINDOW_PROCESSING);

    // Telemetry should have been notified
    EXPECT_EQ(get_notify_window_ready_call_count(), 1);
}

TEST_F(CommunicatorStateTest, ConcurrentWindowClosing)
{
    // Test concurrent closing from multiple threads
    const int num_threads = 10;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; t++)
    {
        threads.emplace_back(
            [this]()
            {
                for (int i = 0; i < 10; i++)
                {
                    uint8_t current = state->get_active_buffer_idx();
                    state->trigger_window_closing(current);
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                }
            });
    }

    for (auto& thread : threads)
    {
        thread.join();
    }

    // Should have transitioned through multiple windows without crash
    EXPECT_LT(state->get_active_buffer_idx(), NUM_BUFFERS);
}

TEST_F(CommunicatorStateTest, OperationTrackingUnderflow)
{
    uint8_t buffer_idx     = 0;
    WindowMetadata* window = state->get_window_metadata(buffer_idx);

    // Mark complete more times than start
    state->mark_operation_start(buffer_idx);
    state->mark_operation_complete(buffer_idx);
    state->mark_operation_complete(buffer_idx);  // Extra complete

    // Should not underflow negative
    EXPECT_GE(window->in_progress_count.load(), 0);
}

TEST_F(CommunicatorStateTest, StressTestAllocateAndFreeRepeatedly)
{
    // Stress test: repeatedly fill and switch buffers
    for (int cycle = 0; cycle < 10; cycle++)
    {
        // Fill current buffer
        for (int i = 0; i < BUFFER_SIZE / 2; i++)
        {
            otelEventHandle_t* slot = state->allocate_event_slot();
            ASSERT_NE(slot, nullptr) << "Failed at cycle " << cycle << ", slot " << i;
        }

        // Switch buffer
        uint8_t current = state->get_active_buffer_idx();
        state->trigger_window_closing(current);
    }

    // Should still be functional
    otelEventHandle_t* slot = state->allocate_event_slot();
    EXPECT_NE(slot, nullptr);
}

TEST_F(CommunicatorStateTest, ConcurrentAllocateAndClose)
{
    // Stress test with threads allocating while windows are closing
    std::atomic<bool> stop{false};
    std::atomic<int> alloc_count{0};
    std::atomic<int> close_count{0};

    auto allocator = [&]()
    {
        while (!stop.load())
        {
            otelEventHandle_t* slot = state->allocate_event_slot();
            if (slot)
            {
                alloc_count++;
            }
        }
    };

    auto closer = [&]()
    {
        while (!stop.load())
        {
            uint8_t idx = state->get_active_buffer_idx();
            state->trigger_window_closing(idx);
            close_count++;
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 3; i++)
    {
        threads.emplace_back(allocator);
    }
    threads.emplace_back(closer);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop.store(true);

    for (auto& t : threads)
    {
        t.join();
    }

    // Should have done work
    EXPECT_GT(alloc_count.load(), 0);
    EXPECT_GT(close_count.load(), 0);

    // Should still be in valid state
    EXPECT_LT(state->get_active_buffer_idx(), NUM_BUFFERS);
}

// ============================================================================
// Tests for automatic window transition on event filling (testing the bug fix)
// ============================================================================

// Test: Window automatically transitions when reaching WINDOW_TRIGGER_COUNT events
// and telemetry thread is notified
TEST_F(CommunicatorStateTest, AutomaticWindowTransitionOnEventFilling)
{
    uint8_t initial_buffer = state->get_active_buffer_idx();
    EXPECT_EQ(initial_buffer, 0);

    // Fill window with events up to trigger count (without marking operations)
    for (int i = 0; i < WINDOW_TRIGGER_COUNT; i++)
    {
        otelEventHandle_t* slot = state->allocate_event_slot();
        ASSERT_NE(slot, nullptr) << "Failed to allocate slot " << i;
    }

    // Window should have auto-switched and transitioned to PROCESSING
    // (since no operations were marked as in-progress)
    EXPECT_EQ(state->get_active_buffer_idx(), 1);  // Switched to buffer 1
    WindowMetadata* window0 = state->get_window_metadata(0);
    EXPECT_EQ(window0->state.load(), WINDOW_PROCESSING);
    EXPECT_EQ(window0->element_count.load(), WINDOW_TRIGGER_COUNT);

    // Verify telemetry thread was notified exactly once
    EXPECT_EQ(get_notify_window_ready_call_count(), 1);

    // Verify the notification was for the correct window
    const auto& calls = get_notify_window_ready_calls();
    ASSERT_EQ(calls.size(), 1u);
    EXPECT_EQ(calls[0].first, state);
    EXPECT_EQ(calls[0].second, initial_buffer);
}

// Test: Window automatically switches when buffer is full, continues to fill next buffer
TEST_F(CommunicatorStateTest, AutomaticWindowSwitchAndContinue)
{
    // Fill first window to trigger count (without marking in-progress)
    for (int i = 0; i < WINDOW_TRIGGER_COUNT; i++)
    {
        otelEventHandle_t* slot = state->allocate_event_slot();
        ASSERT_NE(slot, nullptr);
    }

    // Should have switched to buffer 1 and notified
    EXPECT_EQ(state->get_active_buffer_idx(), 1);
    EXPECT_EQ(get_notify_window_ready_call_count(), 1);

    // Continue adding events to new buffer
    for (int i = 0; i < 100; i++)
    {
        otelEventHandle_t* slot = state->allocate_event_slot();
        ASSERT_NE(slot, nullptr);
    }

    // New buffer should have 100 events
    WindowMetadata* window1 = state->get_window_metadata(1);
    EXPECT_EQ(window1->element_count.load(), 100);
    EXPECT_EQ(window1->state.load(), WINDOW_FILLING);

    // First window should be PROCESSING
    WindowMetadata* window0 = state->get_window_metadata(0);
    EXPECT_EQ(window0->state.load(), WINDOW_PROCESSING);
}

// Test: Multiple windows fill and transition automatically
TEST_F(CommunicatorStateTest, MultipleWindowAutomaticTransitions)
{
    // Fill first window (no in-progress tracking, so immediate transition)
    for (int i = 0; i < WINDOW_TRIGGER_COUNT; i++)
    {
        otelEventHandle_t* slot = state->allocate_event_slot();
        ASSERT_NE(slot, nullptr);
    }

    // Window 0 should be PROCESSING
    WindowMetadata* window0 = state->get_window_metadata(0);
    EXPECT_EQ(window0->state.load(), WINDOW_PROCESSING);
    EXPECT_EQ(get_notify_window_ready_call_count(), 1);

    // Fill second window
    for (int i = 0; i < WINDOW_TRIGGER_COUNT; i++)
    {
        otelEventHandle_t* slot = state->allocate_event_slot();
        ASSERT_NE(slot, nullptr);
    }

    // Window 1 should be PROCESSING
    WindowMetadata* window1 = state->get_window_metadata(1);
    EXPECT_EQ(window1->state.load(), WINDOW_PROCESSING);
    EXPECT_EQ(get_notify_window_ready_call_count(), 2);
}

// Test: Window transition with in-progress operations
TEST_F(CommunicatorStateTest, WindowTransitionWithInProgressOperations)
{
    // Simulate that ProxyOps exist (normal network scenario)
    // This prevents the "internal links only" force-close behavior
    state->windows[0].has_proxy_ops.store(true, std::memory_order_release);

    // Fill window with events and mark them as in-progress
    for (int i = 0; i < WINDOW_TRIGGER_COUNT; i++)
    {
        otelEventHandle_t* slot = state->allocate_event_slot();
        ASSERT_NE(slot, nullptr);
        state->mark_operation_start(0);
    }

    // Window should be CLOSING (not PROCESSING yet, because operations are in-progress)
    WindowMetadata* window0 = state->get_window_metadata(0);
    EXPECT_EQ(window0->state.load(), WINDOW_CLOSING);
    EXPECT_EQ(get_notify_window_ready_call_count(), 0);  // Not notified yet

    // Complete half the operations
    for (int i = 0; i < WINDOW_TRIGGER_COUNT / 2; i++)
    {
        state->mark_operation_complete(0);
    }

    // Should still be CLOSING
    EXPECT_EQ(window0->state.load(), WINDOW_CLOSING);
    EXPECT_EQ(get_notify_window_ready_call_count(), 0);  // Still not notified

    // Complete remaining operations
    for (int i = WINDOW_TRIGGER_COUNT / 2; i < WINDOW_TRIGGER_COUNT; i++)
    {
        state->mark_operation_complete(0);
    }

    // Now should be PROCESSING and notified
    EXPECT_EQ(window0->state.load(), WINDOW_PROCESSING);
    EXPECT_EQ(get_notify_window_ready_call_count(), 1);
}

// Test: No notification if window doesn't reach PROCESSING state
TEST_F(CommunicatorStateTest, NotificationSentOnProcessingTransition)
{
    // Manually close window without filling it
    // Since in_progress_count is 0, it should immediately transition to PROCESSING
    state->trigger_window_closing(0);

    WindowMetadata* window0 = state->get_window_metadata(0);
    EXPECT_EQ(window0->state.load(), WINDOW_PROCESSING);

    // Notification SHOULD have been sent
    EXPECT_EQ(get_notify_window_ready_call_count(), 1);
}

// Test: Window wraps around and continues working
TEST_F(CommunicatorStateTest, WindowWrapAroundWithAutoTransition)
{
    // Fill all 4 windows (without marking in-progress)
    for (int window = 0; window < NUM_BUFFERS; window++)
    {
        for (int i = 0; i < WINDOW_TRIGGER_COUNT; i++)
        {
            otelEventHandle_t* slot = state->allocate_event_slot();
            // May fail on last window if we've wrapped around and buffer isn't ready
            if (slot == nullptr && window == NUM_BUFFERS - 1)
            {
                // Expected - we've filled all buffers and wrapped back
                break;
            }
            ASSERT_NE(slot, nullptr);
        }
    }

    // All 4 windows should have transitioned to PROCESSING and notified
    EXPECT_EQ(get_notify_window_ready_call_count(), NUM_BUFFERS);

    // Active buffer should have wrapped back (but windows are still PROCESSING)
    uint8_t active = state->get_active_buffer_idx();
    EXPECT_LT(active, NUM_BUFFERS);

    // Verify windows are in PROCESSING state (at least the ones we filled)
    int processing_count = 0;
    for (int i = 0; i < NUM_BUFFERS; i++)
    {
        WindowMetadata* window = state->get_window_metadata(i);
        if (window->state.load() == WINDOW_PROCESSING)
        {
            processing_count++;
        }
    }
    EXPECT_GE(processing_count, NUM_BUFFERS - 1);  // At least 3 of 4 should be PROCESSING

    // Note: In real code, telemetry thread would process and reset windows to READY
    // But in this test, windows remain PROCESSING so further allocations may fail
}
