#ifndef PROFILER_COMPAT_H_
#define PROFILER_COMPAT_H_

// Translation helpers for the NCCL profiler v5/v6/v7 interfaces.
//
// The plugin's event handling is written against the v4 descriptor layout. NCCL 2.28 introduced profiler v5
// (64-bit event types, API-level events, commId-first init), NCCL 2.29/2.30 added v6 (copy-engine collective
// events) and NCCL 2.31 added v7 (per-call user tags, kernel phase events). NCCL keeps loading v4 plugins through a
// compatibility shim, but that shim drops every event type v4 does not know about, including copy-engine (CE)
// collectives. Implementing the newer interfaces natively lets the plugin see those events.
//
// The descriptor structs of v5/v6/v7 share field names for the events the plugin already understands, so a single
// template translates any of them into the v4 layout. The rules mirror NCCL's own v4 shim
// (src/plugin/profiler/profiler_v4.cc): Coll/P2p parentObj comes from the parentGroup field.

#include <cstring>

#include "profiler_otel.h"

namespace otel_compat
{

/**
 * @brief Translate a v5/v6/v7 event descriptor into the v4 layout.
 *
 * @return true if the event type exists in v4 and was translated, false if the caller must handle it (CE events,
 *         API events, kernel launch/phase events).
 */
template <typename DescrT>
inline bool translateToV4(const DescrT* in, ncclProfilerEventDescr_v4_t* out)
{
    memset(out, 0, sizeof(*out));
    out->type      = (uint8_t)in->type;
    out->parentObj = in->parentObj;
    out->rank      = in->rank;

    switch (in->type)
    {
        case ncclProfileGroup:
        case ncclProfileProxyCtrl:
            return true;
        case ncclProfileColl:
            out->coll.seqNumber = in->coll.seqNumber;
            out->coll.func      = in->coll.func;
            out->coll.sendBuff  = in->coll.sendBuff;
            out->coll.recvBuff  = in->coll.recvBuff;
            out->coll.count     = in->coll.count;
            out->coll.root      = in->coll.root;
            out->coll.datatype  = in->coll.datatype;
            out->coll.nChannels = in->coll.nChannels;
            out->coll.nWarps    = in->coll.nWarps;
            out->coll.algo      = in->coll.algo;
            out->coll.proto     = in->coll.proto;
            out->parentObj      = in->coll.parentGroup;
            return true;
        case ncclProfileP2p:
            out->p2p.func      = in->p2p.func;
            out->p2p.buff      = in->p2p.buff;
            out->p2p.count     = in->p2p.count;
            out->p2p.datatype  = in->p2p.datatype;
            out->p2p.peer      = in->p2p.peer;
            out->p2p.nChannels = in->p2p.nChannels;
            out->parentObj     = in->p2p.parentGroup;
            return true;
        case ncclProfileProxyOp:
            out->proxyOp.pid       = in->proxyOp.pid;
            out->proxyOp.channelId = in->proxyOp.channelId;
            out->proxyOp.peer      = in->proxyOp.peer;
            out->proxyOp.nSteps    = in->proxyOp.nSteps;
            out->proxyOp.chunkSize = in->proxyOp.chunkSize;
            out->proxyOp.isSend    = in->proxyOp.isSend;
            return true;
        case ncclProfileProxyStep:
            out->proxyStep.step = in->proxyStep.step;
            return true;
        case ncclProfileKernelCh:
            out->kernelCh.channelId = in->kernelCh.channelId;
            out->kernelCh.pTimer    = in->kernelCh.pTimer;
            return true;
        case ncclProfileNetPlugin:
            out->netPlugin.id   = in->netPlugin.id;
            out->netPlugin.data = in->netPlugin.data;
            return true;
        default:
            return false;
    }
}

#if OTEL_HAS_PROFILER_V6
/**
 * @brief Express a copy-engine collective (v6+) as a v4 Coll descriptor.
 *
 * CE collectives have no channels, protocol or kernel algorithm; they are reported with algo "CE" and the
 * synchronization strategy NCCL used ("MC" multicast or "UC" unicast) in the proto slot, so they get their own
 * aggregation key (Comm<hash>_<func>_CE_<sync>_0Chnl) next to kernel-based collectives.
 */
template <typename DescrT>
inline void ceCollToV4(const DescrT* in, ncclProfilerEventDescr_v4_t* out)
{
    memset(out, 0, sizeof(*out));
    out->type           = ncclProfileColl;
    out->parentObj      = in->parentObj;
    out->rank           = in->rank;
    out->coll.seqNumber = in->ceColl.seqNumber;
    out->coll.func      = in->ceColl.func;
    out->coll.sendBuff  = in->ceColl.sendBuff;
    out->coll.recvBuff  = in->ceColl.recvBuff;
    out->coll.count     = in->ceColl.count;
    out->coll.root      = in->ceColl.root;
    out->coll.datatype  = in->ceColl.datatype;
    out->coll.nChannels = 0;
    out->coll.nWarps    = 0;
    out->coll.algo      = "CE";
    out->coll.proto     = in->ceColl.syncStrategy ? in->ceColl.syncStrategy : "CE";
}
#endif  // OTEL_HAS_PROFILER_V6

/**
 * @brief Translate v5/v6/v7 state arguments into the v4 layout (all versions share the same members).
 */
template <typename ArgsT>
inline void translateStateArgsToV4(ncclProfilerEventState_t eState, const ArgsT* in,
                                   ncclProfilerEventStateArgs_v4_t* out)
{
    memset(out, 0, sizeof(*out));
    if (in == nullptr) return;
    switch (eState)
    {
        case ncclProfilerProxyStepSendGPUWait:
        case ncclProfilerProxyStepSendPeerWait_v4:
        case ncclProfilerProxyStepSendWait:
        case ncclProfilerProxyStepRecvWait:
        case ncclProfilerProxyStepRecvFlushWait:
        case ncclProfilerProxyStepRecvGPUWait:
            out->proxyStep.transSize = in->proxyStep.transSize;
            break;
        case ncclProfilerNetPluginUpdate:
            out->netPlugin.data = in->netPlugin.data;
            break;
        case ncclProfilerKernelChStop:
            out->kernelCh.pTimer = in->kernelCh.pTimer;
            break;
        case ncclProfilerProxyCtrlIdle:
        case ncclProfilerProxyCtrlActive:
        case ncclProfilerProxyCtrlSleep:
        case ncclProfilerProxyCtrlWakeup:
        case ncclProfilerProxyCtrlAppend:
        case ncclProfilerProxyCtrlAppendEnd:
            out->proxyCtrl.appendedProxyOps = in->proxyCtrl.appendedProxyOps;
            break;
        default:
            break;
    }
}

}  // namespace otel_compat

#endif  // PROFILER_COMPAT_H_
