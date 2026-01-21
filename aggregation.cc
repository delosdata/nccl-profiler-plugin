// SPDX-FileCopyrightText: 2025 Delos Data Inc
// SPDX-License-Identifier: LicenseRef-FSL-1.1-ALv2

#include "aggregation.h"

#include <sstream>

#include "profiler_otel.h"  // For OTEL_TRACE

/**
 * @brief Construct a WindowAggregator for a specific rank.
 *
 * @param[in] rank Global rank of the process (used for key generation).
 */
WindowAggregator::WindowAggregator(int rank) : rank(rank) {}

/**
 * @brief Generate aggregation key for a collective event.
 *
 * @param[in] event Collective event handle.
 *
 * @return Key string in format: Comm<hash>_Func_Algo_Proto_nChannels
 */
std::string WindowAggregator::getCollectiveKey(const otelEventHandle_t& event) const
{
    std::stringstream ss;
    uint64_t commHash = event.commState ? event.commState->comm_hash : 0;
    ss << "Comm" << commHash << "_" << (event.coll.func ? event.coll.func : "NULL") << "_"
       << (event.coll.algo ? event.coll.algo : "NULL") << "_" << (event.coll.proto ? event.coll.proto : "NULL") << "_"
       << (int)event.coll.nChannels << "Chnl";
    return ss.str();
}

/**
 * @brief Generate aggregation key for a P2P event.
 *
 * For P2P communicators (nranks=2), this generates a key that identifies:
 * - The communicator hash (for correlation with the peer side)
 * - The source (hostname, global_rank, local_rank)
 * - The function (Send/Recv)
 * - The peer rank within the communicator (0 or 1)
 * - Number of channels
 *
 * Note: We only know our own identity (hostname, ranks). The peer's identity
 * can be correlated in Grafana using the shared comm_hash (both sides have the same hash).
 *
 * @param[in] event P2P event handle.
 *
 * @return Key string in format: Comm<hash>_<hostname>_<global>_<local>_<func>_ToPeer<peer>_<nChannels>Chnl
 */
std::string WindowAggregator::getP2PKey(const otelEventHandle_t& event) const
{
    std::stringstream ss;

    uint64_t commHash    = event.commState ? event.commState->comm_hash : 0;
    std::string hostname = event.commState ? event.commState->hostname : "unknown";
    // For P2P: rank within the P2P comm (0 or 1) represents the pipeline number
    int src_pipeline = event.commState ? event.commState->rank : rank;
    const char* func = event.p2p.func ? event.p2p.func : "NULL";

    // Key format: Comm<hash>_(<hostname>)_<func>_Pipeline<src>ToPipeline<dst>_<nChannels>Chnl
    // For P2P comms (nranks=2), both src and peer (0 or 1) represent pipeline numbers
    // Example: Comm123456_(romeo)_Send_Pipeline0ToPipeline1_2Chnl
    ss << "Comm" << commHash << "_(" << hostname << ")_" << func << "_Pipeline" << src_pipeline << "ToPipeline"
       << event.p2p.peer << "_" << (int)event.p2p.nChannels << "Chnl";

    return ss.str();
}

/**
 * @brief Generate aggregation key for rank-to-rank transfers.
 *
 * For COLLECTIVE comms: Uses rank within communicator (no hostname needed)
 * For P2P comms: Uses pipeline numbers (rank within P2P comm represents pipeline)
 *
 * @param[in] commHash Communicator hash.
 * @param[in] peer Destination peer rank within the communicator.
 * @param[in] commState Communicator state for hostname, rank, and comm_type.
 *
 * @return Key string:
 *   - COLLECTIVE: Comm<hash>_Rank<X>_ToPeer<peer>
 *   - P2P: Comm<hash>_<hostname>_Pipeline<src>_ToPipeline<peer>
 */
std::string WindowAggregator::getRankTransferKey(uint64_t commHash, int peer, const CommunicatorState* commState) const
{
    std::stringstream ss;

    bool isP2P = commState && commState->comm_type == CommunicatorState::CommType::P2P;

    if (isP2P)
    {
        // P2P: Show hostname and pipeline numbers
        // rank within P2P comm (0 or 1) represents the pipeline number
        std::string hostname = commState ? commState->hostname : "unknown";
        int src_pipeline     = commState ? commState->rank : rank;
        ss << "Comm" << commHash << "_" << hostname << "_Pipeline" << src_pipeline << "_ToPipeline" << peer;
    }
    else
    {
        // COLLECTIVE: Show rank within communicator (no hostname)
        int comm_rank = commState ? commState->rank : rank;
        ss << "Comm" << commHash << "_Rank" << comm_rank << "_ToPeer" << peer;
    }
    return ss.str();
}

/**
 * @brief Generate aggregation key for per-channel transfers.
 *
 * For COLLECTIVE comms: Uses rank within communicator
 * For P2P comms: Uses pipeline numbers
 *
 * @param[in] event ProxyOp event handle containing channel and peer info.
 *
 * @return Key string:
 *   - COLLECTIVE: Comm<hash>_Rank<X>_ToPeer<peer>_Chnl<id>
 *   - P2P: Comm<hash>_<hostname>_Pipeline<src>_ToPipeline<peer>_Chnl<id>
 */
std::string WindowAggregator::getChannelTransferKey(const otelEventHandle_t& event) const
{
    std::stringstream ss;
    uint64_t commHash = event.commState ? event.commState->comm_hash : 0;

    bool isP2P = event.commState && event.commState->comm_type == CommunicatorState::CommType::P2P;

    if (isP2P)
    {
        std::string hostname = event.commState ? event.commState->hostname : "unknown";
        int src_pipeline     = event.commState ? event.commState->rank : event.rank;
        ss << "Comm" << commHash << "_" << hostname << "_Pipeline" << src_pipeline << "_ToPipeline"
           << event.proxyOp.peer << "_Chnl" << (int)event.proxyOp.channelId;
    }
    else
    {
        int comm_rank = event.commState ? event.commState->rank : event.rank;
        ss << "Comm" << commHash << "_Rank" << comm_rank << "_ToPeer" << event.proxyOp.peer << "_Chnl"
           << (int)event.proxyOp.channelId;
    }
    return ss.str();
}

/**
 * @brief Generate key for transfer channel grouping.
 *
 * @param[in] channelId Channel ID.
 *
 * @return Key string in format: Chnl<id>
 */
std::string WindowAggregator::getTransferChannelKey(uint8_t channelId) const
{
    std::stringstream ss;
    ss << "Chnl" << (int)channelId;
    return ss.str();
}

/**
 * @brief Add an event to the aggregator.
 *
 * Processes events based on type:
 * - Coll/P2P: Tracked for later linking with ProxyOps
 * - ProxyStep: Aggregated to parent ProxyOp
 * - ProxyOp: Stored for linking in finalize()
 *
 * @param[in] event Event handle to process.
 */
void WindowAggregator::addEvent(const otelEventHandle_t& event)
{
    // Phase 1: Track Coll/P2P - store by eHandle for later ProxyOp correlation
    if (event.type == ncclProfileColl)
    {
        InProgressOperation op;
        op.key              = getCollectiveKey(event);
        op.startTs          = event.startTs;
        op.endTs            = event.endTs;  // Coll stop time (fallback for internal links)
        op.bytes            = event.coll.bytes;
        op.expectedProxyOps = event.coll.nChannels;  // We only track send ProxyOps
        op.seenProxyOps     = 0;
        op.lastProxyOpEnd   = event.startTs;  // Initialize to start time

        // Store by eHandle pointer - ProxyOp's parentObj will point here
        collHandleToOp[&event] = op;

        OTEL_TRACE(NCCL_INIT, "Tracked Coll: %s, eHandle=%p, expectedProxyOps=%d, endTs=%.2f", op.key.c_str(), &event,
                   op.expectedProxyOps, op.endTs);
    }
    else if (event.type == ncclProfileP2p)
    {
        InProgressOperation op;
        op.key              = getP2PKey(event);
        op.startTs          = event.startTs;
        op.endTs            = event.endTs;  // P2P stop time (fallback for internal links)
        op.bytes            = event.p2p.bytes;
        op.expectedProxyOps = event.p2p.nChannels;  // We only track send ProxyOps
        op.seenProxyOps     = 0;
        op.lastProxyOpEnd   = event.startTs;

        p2pHandleToOp[&event] = op;

        OTEL_TRACE(NCCL_INIT, "Tracked P2P: %s, eHandle=%p, expectedProxyOps=%d, endTs=%.2f", op.key.c_str(), &event,
                   op.expectedProxyOps, op.endTs);
    }
    // Phase 2: Aggregate ProxyStep transfers to their parent ProxyOp
    else if (event.type == ncclProfileProxyStep)
    {
        // Only process ProxySteps with SendWait (actual transfers)
        if (event.proxyStep.hasSendWait)
        {
            double transferTime = event.endTs - event.proxyStep.sendWaitTs;

            // Skip transfers with transferTime <= 0 (0 means infinite bandwidth, negative is invalid)
            // Do not clamp to 0 as that would incorrectly represent infinite bandwidth
            if (transferTime <= 0)
            {
                OTEL_WARN(NCCL_INIT, "Skipping ProxyStep with invalid transferTime=%.2f us (size=%zu)", transferTime,
                          event.proxyStep.transSize);
                return;  // Skip this transfer
            }

            // Find parent ProxyOp via parentObj
            const void* proxyOpHandle = event.parentObj;
            if (proxyOpHandle)
            {
                // Aggregate to ProxyOp
                proxyOpTransfers[proxyOpHandle].addTransfer(event.proxyStep.transSize, transferTime);

                OTEL_TRACE(NCCL_INIT, "Aggregated ProxyStep to ProxyOp %p: size=%zu, time=%.2f us", proxyOpHandle,
                           event.proxyStep.transSize, transferTime);
            }
            else
            {
                // ProxyStep with SendWait should have a parent ProxyOp
                // This is unexpected and indicates a potential data consistency issue
                OTEL_WARN(NCCL_INIT, "ProxyStep with SendWait has NULL parentObj (size=%zu, transferTime=%.2f us)",
                          event.proxyStep.transSize, transferTime);
            }
        }
    }
    // Phase 3: Just store ProxyOp for later linking in finalize() (after all ProxySteps are aggregated)
    else if (event.type == ncclProfileProxyOp)
    {
        // Store a copy of the ProxyOp event for processing in finalize()
        proxyOps[&event] = event;

        // Update timing in parent Coll/P2P (needed for duration calculation)
        const void* parentHandle = getRootCollectiveHandle(event.parentObj);
        if (parentHandle)
        {
            auto collIt = collHandleToOp.find(parentHandle);
            if (collIt != collHandleToOp.end())
            {
                collIt->second.lastProxyOpEnd = std::max(collIt->second.lastProxyOpEnd, event.endTs);
                collIt->second.seenProxyOps++;
            }

            auto p2pIt = p2pHandleToOp.find(parentHandle);
            if (p2pIt != p2pHandleToOp.end())
            {
                p2pIt->second.lastProxyOpEnd = std::max(p2pIt->second.lastProxyOpEnd, event.endTs);
                p2pIt->second.seenProxyOps++;
            }
        }

        OTEL_TRACE(NCCL_INIT, "Stored ProxyOp %p for finalization: parentObj=%p", &event, event.parentObj);
    }
}

/**
 * @brief Finalize aggregation and calculate metrics.
 *
 * Links ProxyOps to their parent Collectives/P2Ps, calculates correct durations
 * (Coll/P2P START → Last ProxyOp STOP), and prepares aggregated data for export.
 * Must be called after all events are added.
 */
void WindowAggregator::finalize()
{
    OTEL_TRACE(NCCL_INIT, "Finalizing: %zu ProxyOps, %zu ProxyOp transfers", proxyOps.size(), proxyOpTransfers.size());

    int proxyOpsWithTransfers    = 0;
    int proxyOpsWithoutTransfers = 0;

    // Phase 1: Link ProxyOps to their parent Coll/P2P and aggregate transfers
    for (const auto& proxyPair : proxyOps)
    {
        const otelEventHandle_t& proxyOp = proxyPair.second;
        const void* proxyOpHandle        = proxyPair.first;

        // Get aggregated transfers for this ProxyOp
        auto proxyIt = proxyOpTransfers.find(proxyOpHandle);

        if (proxyIt != proxyOpTransfers.end())
        {
            proxyOpsWithTransfers++;
            const AggregatedTransfer& transfers = proxyIt->second;

            // Find parent Coll or P2P (if any)
            const void* parentHandle = getRootCollectiveHandle(proxyOp.parentObj);

            if (parentHandle)
            {
                // Link to Coll
                auto collIt = collHandleToOp.find(parentHandle);
                if (collIt != collHandleToOp.end())
                {
                    std::string transferKey = getTransferChannelKey(proxyOp.proxyOp.channelId);
                    collectives[collIt->second.key].transfers[transferKey].totalBytes += transfers.totalBytes;
                    collectives[collIt->second.key].transfers[transferKey].totalTimeUs += transfers.totalTimeUs;
                    collectives[collIt->second.key].transfers[transferKey].count += transfers.count;

                    // Add each transfer individually to the cache (once per ProxyStep)
                    for (int i = 0; i < transfers.count; i++)
                    {
                        double avgBytes = (double)transfers.totalBytes / transfers.count;
                        double avgTime  = transfers.totalTimeUs / transfers.count;
                        collectives[collIt->second.key].addTransferToCache(avgBytes, avgTime);
                    }

                    OTEL_TRACE(NCCL_INIT, "Linked ProxyOp %p to Coll %s: bytes=%zu, time=%.2f us, count=%d",
                               proxyOpHandle, collIt->second.key.c_str(), transfers.totalBytes, transfers.totalTimeUs,
                               transfers.count);
                }

                // Link to P2P
                auto p2pIt = p2pHandleToOp.find(parentHandle);
                if (p2pIt != p2pHandleToOp.end())
                {
                    std::string transferKey = getTransferChannelKey(proxyOp.proxyOp.channelId);
                    p2ps[p2pIt->second.key].transfers[transferKey].totalBytes += transfers.totalBytes;
                    p2ps[p2pIt->second.key].transfers[transferKey].totalTimeUs += transfers.totalTimeUs;
                    p2ps[p2pIt->second.key].transfers[transferKey].count += transfers.count;

                    // Add each transfer individually to the cache (once per ProxyStep)
                    for (int i = 0; i < transfers.count; i++)
                    {
                        double avgBytes = (double)transfers.totalBytes / transfers.count;
                        double avgTime  = transfers.totalTimeUs / transfers.count;
                        p2ps[p2pIt->second.key].addTransferToCache(avgBytes, avgTime);
                    }

                    OTEL_TRACE(NCCL_INIT, "Linked ProxyOp %p to P2P %s: bytes=%zu, time=%.2f us", proxyOpHandle,
                               p2pIt->second.key.c_str(), transfers.totalBytes, transfers.totalTimeUs);
                }
            }

            // Aggregate for rank/channel metrics (for ALL ProxyOps, with or without parent)
            uint64_t commHash           = proxyOp.commState ? proxyOp.commState->comm_hash : 0;
            std::string rankTransferKey = getRankTransferKey(commHash, proxyOp.proxyOp.peer, proxyOp.commState);
            rankTransfers[rankTransferKey].totalBytes += transfers.totalBytes;
            rankTransfers[rankTransferKey].totalTimeUs += transfers.totalTimeUs;
            rankTransfers[rankTransferKey].count += transfers.count;
            // Merge the individual ProxyStep data points from this ProxyOp
            rankTransfers[rankTransferKey].lr.merge(transfers.lr);

            std::string channelTransferKey = getChannelTransferKey(proxyOp);
            channelTransfers[channelTransferKey].totalBytes += transfers.totalBytes;
            channelTransfers[channelTransferKey].totalTimeUs += transfers.totalTimeUs;
            channelTransfers[channelTransferKey].count += transfers.count;
            // Merge the individual ProxyStep data points from this ProxyOp
            channelTransfers[channelTransferKey].lr.merge(transfers.lr);
        }
        else
        {
            proxyOpsWithoutTransfers++;
            // This is expected for ProxyOps that span window boundaries
            // (ProxyOp in one window, ProxySteps in another)
        }
    }

    // Log summary of ProxyOp linking (only if there are issues)
    if (proxyOpsWithoutTransfers > 0)
    {
        OTEL_TRACE(NCCL_INIT,
                   "Finalized ProxyOps: %d with transfers, %d without transfers (likely window boundary issue)",
                   proxyOpsWithTransfers, proxyOpsWithoutTransfers);
    }
    else
    {
        OTEL_TRACE(NCCL_INIT, "Finalized ProxyOps: %d with transfers", proxyOpsWithTransfers);
    }

    // Phase 2: Calculate correct Coll durations
    // Normal case: startTs -> lastProxyOpEnd (when ProxyOps exist)
    // Internal links: startTs -> endTs (when no ProxyOps, use Coll stop time)
    for (auto& pair : collHandleToOp)
    {
        InProgressOperation& op = pair.second;
        double realDuration;

        if (op.seenProxyOps > 0)
        {
            // Normal case: use lastProxyOpEnd (actual transfer completion)
            realDuration = op.lastProxyOpEnd - op.startTs;
            OTEL_TRACE(NCCL_INIT,
                       "Finalized Coll: %s, bytes=%zu, duration=%.2f us (start=%.2f, lastProxyOpEnd=%.2f, proxyOps=%d)",
                       op.key.c_str(), op.bytes, realDuration, op.startTs, op.lastProxyOpEnd, op.seenProxyOps);
        }
        else
        {
            // Internal links only: no ProxyOps, use Coll stop time
            // Note: This timing is less accurate but still gives valid byte counts
            realDuration = op.endTs - op.startTs;
            OTEL_INFO(NCCL_INIT,
                      "Finalized Coll (internal links): %s, bytes=%zu, duration=%.2f us (start=%.2f, endTs=%.2f, no "
                      "ProxyOps)",
                      op.key.c_str(), op.bytes, realDuration, op.startTs, op.endTs);
        }

        // Skip collectives with realDuration <= 0
        // Zero duration causes infinite bandwidth in regression calculations
        // Negative duration indicates invalid timing data
        if (realDuration <= 0)
        {
            OTEL_WARN(NCCL_INIT, "Skipping Coll with invalid duration=%.2f us: %s, bytes=%zu", realDuration,
                      op.key.c_str(), op.bytes);
            continue;
        }

        collectives[op.key].addCollective(op.bytes, realDuration);
    }

    // Same for P2P
    for (auto& pair : p2pHandleToOp)
    {
        InProgressOperation& op = pair.second;
        double realDuration;

        if (op.seenProxyOps > 0)
        {
            // Normal case: use lastProxyOpEnd (actual transfer completion)
            realDuration = op.lastProxyOpEnd - op.startTs;
            OTEL_TRACE(NCCL_INIT, "Finalized P2P: %s, bytes=%zu, duration=%.2f us (proxyOps=%d)", op.key.c_str(),
                       op.bytes, realDuration, op.seenProxyOps);
        }
        else
        {
            // Internal links only: no ProxyOps, use P2P stop time
            realDuration = op.endTs - op.startTs;
            OTEL_INFO(NCCL_INIT, "Finalized P2P (internal links): %s, bytes=%zu, duration=%.2f us (no ProxyOps)",
                      op.key.c_str(), op.bytes, realDuration);
        }

        // Skip P2P operations with realDuration <= 0
        // Zero duration causes infinite bandwidth in regression calculations
        // Negative duration indicates invalid timing data
        if (realDuration <= 0)
        {
            OTEL_WARN(NCCL_INIT, "Skipping P2P with invalid duration=%.2f us: %s, bytes=%zu", realDuration,
                      op.key.c_str(), op.bytes);
            continue;
        }

        p2ps[op.key].addP2P(op.bytes, realDuration);
    }
}

/**
 * @brief Get the root Coll/P2P handle from parentObj chain.
 *
 * Traverses the parentObj chain to find the root Collective or P2P operation.
 * Used to link ProxyOps to their parent operations.
 *
 * @param[in] parentObj Parent object pointer (may be nullptr).
 *
 * @return Pointer to root Coll/P2P handle, or nullptr if not found.
 */
const void* WindowAggregator::getRootCollectiveHandle(const void* parentObj) const
{
    if (!parentObj) return nullptr;

    // ProxyOp's parentObj should point directly to Coll or P2P eHandle
    if (collHandleToOp.count(parentObj))
    {
        return parentObj;
    }

    if (p2pHandleToOp.count(parentObj))
    {
        return parentObj;
    }

    return nullptr;  // Not found or not a tracked operation
}
