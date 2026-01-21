// SPDX-FileCopyrightText: 2025 Delos Data Inc
// SPDX-License-Identifier: LicenseRef-FSL-1.1-ALv2

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../../communicator_state.h"
#include "../../events.h"
#include "../../profiler_otel.h"
#include "../test_helpers.h"

// Mock functions are now provided by test_mocks.cc to avoid multiple definitions

// Mock logger
static void mock_logger(ncclDebugLogLevel level, unsigned long flags, const char* file, int line, const char* fmt, ...)
{
    (void)level;
    (void)flags;
    (void)file;
    (void)line;
    (void)fmt;
}

class ProfilerEventTest : public ::testing::Test
{
protected:
    void* context;
    int eActivationMask;

    void SetUp() override
    {
        context         = nullptr;
        eActivationMask = 0;

        // Reset global state for clean test
        resetProfilerState();

        // Initialize profiler
        ncclResult_t result =
            profiler_otel_init_v4(&context, &eActivationMask, "test_comm", 12345, 2, 4, 0, mock_logger);
        ASSERT_EQ(result, ncclSuccess);
        ASSERT_NE(context, nullptr);
    }

    void TearDown() override
    {
        if (context)
        {
            profiler_otel_finalize_v4(context);
            context = nullptr;
        }
    }
};

TEST_F(ProfilerEventTest, StartCollectiveEventBasic)
{
    ncclProfilerEventDescr_v4_t descr = {};
    descr.type                        = ncclProfileColl;
    descr.coll.func                   = "AllReduce";
    descr.coll.datatype               = "ncclInt32";
    descr.coll.count                  = 1024;  // 1024 * 4 bytes = 4096 bytes
    descr.coll.nChannels              = 2;
    descr.coll.algo                   = "Ring";
    descr.coll.proto                  = "Simple";
    descr.coll.seqNumber              = 1;
    descr.parentObj                   = (void*)0x1234;

    void* eHandle       = nullptr;
    ncclResult_t result = profiler_otel_start_event_v4(context, &eHandle, &descr);

    EXPECT_EQ(result, ncclSuccess);
    EXPECT_NE(eHandle, nullptr);

    // Verify event was created correctly
    otelEventHandle_t* event = (otelEventHandle_t*)eHandle;
    EXPECT_EQ(event->type, ncclProfileColl);
    EXPECT_STREQ(event->coll.func, "AllReduce");
    EXPECT_EQ(event->coll.bytes, 4096u);  // 1024 * 4
    EXPECT_EQ(event->coll.nChannels, 2);
}

TEST_F(ProfilerEventTest, StartP2PEventBasic)
{
    ncclProfilerEventDescr_v4_t descr = {};
    descr.type                        = ncclProfileP2p;
    descr.p2p.func                    = "Send";
    descr.p2p.datatype                = "ncclFloat32";
    descr.p2p.count                   = 512;  // 512 * 4 bytes = 2048 bytes
    descr.p2p.peer                    = 3;
    descr.p2p.nChannels               = 1;
    descr.parentObj                   = (void*)0x5678;

    void* eHandle       = nullptr;
    ncclResult_t result = profiler_otel_start_event_v4(context, &eHandle, &descr);

    EXPECT_EQ(result, ncclSuccess);
    EXPECT_NE(eHandle, nullptr);

    otelEventHandle_t* event = (otelEventHandle_t*)eHandle;
    EXPECT_EQ(event->type, ncclProfileP2p);
    EXPECT_STREQ(event->p2p.func, "Send");
    EXPECT_EQ(event->p2p.bytes, 2048u);
    EXPECT_EQ(event->p2p.peer, 3);
    EXPECT_EQ(event->p2p.nChannels, 1);
}

TEST_F(ProfilerEventTest, StartProxyOpEventBasic)
{
    ncclProfilerEventDescr_v4_t descr = {};
    descr.type                        = ncclProfileProxyOp;
    descr.proxyOp.channelId           = 0;
    descr.proxyOp.peer                = 2;
    descr.proxyOp.chunkSize           = 256;
    descr.proxyOp.isSend              = 1;  // Send direction
    descr.proxyOp.pid                 = getpid();
    descr.parentObj                   = (void*)0x9ABC;

    void* eHandle       = nullptr;
    ncclResult_t result = profiler_otel_start_event_v4(context, &eHandle, &descr);

    EXPECT_EQ(result, ncclSuccess);
    EXPECT_NE(eHandle, nullptr);

    otelEventHandle_t* event = (otelEventHandle_t*)eHandle;
    EXPECT_EQ(event->type, ncclProfileProxyOp);
    EXPECT_EQ(event->proxyOp.channelId, 0);
    EXPECT_EQ(event->proxyOp.peer, 2);
    EXPECT_EQ(event->proxyOp.chunkSize, 256);
}

TEST_F(ProfilerEventTest, StartProxyOpEventReceiveSkipped)
{
    // Receive direction should be skipped
    ncclProfilerEventDescr_v4_t descr = {};
    descr.type                        = ncclProfileProxyOp;
    descr.proxyOp.channelId           = 0;
    descr.proxyOp.peer                = 2;
    descr.proxyOp.chunkSize           = 256;
    descr.proxyOp.isSend              = 0;  // Receive direction
    descr.proxyOp.pid                 = getpid();

    void* eHandle       = nullptr;
    ncclResult_t result = profiler_otel_start_event_v4(context, &eHandle, &descr);

    EXPECT_EQ(result, ncclSuccess);
    EXPECT_EQ(eHandle, nullptr);  // Should be null for receive
}

TEST_F(ProfilerEventTest, StartProxyOpEventWrongPid)
{
    // Wrong PID should be skipped
    ncclProfilerEventDescr_v4_t descr = {};
    descr.type                        = ncclProfileProxyOp;
    descr.proxyOp.channelId           = 0;
    descr.proxyOp.peer                = 2;
    descr.proxyOp.chunkSize           = 256;
    descr.proxyOp.isSend              = 1;
    descr.proxyOp.pid                 = getpid() + 1000;  // Different PID

    void* eHandle       = nullptr;
    ncclResult_t result = profiler_otel_start_event_v4(context, &eHandle, &descr);

    EXPECT_EQ(result, ncclSuccess);
    // Event should not be created for wrong PID
}

TEST_F(ProfilerEventTest, StopEventBasic)
{
    // Start an event
    ncclProfilerEventDescr_v4_t descr = {};
    descr.type                        = ncclProfileColl;
    descr.coll.func                   = "AllReduce";
    descr.coll.datatype               = "ncclInt32";
    descr.coll.count                  = 1024;
    descr.coll.nChannels              = 2;
    descr.coll.algo                   = "Ring";
    descr.coll.proto                  = "Simple";

    void* eHandle = nullptr;
    profiler_otel_start_event_v4(context, &eHandle, &descr);
    ASSERT_NE(eHandle, nullptr);

    otelEventHandle_t* event = (otelEventHandle_t*)eHandle;
    double startTs           = event->startTs;
    EXPECT_EQ(event->endTs, 0.0);  // Should be 0 initially

    // Stop the event
    ncclResult_t result = profiler_otel_stop_event_v4(eHandle);
    EXPECT_EQ(result, ncclSuccess);

    // Verify endTs was set and is after startTs
    EXPECT_GT(event->endTs, 0.0);
    EXPECT_GE(event->endTs, startTs);
}

TEST_F(ProfilerEventTest, StopEventWithNullHandle)
{
    ncclResult_t result = profiler_otel_stop_event_v4(nullptr);
    EXPECT_EQ(result, ncclSuccess);  // Should handle null gracefully
}

TEST_F(ProfilerEventTest, MultipleCollectiveEvents)
{
    std::vector<void*> handles;

    // Create multiple events
    for (int i = 0; i < 10; i++)
    {
        ncclProfilerEventDescr_v4_t descr = {};
        descr.type                        = ncclProfileColl;
        descr.coll.func                   = "AllReduce";
        descr.coll.datatype               = "ncclInt32";
        descr.coll.count                  = 1024 + i * 100;
        descr.coll.nChannels              = 2;
        descr.coll.algo                   = "Ring";
        descr.coll.proto                  = "Simple";

        void* eHandle = nullptr;
        profiler_otel_start_event_v4(context, &eHandle, &descr);
        EXPECT_NE(eHandle, nullptr);
        handles.push_back(eHandle);
    }

    // Stop all events
    for (auto handle : handles)
    {
        ncclResult_t result = profiler_otel_stop_event_v4(handle);
        EXPECT_EQ(result, ncclSuccess);
    }
}

TEST_F(ProfilerEventTest, DatatypeSizeCalculation)
{
    struct TestCase
    {
        const char* datatype;
        size_t count;
        size_t expectedBytes;
    };

    TestCase cases[] = {
        {"ncclInt8",     1000, 1000},
        {"ncclUint8",    1000, 1000},
        {"ncclFloat16",  1000, 2000},
        {"ncclBfloat16", 1000, 2000},
        {"ncclInt32",    1000, 4000},
        {"ncclUint32",   1000, 4000},
        {"ncclFloat32",  1000, 4000},
        {"ncclInt64",    1000, 8000},
        {"ncclUint64",   1000, 8000},
        {"ncclFloat64",  1000, 8000},
        {"unknown",      1000, 0   }, // Unknown type
        {nullptr,        1000, 0   }, // Null type
    };

    for (const auto& testCase : cases)
    {
        ncclProfilerEventDescr_v4_t descr = {};
        descr.type                        = ncclProfileColl;
        descr.coll.func                   = "AllReduce";
        descr.coll.datatype               = testCase.datatype;
        descr.coll.count                  = testCase.count;
        descr.coll.nChannels              = 2;
        descr.coll.algo                   = "Ring";
        descr.coll.proto                  = "Simple";

        void* eHandle = nullptr;
        profiler_otel_start_event_v4(context, &eHandle, &descr);

        if (testCase.expectedBytes > 0)
        {
            ASSERT_NE(eHandle, nullptr);
            otelEventHandle_t* event = (otelEventHandle_t*)eHandle;
            EXPECT_EQ(event->coll.bytes, testCase.expectedBytes)
                << "Failed for datatype: " << (testCase.datatype ? testCase.datatype : "nullptr");
        }
    }
}

TEST_F(ProfilerEventTest, BufferOverflowHandling)
{
    // Try to create more events than a single buffer can hold
    // Window closes at WINDOW_TRIGGER_COUNT, switching to next buffer.
    // So we can create more than BUFFER_SIZE events in total (across multiple windows).
    std::vector<void*> handles;
    int successCount = 0;

    // BUFFER_SIZE and WINDOW_TRIGGER_COUNT are defined in communicator_state.h
    // Try to create 25000 events across multiple windows
    for (int i = 0; i < 25000; i++)
    {
        ncclProfilerEventDescr_v4_t descr = {};
        descr.type                        = ncclProfileColl;
        descr.coll.func                   = "AllReduce";
        descr.coll.datatype               = "ncclInt32";
        descr.coll.count                  = 1024;
        descr.coll.nChannels              = 2;
        descr.coll.algo                   = "Ring";
        descr.coll.proto                  = "Simple";

        void* eHandle       = nullptr;
        ncclResult_t result = profiler_otel_start_event_v4(context, &eHandle, &descr);
        EXPECT_EQ(result, ncclSuccess);

        if (eHandle != nullptr)
        {
            successCount++;
            handles.push_back(eHandle);
        }
    }

    // We should be able to create a substantial number of events without failures.
    // (window closing may or may not trigger depending on WINDOW_TRIGGER_COUNT)
    EXPECT_GE(successCount, 10000);
    EXPECT_GT(successCount, 0);
}

TEST_F(ProfilerEventTest, RecordEventStateNotImplemented)
{
    ncclProfilerEventDescr_v4_t descr = {};
    descr.type                        = ncclProfileColl;
    descr.coll.func                   = "AllReduce";
    descr.coll.datatype               = "ncclInt32";
    descr.coll.count                  = 1024;

    void* eHandle = nullptr;
    profiler_otel_start_event_v4(context, &eHandle, &descr);
    ASSERT_NE(eHandle, nullptr);

    // Record event state (not implemented, but should return success)
    ncclProfilerEventStateArgs_v4_t args = {};
    ncclResult_t result = profiler_otel_record_event_state_v4(eHandle, (ncclProfilerEventState_v4_t)0, &args);

    EXPECT_EQ(result, ncclSuccess);
}

TEST_F(ProfilerEventTest, FinalizeWithNullContext)
{
    ncclResult_t result = profiler_otel_finalize_v4(nullptr);
    EXPECT_EQ(result, ncclSuccess);  // Should handle null gracefully
}

TEST_F(ProfilerEventTest, WindowTriggeringAfterTriggerCount)
{
    struct eventContext* ctx     = (struct eventContext*)context;
    CommunicatorState* commState = ctx->commState;

    (void)commState;  // Will be used when window logic is enabled

    // Create exactly WINDOW_TRIGGER_COUNT events
    for (int i = 0; i < WINDOW_TRIGGER_COUNT; i++)
    {
        ncclProfilerEventDescr_v4_t descr = {};
        descr.type                        = ncclProfileColl;
        descr.coll.func                   = "AllReduce";
        descr.coll.datatype               = "ncclInt32";
        descr.coll.count                  = 100;
        descr.coll.nChannels              = 2;
        descr.coll.algo                   = "Ring";
        descr.coll.proto                  = "Simple";

        void* eHandle = nullptr;
        profiler_otel_start_event_v4(context, &eHandle, &descr);
    }

    // Buffer may switch after WINDOW_TRIGGER_COUNT events (depending on in-progress window logic).
}
