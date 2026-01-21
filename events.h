// SPDX-FileCopyrightText: 2025 Delos Data Inc
// SPDX-License-Identifier: LicenseRef-FSL-1.1-ALv2

#ifndef EVENTS_H_
#define EVENTS_H_

#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include "profiler_otel.h"

// Forward declaration
struct CommunicatorState;

/**
 * Context of the profiler plugin instance.
 */
struct eventContext
{
    const char* commName;
    uint64_t commHash;
    int nNodes;
    int nranks;
    int rank;
    struct CommunicatorState* commState;  // Pointer to circular buffer state
};

/**
 * Lightweight event handle for circular buffer storage.
 * This structure is optimized for lock-free operations and minimal memory footprint.
 * It stores the essential information needed for telemetry export.
 */
typedef struct
{
    uint8_t type;        // ncclProfileGroup, ncclProfileColl, ncclProfileP2p, ncclProfileProxyOp, ncclProfileProxyStep,
                         // ncclProfileProxyCtrl
    uint8_t buffer_idx;  // Window/buffer index this event belongs to (for mark_operation_complete)
    void* parentObj;     // pointer to parent profiler object
    struct CommunicatorState* commState;  // Back-pointer to commState (for mark_operation_complete)
    int rank;                             // originating rank
    double startTs;
    double endTs;
    union
    {
        struct
        {
            uint64_t seqNumber;
            const char* func;
            size_t bytes;
            uint8_t nChannels;
            const char* algo;
            const char* proto;
            bool firstProxyOpCompleted;  // Flag to track if first ProxyOp has completed (for in_progress_count)
        } coll;
        struct
        {
            const char* func;
            size_t bytes;
            int peer;
            uint8_t nChannels;
            bool firstProxyOpCompleted;  // Flag to track if first ProxyOp has completed (for in_progress_count)
        } p2p;
        struct
        {
            uint8_t channelId;
            int peer;
            int chunkSize;
        } proxyOp;
        struct
        {
            int step;
            size_t transSize;   // Real transfer size from recordEventState with ProxyStepSendWait
            double sendWaitTs;  // Timestamp when ProxyStepSendWait state is recorded (start of actual transfer)
            bool hasSendWait;   // Whether SendWait state was recorded
        } proxyStep;
    };
} otelEventHandle_t;

#endif  // EVENTS_H_
