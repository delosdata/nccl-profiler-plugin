// SPDX-FileCopyrightText: 2025 Delos Data Inc
// SPDX-License-Identifier: LicenseRef-FSL-1.1-ALv2

#ifndef PROFILER_OTEL_H
#define PROFILER_OTEL_H

#include <sys/types.h>
#include <unistd.h>

#include <cstdint>

#include "nccl.h"
#include "nccl_common.h"
#include "plugin/nccl_profiler.h"
#include "plugin/profiler/profiler_v4.h"

// Make functions hidden - only accessible via plugin structure
#define OTEL_HIDDEN __attribute__((visibility("hidden")))

// Global log function pointer (set during otelProfilerInit)
extern ncclDebugLogger_t otel_log_func;

// Test interface functions for unit testing
#ifdef UNIT_TESTING
int getInitialized();
void setInitialized(int value);
double getStartTime();
void setStartTime(double value);
pid_t getPid();
void setPid(pid_t value);

// Function declaration for mocking
double gettime();
#endif  // UNIT_TESTING

// Logging macros that use NCCL's logging system with PROF/OTEL prefix
#define OTEL_WARN(FLAGS, fmt, ...)                                                                                     \
    if (otel_log_func)                                                                                                 \
    (*otel_log_func)(NCCL_LOG_WARN, (FLAGS), __FUNCTION__, __LINE__, "[PROFILER/OTEL] " fmt, ##__VA_ARGS__)

#define OTEL_INFO(FLAGS, fmt, ...)                                                                                     \
    if (otel_log_func)                                                                                                 \
    (*otel_log_func)(NCCL_LOG_INFO, (FLAGS), __FUNCTION__, __LINE__, "[PROFILER/OTEL] " fmt, ##__VA_ARGS__)

// Compile-time TRACE gating: if PROFILER_OTEL_ENABLE_TRACE is not defined, OTEL_TRACE compiles to a no-op.
#ifdef PROFILER_OTEL_ENABLE_TRACE
#define OTEL_TRACE(FLAGS, fmt, ...)                                                                                    \
    if (otel_log_func)                                                                                                 \
    (*otel_log_func)(NCCL_LOG_TRACE, (FLAGS), __FUNCTION__, __LINE__, "[PROFILER/OTEL] " fmt, ##__VA_ARGS__)
#else
#define OTEL_TRACE(FLAGS, fmt, ...)                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
    } while (0)
#endif

#ifdef __cplusplus
extern "C"
{
#endif  // __cplusplus

    /**
     * @brief Initialize the NCCL Profiler OTEL plugin.
     *
     * This function is called by NCCL to initialize the profiler plugin for a communicator.
     * It creates the plugin context, initializes circular buffers, and sets up telemetry
     * collection if enabled.
     *
     * @param[out] context Pointer to store the plugin context. Set to nullptr if plugin is disabled.
     * @param[out] eActivationMask Pointer to store the event activation mask. Determines which
     *                             NCCL events will be profiled (default: 0x1E = Coll + P2P + ProxyOp + ProxyStep).
     * @param[in] commName Name of the NCCL communicator (for identification in metrics).
     * @param[in] commHash Hash of the NCCL communicator (unique identifier).
     * @param[in] nNodes Number of nodes in the communicator.
     * @param[in] nranks Total number of ranks in the communicator.
     * @param[in] rank Rank of the current process.
     * @param[in] logfn NCCL logging function for plugin logging.
     *
     * @return ncclSuccess on success, ncclError on failure.
     *
     * @note The plugin can be disabled via NCCL_PROFILER_OTEL_ENABLE=0 environment variable.
     * @note Telemetry is initialized on the first communicator initialization.
     */
    OTEL_HIDDEN ncclResult_t profiler_otel_init_v4(void** context, int* eActivationMask, const char* commName,
                                                   uint64_t commHash, int nNodes, int nranks, int rank,
                                                   ncclDebugLogger_t logfn);

    /**
     * @brief Start a new profiling event.
     *
     * Called by NCCL when a profiled event starts (Coll, P2P, ProxyOp, ProxyStep, Group).
     * Allocates an event handle from the circular buffer and initializes event data.
     *
     * @param[in] context Plugin context from profiler_otel_init_v4().
     * @param[out] eHandle Pointer to store the event handle. Set to nullptr if event is filtered.
     * @param[in] eDescr Event descriptor containing event type and type-specific data.
     *
     * @return ncclSuccess on success (even if event is filtered).
     *
     * @note Events are filtered based on type (ProxyCtrl, receive ProxyOps are skipped).
     * @note P2P Recv events are skipped (only Send is tracked).
     * @note Event handle is allocated from a lock-free circular buffer.
     */
    OTEL_HIDDEN ncclResult_t profiler_otel_start_event_v4(void* context, void** eHandle,
                                                          ncclProfilerEventDescr_v4_t* eDescr);

    /**
     * @brief Stop a profiling event.
     *
     * Called by NCCL when a profiled event completes. Records the end timestamp and
     * updates window management state for ProxyOp events.
     *
     * @param[in] eHandle Event handle from profiler_otel_start_event_v4().
     *
     * @return ncclSuccess on success.
     *
     * @note If eHandle is nullptr, the function returns successfully (event was filtered).
     * @note For ProxyOp events, this updates window in-progress accounting used for window boundary handling.
     */
    OTEL_HIDDEN ncclResult_t profiler_otel_stop_event_v4(void* eHandle);

    /**
     * @brief Record event state transition.
     *
     * Called by NCCL to record state changes in ProxyStep events. Used to capture
     * the actual transfer start time (SendWait state) and transfer size.
     *
     * @param[in] eHandle Event handle from profiler_otel_start_event_v4().
     * @param[in] eState Event state (e.g., ProxyStepSendWait, ProxyStepSendPeerWait).
     * @param[in] eStateArgs State-specific arguments (e.g., transfer size for SendWait).
     *
     * @return ncclSuccess on success.
     *
     * @note For ProxyStep events, SendWait captures the timestamp used as the transfer start.
     * @note Transfer size is recorded from state args; transfer metrics are only computed when SendWait was observed.
     */
    OTEL_HIDDEN ncclResult_t profiler_otel_record_event_state_v4(void* eHandle, ncclProfilerEventState_v4_t eState,
                                                                 ncclProfilerEventStateArgs_v4_t* eStateArgs);

    /**
     * @brief Finalize the profiler plugin for a communicator.
     *
     * Called by NCCL when a communicator is destroyed. Cleans up plugin context and
     * communicator state. Telemetry is cleaned up when the last communicator is finalized.
     *
     * @param[in] context Plugin context from profiler_otel_init_v4().
     *
     * @return ncclSuccess on success.
     *
     * @note Telemetry thread is stopped only when the last communicator is finalized.
     */
    OTEL_HIDDEN ncclResult_t profiler_otel_finalize_v4(void* context);

#ifdef __cplusplus
}
#endif  // __cplusplus

#endif  // PROFILER_OTEL_H
