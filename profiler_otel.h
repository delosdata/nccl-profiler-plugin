#ifndef PROFILER_OTEL_H
#define PROFILER_OTEL_H

#include <sys/types.h>
#include <unistd.h>

#include <cstdint>

#include "nccl.h"
#include "nccl_common.h"
#include "plugin/nccl_profiler.h"
#include "plugin/profiler/profiler_v4.h"

// Newer NCCL profiler interfaces. They are compiled in when the NCCL headers the plugin is built against provide
// them (profiler v5: NCCL 2.28+, v6: copy-engine collective events, v7: NCCL 2.31+). NCCL loads the newest version a
// plugin exports and falls back to older ones, so the plugin keeps exporting v4 for older NCCL releases.
#if defined(__has_include)
#if __has_include("plugin/profiler/profiler_v5.h")
#include "plugin/profiler/profiler_v5.h"
#define OTEL_HAS_PROFILER_V5 1
#endif
#if __has_include("plugin/profiler/profiler_v6.h")
#include "plugin/profiler/profiler_v6.h"
#define OTEL_HAS_PROFILER_V6 1
#endif
#if __has_include("plugin/profiler/profiler_v7.h")
#include "plugin/profiler/profiler_v7.h"
#define OTEL_HAS_PROFILER_V7 1
#endif
#endif  // __has_include
#ifndef OTEL_HAS_PROFILER_V5
#define OTEL_HAS_PROFILER_V5 0
#endif
#ifndef OTEL_HAS_PROFILER_V6
#define OTEL_HAS_PROFILER_V6 0
#endif
#ifndef OTEL_HAS_PROFILER_V7
#define OTEL_HAS_PROFILER_V7 0
#endif

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

#if OTEL_HAS_PROFILER_V5
    /**
     * @brief Profiler v5 entry points (NCCL 2.28+).
     *
     * v5 passes the communicator id before the activation mask and widens the event type to 64 bits. Events that
     * exist in v4 are translated and handled by the v4 code path; API-level events (GroupApi, CollApi, P2pApi,
     * KernelLaunch) are not used for telemetry and get no handle.
     */
    OTEL_HIDDEN ncclResult_t profiler_otel_init_v5(void** context, uint64_t commId, int* eActivationMask,
                                                   const char* commName, int nNodes, int nranks, int rank,
                                                   ncclDebugLogger_t logfn);
    OTEL_HIDDEN ncclResult_t profiler_otel_start_event_v5(void* context, void** eHandle,
                                                          ncclProfilerEventDescr_v5_t* eDescr);
    OTEL_HIDDEN ncclResult_t profiler_otel_record_event_state_v5(void* eHandle, ncclProfilerEventState_v5_t eState,
                                                                 ncclProfilerEventStateArgs_v5_t* eStateArgs);
#endif  // OTEL_HAS_PROFILER_V5

#if OTEL_HAS_PROFILER_V6
    /**
     * @brief Profiler v6 entry points (adds copy-engine collective events).
     *
     * CE collectives are recorded as collectives with algo "CE" (see profiler_compat.h). When the user has not set
     * NCCL_PROFILE_EVENT_MASK, ncclProfileCeColl is added to the default activation mask.
     */
    OTEL_HIDDEN ncclResult_t profiler_otel_init_v6(void** context, uint64_t commId, int* eActivationMask,
                                                   const char* commName, int nNodes, int nranks, int rank,
                                                   ncclDebugLogger_t logfn);
    OTEL_HIDDEN ncclResult_t profiler_otel_start_event_v6(void* context, void** eHandle,
                                                          ncclProfilerEventDescr_v6_t* eDescr);
    OTEL_HIDDEN ncclResult_t profiler_otel_record_event_state_v6(void* eHandle, ncclProfilerEventState_v6_t eState,
                                                                 ncclProfilerEventStateArgs_v6_t* eStateArgs);
#endif  // OTEL_HAS_PROFILER_V6

#if OTEL_HAS_PROFILER_V7
    /**
     * @brief Profiler v7 entry points (NCCL 2.31+). Same handling as v6; kernel phase events are ignored.
     */
    OTEL_HIDDEN ncclResult_t profiler_otel_start_event_v7(void* context, void** eHandle,
                                                          ncclProfilerEventDescr_v7_t* eDescr);
    OTEL_HIDDEN ncclResult_t profiler_otel_record_event_state_v7(void* eHandle, ncclProfilerEventState_v7_t eState,
                                                                 ncclProfilerEventStateArgs_v7_t* eStateArgs);
#endif  // OTEL_HAS_PROFILER_V7

#ifdef __cplusplus
}
#endif  // __cplusplus

#endif  // PROFILER_OTEL_H
