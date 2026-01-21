// SPDX-FileCopyrightText: 2025 Delos Data Inc
// SPDX-License-Identifier: LicenseRef-FSL-1.1-ALv2

#ifndef AGGREGATION_H_
#define AGGREGATION_H_

#include <map>
#include <string>
#include <vector>

#include "communicator_state.h"
#include "events.h"
#include "linear_regression.h"
#include "param.h"

// Helper function to get linear regression mode from environment
static inline LinearRegression::Mode getLinearRegressionMode()
{
    const char* modeStr = ncclParamLinearRegressionMode();
    if (strcmp(modeStr, "AVG") == 0)
    {
        return LinearRegression::Mode::AVG;
    }
    else
    {
        // Default to MIN mode
        return LinearRegression::Mode::MIN;
    }
}

// Structure for aggregated transfer information
struct AggregatedTransfer
{
    size_t totalBytes;
    double totalTimeUs;
    int count;
    LinearRegression lr;  // For latency and rate calculation

    AggregatedTransfer() : totalBytes(0), totalTimeUs(0.0), count(0), lr(getLinearRegressionMode()) {}

    void addTransfer(size_t bytes, double timeUs)
    {
        totalBytes += bytes;
        totalTimeUs += timeUs;
        count++;
        lr.addPoint(bytes, timeUs);
    }

    double getAverageSize() const
    {
        return count > 0 ? (double)totalBytes / count : 0.0;
    }
    double getAverageTime() const
    {
        return count > 0 ? totalTimeUs / count : 0.0;
    }
    double getAverageRateMBps() const
    {
        return totalTimeUs > 0 ? (double)totalBytes / totalTimeUs : 0.0;
    }

    // Linear regression based latency (intercept) and rate (1/slope)
    // Linear regression fits: time = intercept + slope * size
    // Where: slope is us/byte, intercept is latency at size=0
    // Rate calculation: 1/slope gives bytes/us
    // Conversion to MB/s: bytes/us * (1e6 us/s) / (1e6 bytes/MB) = MB/s
    // Using decimal MB convention (1 MB = 1,000,000 bytes) to match getAverageRateMBps()
    // Therefore: bytes/us = MB/s (since 1 byte/us = 1,000,000 bytes/s = 1 MB/s)
    //
    // Requirements:
    // - At least 3 different transfer sizes
    // - Acceptable variance (R-squared >= 0.8)
    bool getLinearRegressionMetrics(double& latencyUs, double& rateMBps) const
    {
        // Check if we have at least 3 different transfer sizes
        if (!lr.hasAtLeastThreeDifferentSizes())
        {
            latencyUs = 0.0;
            rateMBps  = 0.0;
            return false;
        }

        double slope, intercept;
        if (lr.calculate(slope, intercept))
        {
            // Check R-squared for acceptable variance (goodness of fit)
            double rSquared;
            if (lr.calculateRSquared(rSquared) && rSquared >= 0.8)
            {
                latencyUs = (intercept >= 0) ? intercept : 0.0;  // Clamp negative latency to 0

                // Only compute rate if slope is positive and not too close to zero
                // Slope close to zero indicates near-infinite bandwidth, which is not physically meaningful
                if (slope > 1e-6)  // Require slope > 1e-6 (equivalent to rate < 1e6 MB/s)
                {
                    // 1/slope gives bytes/us, which equals MB/s with decimal MB convention
                    // This matches getAverageRateMBps() which returns bytes/us directly
                    rateMBps = 1.0 / slope;
                    return true;
                }
                else
                {
                    // Slope too small (infinite bandwidth) - reject as not physically meaningful
                    latencyUs = 0.0;
                    rateMBps  = 0.0;
                    return false;
                }
            }
        }
        latencyUs = 0.0;
        rateMBps  = 0.0;
        return false;
    }
};

// Base structure for aggregated operations (Collective or P2P)
// Contains common fields and methods shared by both operation types
struct AggregatedOperationBase
{
    size_t totalBytes;
    double totalTimeUs;
    int count;
    std::map<std::string, AggregatedTransfer> transfers;  // Key: ChannelId (underlying proxy ops)

    // Cached aggregates for underlying transfers (updated when transfers are added)
    int cachedTotalTransferCount;
    size_t cachedTotalTransferBytes;
    double cachedTotalTransferTimeUs;

    AggregatedOperationBase()
        : totalBytes(0),
          totalTimeUs(0.0),
          count(0),
          cachedTotalTransferCount(0),
          cachedTotalTransferBytes(0),
          cachedTotalTransferTimeUs(0.0)
    {
    }

    // Add operation data (bytes and time)
    void addOperation(size_t bytes, double timeUs)
    {
        totalBytes += bytes;
        totalTimeUs += timeUs;
        count++;
    }

    // Called when a transfer is added to this operation's transfers map - O(1)
    void addTransferToCache(size_t bytes, double timeUs)
    {
        cachedTotalTransferCount++;
        cachedTotalTransferBytes += bytes;
        cachedTotalTransferTimeUs += timeUs;
    }

    // Get average bytes per operation
    double getAverageSize() const
    {
        return count > 0 ? (double)totalBytes / count : 0.0;
    }

    // Get average time per operation
    double getAverageTime() const
    {
        return count > 0 ? totalTimeUs / count : 0.0;
    }

    // Get aggregate transfer statistics from underlying proxy operations - O(1)
    int getTotalTransferCount() const
    {
        return cachedTotalTransferCount;
    }

    double getAverageTransferCount() const
    {
        return count > 0 ? (double)cachedTotalTransferCount / count : 0.0;
    }

    double getAverageTransferSize() const
    {
        return cachedTotalTransferCount > 0 ? (double)cachedTotalTransferBytes / cachedTotalTransferCount : 0.0;
    }

    double getAverageTransferTime() const
    {
        return cachedTotalTransferCount > 0 ? cachedTotalTransferTimeUs / cachedTotalTransferCount : 0.0;
    }
};

// Structure for aggregated P2P information
struct AggregatedP2P : public AggregatedOperationBase
{
    // Inherits all fields and methods from AggregatedOperationBase

    // Convenience method for P2P-specific naming (delegates to base class)
    void addP2P(size_t bytes, double timeUs)
    {
        addOperation(bytes, timeUs);
    }
};

// Structure for aggregated Collective information
struct AggregatedCollective : public AggregatedOperationBase
{
    // Inherits all fields and methods from AggregatedOperationBase

    // Convenience method for collective-specific naming (delegates to base class)
    void addCollective(size_t bytes, double timeUs)
    {
        addOperation(bytes, timeUs);
    }
};

// Structure to track in-progress Collective/P2P for correct timing
struct InProgressOperation
{
    std::string key;        // Aggregate key (Func_Algo_Proto_nChannels)
    double startTs;         // When Coll/P2P started
    double endTs;           // When Coll/P2P stopped (fallback for internal links scenario)
    double lastProxyOpEnd;  // Latest ProxyOp end time (for duration calculation)
    size_t bytes;           // Operation bytes
    int expectedProxyOps;   // Expected number of ProxyOps (nChannels * 2 typically)
    int seenProxyOps;       // Count of ProxyOps seen so far

    InProgressOperation() : startTs(0), endTs(0), lastProxyOpEnd(0), bytes(0), expectedProxyOps(0), seenProxyOps(0) {}
};

/**
 * @brief Main aggregator for processing a single window of events.
 *
 * Aggregates NCCL events from a window, links ProxyOps to their parent Collectives/P2Ps,
 * and calculates metrics. Processes events in phases:
 * 1. Track Coll/P2P operations
 * 2. Aggregate ProxyStep transfers
 * 3. Link ProxyOps to parents and calculate durations
 * 4. Export-ready aggregated data
 *
 * @note This class is NOT thread-safe and is designed for single-threaded use
 *       by the telemetry thread. Each window is processed by one thread at a time.
 */
class WindowAggregator
{
public:
    /**
     * @brief Construct a WindowAggregator for a specific rank.
     *
     * @param[in] rank Global rank of the process (used for key generation).
     */
    WindowAggregator(int rank);

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
    void addEvent(const otelEventHandle_t& event);

    /**
     * @brief Finalize aggregation and calculate metrics.
     *
     * Links ProxyOps to their parent Collectives/P2Ps, calculates correct durations
     * (Coll/P2P START → Last ProxyOp STOP), and prepares aggregated data for export.
     * Must be called after all events are added.
     */
    void finalize();

    /**
     * @brief Get aggregated collective operations.
     *
     * @return Map of collective keys to aggregated data.
     *         Key format: Comm<hash>_Func_Algo_Proto_nChannels
     */
    const std::map<std::string, AggregatedCollective>& getCollectives() const
    {
        return collectives;
    }

    /**
     * @brief Get aggregated P2P operations.
     *
     * @return Map of P2P keys to aggregated data.
     *         Key format: Comm<hash>_Func_RankXToRankY_nChannels
     */
    const std::map<std::string, AggregatedP2P>& getP2Ps() const
    {
        return p2ps;
    }

    /**
     * @brief Get aggregated rank-to-rank transfers.
     *
     * @return Map of rank transfer keys to aggregated data.
     *         Key format: Comm<hash>_RankXToRankY
     */
    const std::map<std::string, AggregatedTransfer>& getRankTransfers() const
    {
        return rankTransfers;
    }

    /**
     * @brief Get aggregated per-channel transfers.
     *
     * @return Map of channel transfer keys to aggregated data.
     *         Key format: Comm<hash>_RankXToRankY_Chnl<id>
     */
    const std::map<std::string, AggregatedTransfer>& getChannelTransfers() const
    {
        return channelTransfers;
    }

private:
    int rank;
    std::map<std::string, AggregatedCollective> collectives;     // Key: Comm<hash>_Func_Algo_Proto_nChannels
    std::map<std::string, AggregatedP2P> p2ps;                   // Key: Comm<hash>_Func_RankXToRankY_nChannels
    std::map<std::string, AggregatedTransfer> rankTransfers;     // Key: Comm<hash>_RankXToRankY
    std::map<std::string, AggregatedTransfer> channelTransfers;  // Key: Comm<hash>_RankXToRankY_Chnl<id>

    // NEW: Maps to track eHandle -> operation relationship
    std::map<const void*, InProgressOperation> collHandleToOp;  // Coll eHandle -> in-progress operation
    std::map<const void*, InProgressOperation> p2pHandleToOp;   // P2P eHandle -> in-progress operation

    // NEW: Map ProxyOp eHandle to its aggregated transfer data (from ProxySteps)
    std::map<const void*, AggregatedTransfer> proxyOpTransfers;  // ProxyOp eHandle -> aggregated ProxyStep transfers

    // NEW: Store ProxyOp events for linking in finalize() (after ProxySteps are aggregated)
    std::map<const void*, otelEventHandle_t> proxyOps;  // ProxyOp eHandle -> ProxyOp event

    /**
     * @brief Generate aggregation key for a collective event.
     *
     * @param[in] event Collective event handle.
     *
     * @return Key string in format: Comm<hash>_Func_Algo_Proto_nChannels
     */
    std::string getCollectiveKey(const otelEventHandle_t& event) const;

    /**
     * @brief Generate aggregation key for a P2P event.
     *
     * @param[in] event P2P event handle.
     *
     * @return Key string in format: Comm<hash>_Func_RankXToRankY_nChannels
     */
    std::string getP2PKey(const otelEventHandle_t& event) const;

    /**
     * @brief Generate aggregation key for rank-to-rank transfers.
     *
     * @param[in] commHash Communicator hash.
     * @param[in] peer Destination peer rank.
     *
     * @return Key string in format: Comm<hash>_<hostname>_GPU<local>_ToPeer<peer>
     */
    std::string getRankTransferKey(uint64_t commHash, int peer, const CommunicatorState* commState) const;

    /**
     * @brief Generate aggregation key for per-channel transfers.
     *
     * @param[in] event ProxyOp event handle containing channel and peer info.
     *
     * @return Key string in format: Comm<hash>_RankXToRankY_Chnl<id>
     */
    std::string getChannelTransferKey(const otelEventHandle_t& event) const;

    /**
     * @brief Generate key for transfer channel grouping.
     *
     * @param[in] channelId Channel ID.
     *
     * @return Key string in format: Chnl<id>
     */
    std::string getTransferChannelKey(uint8_t channelId) const;

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
    const void* getRootCollectiveHandle(const void* parentObj) const;
};

#endif  // AGGREGATION_H_
