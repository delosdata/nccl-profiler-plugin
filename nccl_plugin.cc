// SPDX-FileCopyrightText: 2025 Delos Data Inc
// SPDX-License-Identifier: LicenseRef-FSL-1.1-ALv2

#include <sys/types.h>
#include <unistd.h>

#include "profiler_otel.h"

// NCCL Profiler Plugin v4 structure
extern "C"
{
    volatile ncclProfiler_v4_t ncclProfiler_v4 = {
        "otel-profiler",                      // name
        profiler_otel_init_v4,                // init
        profiler_otel_start_event_v4,         // startEvent
        profiler_otel_stop_event_v4,          // stopEvent
        profiler_otel_record_event_state_v4,  // recordEventState
        profiler_otel_finalize_v4,            // finalize
    };
}  // extern "C"
