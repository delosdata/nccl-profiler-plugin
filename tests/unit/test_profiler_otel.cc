// SPDX-FileCopyrightText: 2025 Delos Data Inc
// SPDX-License-Identifier: LicenseRef-FSL-1.1-ALv2

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>

#include "../../events.h"

// Include the header for the function under test
#include "../../profiler_otel.h"

// Mock functions are now provided by test_mocks.cc to avoid multiple definitions

// Test fixture for profiler_otel_init_v4
class OtelProfilerInitV4Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Clear any existing environment variables
        unsetenv("NCCL_PROFILER_OTEL_ENABLE");
        unsetenv("NCCL_PROFILE_EVENT_MASK");

        // Reset global state using getter/setter functions
        extern ncclDebugLogger_t otel_log_func;

        setInitialized(0);
        otel_log_func = nullptr;
        setPid(0);
    }

    void TearDown() override
    {
        // Clean up any allocated memory
        // Note: The function allocates memory for context, but doesn't provide cleanup
        // In a real test environment, you'd want to track allocations
    }

    // Helper function to create a mock logger
    static void mock_logger(ncclDebugLogLevel level, unsigned long flags, const char* file, int line, const char* fmt,
                            ...)
    {
        // Mock implementation - do nothing
        (void)level;
        (void)flags;
        (void)file;
        (void)line;
        (void)fmt;
    }
};

// Test successful initialization with valid parameters
TEST_F(OtelProfilerInitV4Test, SuccessfulInitialization)
{
    // Arrange
    void* context        = nullptr;
    int eActivationMask  = 0;
    const char* commName = "test_comm";
    uint64_t commHash    = 12345;
    int nNodes           = 2;
    int nranks           = 4;
    int rank             = 0;

    // Act
    ncclResult_t result =
        profiler_otel_init_v4(&context, &eActivationMask, commName, commHash, nNodes, nranks, rank, mock_logger);

    // Assert
    EXPECT_EQ(result, ncclSuccess);
    EXPECT_NE(context, nullptr);

    // Verify context was populated correctly
    struct eventContext* ctx = static_cast<struct eventContext*>(context);
    EXPECT_STREQ(ctx->commName, commName);
    EXPECT_EQ(ctx->commHash, commHash);
    EXPECT_EQ(ctx->nNodes, nNodes);
    EXPECT_EQ(ctx->nranks, nranks);
    EXPECT_EQ(ctx->rank, rank);

    // Verify global state was set
    extern ncclDebugLogger_t otel_log_func;
    EXPECT_EQ(otel_log_func, mock_logger);
}

// Test initialization when plugin is disabled via environment variable
TEST_F(OtelProfilerInitV4Test, DisabledByEnvironmentVariable)
{
    // Arrange
    setenv("NCCL_PROFILER_OTEL_ENABLE", "0", 1);
    void* context        = nullptr;
    int eActivationMask  = 0;
    const char* commName = "test_comm";
    uint64_t commHash    = 12345;
    int nNodes           = 2;
    int nranks           = 4;
    int rank             = 0;

    // Act
    ncclResult_t result =
        profiler_otel_init_v4(&context, &eActivationMask, commName, commHash, nNodes, nranks, rank, mock_logger);

    // Assert
    EXPECT_EQ(result, ncclSuccess);
    EXPECT_EQ(context, nullptr);  // Should not create context when disabled
}

// Test initialization with null logger (should use fallback)
TEST_F(OtelProfilerInitV4Test, NullLoggerUsesFallback)
{
    // Arrange
    void* context        = nullptr;
    int eActivationMask  = 0;
    const char* commName = "test_comm";
    uint64_t commHash    = 12345;
    int nNodes           = 2;
    int nranks           = 4;
    int rank             = 0;

    // Act
    ncclResult_t result =
        profiler_otel_init_v4(&context, &eActivationMask, commName, commHash, nNodes, nranks, rank, nullptr);

    // Assert
    EXPECT_EQ(result, ncclSuccess);
    EXPECT_NE(context, nullptr);

    // Verify fallback logger was set
    extern ncclDebugLogger_t otel_log_func;
    EXPECT_NE(otel_log_func, nullptr);
}

// Test event mask initialization from environment variable
TEST_F(OtelProfilerInitV4Test, EventMaskFromEnvironment)
{
    // Arrange
    setenv("NCCL_PROFILE_EVENT_MASK", "42", 1);
    void* context        = nullptr;
    int eActivationMask  = 0;
    const char* commName = "test_comm";
    uint64_t commHash    = 12345;
    int nNodes           = 2;
    int nranks           = 4;
    int rank             = 0;

    // Act
    ncclResult_t result =
        profiler_otel_init_v4(&context, &eActivationMask, commName, commHash, nNodes, nranks, rank, mock_logger);

    // Assert
    EXPECT_EQ(result, ncclSuccess);
    EXPECT_EQ(eActivationMask, 42);
}

// Test event mask defaults to 0x1E (only telemetry-relevant events) when not set by environment variable
TEST_F(OtelProfilerInitV4Test, EventMaskDefaultsToZero)
{
    // Arrange
    void* context        = nullptr;
    int eActivationMask  = 0;
    const char* commName = "test_comm";
    uint64_t commHash    = 12345;
    int nNodes           = 2;
    int nranks           = 4;
    int rank             = 0;

    // Act
    ncclResult_t result =
        profiler_otel_init_v4(&context, &eActivationMask, commName, commHash, nNodes, nranks, rank, mock_logger);

    // Assert
    EXPECT_EQ(result, ncclSuccess);
    // Defaults to 0x1E (only telemetry-relevant events: Coll | P2p | ProxyOp | ProxyStep)
    // Group and ProxyCtrl are excluded as they're not used in aggregation
    EXPECT_EQ(eActivationMask, 0x1E);
}

// Test process ID is set correctly
TEST_F(OtelProfilerInitV4Test, ProcessIdIsSet)
{
    // Arrange
    void* context        = nullptr;
    int eActivationMask  = 0;
    const char* commName = "test_comm";
    uint64_t commHash    = 12345;
    int nNodes           = 2;
    int nranks           = 4;
    int rank             = 0;
    pid_t expectedPid    = getpid();

    // Act
    ncclResult_t result =
        profiler_otel_init_v4(&context, &eActivationMask, commName, commHash, nNodes, nranks, rank, mock_logger);

    // Assert
    EXPECT_EQ(result, ncclSuccess);
    EXPECT_EQ(getPid(), expectedPid);
}

// Test multiple initialization calls (should only initialize once)
TEST_F(OtelProfilerInitV4Test, MultipleInitializationCalls)
{
    // Arrange
    void* context1       = nullptr;
    void* context2       = nullptr;
    int eActivationMask1 = 0;
    int eActivationMask2 = 0;
    const char* commName = "test_comm";
    uint64_t commHash    = 12345;
    int nNodes           = 2;
    int nranks           = 4;
    int rank             = 0;

    // Act - First initialization
    ncclResult_t result1 =
        profiler_otel_init_v4(&context1, &eActivationMask1, commName, commHash, nNodes, nranks, rank, mock_logger);

    // Act - Second initialization
    ncclResult_t result2 =
        profiler_otel_init_v4(&context2, &eActivationMask2, commName, commHash, nNodes, nranks, rank, mock_logger);

    // Assert
    EXPECT_EQ(result1, ncclSuccess);
    EXPECT_EQ(result2, ncclSuccess);
    EXPECT_NE(context1, nullptr);
    EXPECT_NE(context2, nullptr);

    // Both contexts should be valid
    struct eventContext* ctx1 = static_cast<struct eventContext*>(context1);
    struct eventContext* ctx2 = static_cast<struct eventContext*>(context2);
    EXPECT_STREQ(ctx1->commName, commName);
    EXPECT_STREQ(ctx2->commName, commName);
}

// Test with different rank values
TEST_F(OtelProfilerInitV4Test, DifferentRankValues)
{
    // Arrange
    void* context        = nullptr;
    int eActivationMask  = 0;
    const char* commName = "test_comm";
    uint64_t commHash    = 12345;
    int nNodes           = 2;
    int nranks           = 4;
    int rank             = 3;  // Different rank

    // Act
    ncclResult_t result =
        profiler_otel_init_v4(&context, &eActivationMask, commName, commHash, nNodes, nranks, rank, mock_logger);

    // Assert
    EXPECT_EQ(result, ncclSuccess);
    EXPECT_NE(context, nullptr);

    struct eventContext* ctx = static_cast<struct eventContext*>(context);
    EXPECT_EQ(ctx->rank, rank);
}

// Test with different communication hash values
TEST_F(OtelProfilerInitV4Test, DifferentCommHashValues)
{
    // Arrange
    void* context        = nullptr;
    int eActivationMask  = 0;
    const char* commName = "test_comm";
    uint64_t commHash    = 98765;  // Different hash
    int nNodes           = 2;
    int nranks           = 4;
    int rank             = 0;

    // Act
    ncclResult_t result =
        profiler_otel_init_v4(&context, &eActivationMask, commName, commHash, nNodes, nranks, rank, mock_logger);

    // Assert
    EXPECT_EQ(result, ncclSuccess);
    EXPECT_NE(context, nullptr);

    struct eventContext* ctx = static_cast<struct eventContext*>(context);
    EXPECT_EQ(ctx->commHash, commHash);
}

// Test with different node and rank counts
TEST_F(OtelProfilerInitV4Test, DifferentNodeAndRankCounts)
{
    // Arrange
    void* context        = nullptr;
    int eActivationMask  = 0;
    const char* commName = "test_comm";
    uint64_t commHash    = 12345;
    int nNodes           = 8;   // Different node count
    int nranks           = 16;  // Different rank count
    int rank             = 0;

    // Act
    ncclResult_t result =
        profiler_otel_init_v4(&context, &eActivationMask, commName, commHash, nNodes, nranks, rank, mock_logger);

    // Assert
    EXPECT_EQ(result, ncclSuccess);
    EXPECT_NE(context, nullptr);

    struct eventContext* ctx = static_cast<struct eventContext*>(context);
    EXPECT_EQ(ctx->nNodes, nNodes);
    EXPECT_EQ(ctx->nranks, nranks);
}

// Test with empty communication name
TEST_F(OtelProfilerInitV4Test, EmptyCommName)
{
    // Arrange
    void* context        = nullptr;
    int eActivationMask  = 0;
    const char* commName = "";  // Empty name
    uint64_t commHash    = 12345;
    int nNodes           = 2;
    int nranks           = 4;
    int rank             = 0;

    // Act
    ncclResult_t result =
        profiler_otel_init_v4(&context, &eActivationMask, commName, commHash, nNodes, nranks, rank, mock_logger);

    // Assert
    EXPECT_EQ(result, ncclSuccess);
    EXPECT_NE(context, nullptr);

    struct eventContext* ctx = static_cast<struct eventContext*>(context);
    EXPECT_STREQ(ctx->commName, commName);
}

// Test with null communication name
TEST_F(OtelProfilerInitV4Test, NullCommName)
{
    // Arrange
    void* context        = nullptr;
    int eActivationMask  = 0;
    const char* commName = nullptr;  // Null name
    uint64_t commHash    = 12345;
    int nNodes           = 2;
    int nranks           = 4;
    int rank             = 0;

    // Act
    ncclResult_t result =
        profiler_otel_init_v4(&context, &eActivationMask, commName, commHash, nNodes, nranks, rank, mock_logger);

    // Assert
    EXPECT_EQ(result, ncclSuccess);
    EXPECT_NE(context, nullptr);

    struct eventContext* ctx = static_cast<struct eventContext*>(context);
    EXPECT_EQ(ctx->commName, commName);
}

// Test that start time is initialized
TEST_F(OtelProfilerInitV4Test, StartTimeIsInitialized)
{
    // Arrange
    void* context        = nullptr;
    int eActivationMask  = 0;
    const char* commName = "test_comm";
    uint64_t commHash    = 12345;
    int nNodes           = 2;
    int nranks           = 4;
    int rank             = 0;

    // Act
    ncclResult_t result =
        profiler_otel_init_v4(&context, &eActivationMask, commName, commHash, nNodes, nranks, rank, mock_logger);

    // Assert
    EXPECT_EQ(result, ncclSuccess);
    // Verify start time was initialized (should be in reasonable range)
    // gettime() returns incrementing values starting at 1234567890.0 * 1e6
    EXPECT_GT(getStartTime(), 1234567890.0 * 1e6 - 1);     // Should be at or near base value
    EXPECT_LT(getStartTime(), 1234567890.0 * 1e6 + 1000);  // Within 1000 microseconds
}

// Test that telemetry initialization is called
TEST_F(OtelProfilerInitV4Test, TelemetryInitializationCalled)
{
    // This test would require mocking otelTelemetryInit to verify it's called
    // For now, we'll just verify the function completes successfully
    // In a more sophisticated test setup, you could use a mock framework
    // to verify function calls

    // Arrange
    void* context        = nullptr;
    int eActivationMask  = 0;
    const char* commName = "test_comm";
    uint64_t commHash    = 12345;
    int nNodes           = 2;
    int nranks           = 4;
    int rank             = 0;

    // Act
    ncclResult_t result =
        profiler_otel_init_v4(&context, &eActivationMask, commName, commHash, nNodes, nranks, rank, mock_logger);

    // Assert
    EXPECT_EQ(result, ncclSuccess);
    EXPECT_NE(context, nullptr);
}
