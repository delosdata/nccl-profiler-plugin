#include <sys/types.h>
#include <unistd.h>

#include "profiler_otel.h"

// NCCL looks up the newest profiler symbol first (ncclProfiler_v7, then v6, v5, v4, ...) and uses the first one the
// plugin exports. The newer structures are only compiled in when the NCCL headers provide them; v4 is always exported
// so older NCCL releases (e.g. 2.27) keep working.
extern "C"
{
    // NCCL Profiler Plugin v4 structure
    volatile ncclProfiler_v4_t ncclProfiler_v4 = {
        "otel-profiler",                      // name
        profiler_otel_init_v4,                // init
        profiler_otel_start_event_v4,         // startEvent
        profiler_otel_stop_event_v4,          // stopEvent
        profiler_otel_record_event_state_v4,  // recordEventState
        profiler_otel_finalize_v4,            // finalize
    };

#if OTEL_HAS_PROFILER_V5
    // NCCL Profiler Plugin v5 structure (NCCL 2.28+)
    volatile ncclProfiler_v5_t ncclProfiler_v5 = {
        "otel-profiler",                      // name
        profiler_otel_init_v5,                // init
        profiler_otel_start_event_v5,         // startEvent
        profiler_otel_stop_event_v4,          // stopEvent (unchanged since v4)
        profiler_otel_record_event_state_v5,  // recordEventState
        profiler_otel_finalize_v4,            // finalize (unchanged since v4)
    };
#endif

#if OTEL_HAS_PROFILER_V6
    // NCCL Profiler Plugin v6 structure (adds copy-engine collective events)
    volatile ncclProfiler_v6_t ncclProfiler_v6 = {
        "otel-profiler",                      // name
        profiler_otel_init_v6,                // init
        profiler_otel_start_event_v6,         // startEvent
        profiler_otel_stop_event_v4,          // stopEvent
        profiler_otel_record_event_state_v6,  // recordEventState
        profiler_otel_finalize_v4,            // finalize
    };
#endif

#if OTEL_HAS_PROFILER_V7
    // NCCL Profiler Plugin v7 structure (NCCL 2.31+)
    volatile ncclProfiler_v7_t ncclProfiler_v7 = {
        "otel-profiler",                      // name
        profiler_otel_init_v6,                // init (same signature and behavior as v6)
        profiler_otel_start_event_v7,         // startEvent
        profiler_otel_stop_event_v4,          // stopEvent
        profiler_otel_record_event_state_v7,  // recordEventState
        profiler_otel_finalize_v4,            // finalize
    };
#endif
}  // extern "C"
