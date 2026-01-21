// SPDX-FileCopyrightText: 2025 Delos Data Inc
// SPDX-License-Identifier: LicenseRef-FSL-1.1-ALv2

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../../aggregation.h"
#include "../../events.h"

// Test fixture for AggregatedTransfer
class AggregatedTransferTest : public ::testing::Test
{
protected:
    AggregatedTransfer transfer;
};

TEST_F(AggregatedTransferTest, InitialState)
{
    EXPECT_EQ(transfer.totalBytes, 0u);
    EXPECT_EQ(transfer.totalTimeUs, 0.0);
    EXPECT_EQ(transfer.count, 0);
}

TEST_F(AggregatedTransferTest, SingleTransfer)
{
    transfer.addTransfer(1024, 10.5);

    EXPECT_EQ(transfer.totalBytes, 1024u);
    EXPECT_DOUBLE_EQ(transfer.totalTimeUs, 10.5);
    EXPECT_EQ(transfer.count, 1);
    EXPECT_DOUBLE_EQ(transfer.getAverageSize(), 1024.0);
    EXPECT_DOUBLE_EQ(transfer.getAverageTime(), 10.5);
    EXPECT_DOUBLE_EQ(transfer.getAverageRateMBps(), 1024.0 / 10.5);
}

TEST_F(AggregatedTransferTest, MultipleTransfers)
{
    transfer.addTransfer(1000, 10.0);
    transfer.addTransfer(2000, 20.0);
    transfer.addTransfer(3000, 30.0);

    EXPECT_EQ(transfer.totalBytes, 6000u);
    EXPECT_DOUBLE_EQ(transfer.totalTimeUs, 60.0);
    EXPECT_EQ(transfer.count, 3);
    EXPECT_DOUBLE_EQ(transfer.getAverageSize(), 2000.0);
    EXPECT_DOUBLE_EQ(transfer.getAverageTime(), 20.0);
}

TEST_F(AggregatedTransferTest, ZeroBytes)
{
    transfer.addTransfer(0, 10.0);

    EXPECT_EQ(transfer.totalBytes, 0u);
    EXPECT_DOUBLE_EQ(transfer.totalTimeUs, 10.0);
    EXPECT_EQ(transfer.count, 1);
    EXPECT_DOUBLE_EQ(transfer.getAverageSize(), 0.0);
}

TEST_F(AggregatedTransferTest, ZeroTime)
{
    transfer.addTransfer(1000, 0.0);

    EXPECT_EQ(transfer.totalBytes, 1000u);
    EXPECT_DOUBLE_EQ(transfer.totalTimeUs, 0.0);
    EXPECT_DOUBLE_EQ(transfer.getAverageRateMBps(), 0.0);  // Should handle division by zero
}

TEST_F(AggregatedTransferTest, LinearRegressionWithTwoPoints)
{
    // Only 2 different sizes - should fail (need at least 3)
    transfer.addTransfer(0, 10.0);
    transfer.addTransfer(1000, 11.0);

    double latency, rate;
    EXPECT_FALSE(transfer.getLinearRegressionMetrics(latency, rate));
    EXPECT_DOUBLE_EQ(latency, 0.0);
    EXPECT_DOUBLE_EQ(rate, 0.0);
}

TEST_F(AggregatedTransferTest, LinearRegressionWithOnePoint)
{
    transfer.addTransfer(1000, 10.0);

    double latency, rate;
    EXPECT_FALSE(transfer.getLinearRegressionMetrics(latency, rate));
    EXPECT_DOUBLE_EQ(latency, 0.0);
    EXPECT_DOUBLE_EQ(rate, 0.0);
}

TEST_F(AggregatedTransferTest, LinearRegressionWithNoPoints)
{
    double latency, rate;
    EXPECT_FALSE(transfer.getLinearRegressionMetrics(latency, rate));
}

TEST_F(AggregatedTransferTest, LinearRegressionWithIdenticalSizes)
{
    // All same size - should fail (vertical line)
    transfer.addTransfer(1000, 10.0);
    transfer.addTransfer(1000, 11.0);
    transfer.addTransfer(1000, 12.0);

    double latency, rate;
    EXPECT_FALSE(transfer.getLinearRegressionMetrics(latency, rate));
}

TEST_F(AggregatedTransferTest, LinearRegressionWithZeroSlope)
{
    // Constant time - slope = 0 (infinite bandwidth) - should be rejected as not physically meaningful
    transfer.addTransfer(0, 10.0);
    transfer.addTransfer(1000, 10.0);
    transfer.addTransfer(2000, 10.0);
    transfer.addTransfer(3000, 10.0);

    double latency, rate;
    EXPECT_FALSE(transfer.getLinearRegressionMetrics(latency, rate));
    EXPECT_DOUBLE_EQ(latency, 0.0);
    EXPECT_DOUBLE_EQ(rate, 0.0);
}

TEST_F(AggregatedTransferTest, LinearRegressionWithNegativeSlope)
{
    // Negative slope (time decreases with size) - physically meaningless for bandwidth
    transfer.addTransfer(0, 100.0);
    transfer.addTransfer(1000, 90.0);
    transfer.addTransfer(2000, 80.0);

    double latency, rate;
    EXPECT_FALSE(transfer.getLinearRegressionMetrics(latency, rate));
    EXPECT_DOUBLE_EQ(latency, 0.0);
    EXPECT_DOUBLE_EQ(rate, 0.0);
}

TEST_F(AggregatedTransferTest, LinearRegressionRealisticCase)
{
    // Realistic case: 10us latency, 0.01 us/byte (100 MB/s)
    transfer.addTransfer(0, 10.0);
    transfer.addTransfer(1000, 20.0);
    transfer.addTransfer(2000, 30.0);
    transfer.addTransfer(5000, 60.0);

    double latency, rate;
    EXPECT_TRUE(transfer.getLinearRegressionMetrics(latency, rate));
    EXPECT_NEAR(latency, 10.0, 0.5);
    EXPECT_NEAR(rate, 100.0, 1.0);  // 1/0.01 = 100 MB/s
}

// Test fixture for AggregatedOperationBase
class AggregatedOperationBaseTest : public ::testing::Test
{
protected:
    AggregatedOperationBase operation;
};

TEST_F(AggregatedOperationBaseTest, InitialState)
{
    EXPECT_EQ(operation.totalBytes, 0u);
    EXPECT_EQ(operation.totalTimeUs, 0.0);
    EXPECT_EQ(operation.count, 0);
    EXPECT_EQ(operation.cachedTotalTransferCount, 0);
    EXPECT_EQ(operation.cachedTotalTransferBytes, 0u);
    EXPECT_DOUBLE_EQ(operation.cachedTotalTransferTimeUs, 0.0);
    EXPECT_TRUE(operation.transfers.empty());
}

TEST_F(AggregatedOperationBaseTest, AddOperation)
{
    operation.addOperation(2048, 15.5);

    EXPECT_EQ(operation.totalBytes, 2048u);
    EXPECT_DOUBLE_EQ(operation.totalTimeUs, 15.5);
    EXPECT_EQ(operation.count, 1);
    EXPECT_DOUBLE_EQ(operation.getAverageSize(), 2048.0);
    EXPECT_DOUBLE_EQ(operation.getAverageTime(), 15.5);
}

TEST_F(AggregatedOperationBaseTest, MultipleOperations)
{
    operation.addOperation(1000, 10.0);
    operation.addOperation(2000, 20.0);
    operation.addOperation(3000, 30.0);

    EXPECT_EQ(operation.count, 3);
    EXPECT_EQ(operation.totalBytes, 6000u);
    EXPECT_DOUBLE_EQ(operation.totalTimeUs, 60.0);
    EXPECT_DOUBLE_EQ(operation.getAverageSize(), 2000.0);
    EXPECT_DOUBLE_EQ(operation.getAverageTime(), 20.0);
}

TEST_F(AggregatedOperationBaseTest, AddTransferToCache)
{
    operation.addTransferToCache(512, 5.0);
    operation.addTransferToCache(1024, 10.0);

    EXPECT_EQ(operation.cachedTotalTransferCount, 2);
    EXPECT_EQ(operation.cachedTotalTransferBytes, 1536u);
    EXPECT_DOUBLE_EQ(operation.cachedTotalTransferTimeUs, 15.0);
}

TEST_F(AggregatedOperationBaseTest, GetTotalTransferCount)
{
    EXPECT_EQ(operation.getTotalTransferCount(), 0);

    operation.addTransferToCache(100, 1.0);
    EXPECT_EQ(operation.getTotalTransferCount(), 1);

    operation.addTransferToCache(200, 2.0);
    EXPECT_EQ(operation.getTotalTransferCount(), 2);
}

TEST_F(AggregatedOperationBaseTest, GetAverageTransferCountWithNoOperations)
{
    operation.addTransferToCache(100, 1.0);
    EXPECT_DOUBLE_EQ(operation.getAverageTransferCount(), 0.0);  // No operations added yet
}

TEST_F(AggregatedOperationBaseTest, GetAverageTransferCountWithOperations)
{
    operation.addOperation(1000, 10.0);
    operation.addOperation(2000, 20.0);
    operation.addTransferToCache(100, 1.0);
    operation.addTransferToCache(200, 2.0);
    operation.addTransferToCache(300, 3.0);
    operation.addTransferToCache(400, 4.0);

    EXPECT_EQ(operation.count, 2);
    EXPECT_EQ(operation.cachedTotalTransferCount, 4);
    EXPECT_DOUBLE_EQ(operation.getAverageTransferCount(), 2.0);  // 4 transfers / 2 operations
}

TEST_F(AggregatedOperationBaseTest, GetAverageTransferSizeWithNoTransfers)
{
    EXPECT_DOUBLE_EQ(operation.getAverageTransferSize(), 0.0);
}

TEST_F(AggregatedOperationBaseTest, GetAverageTransferSizeWithTransfers)
{
    operation.addTransferToCache(100, 1.0);
    operation.addTransferToCache(200, 2.0);
    operation.addTransferToCache(300, 3.0);

    double avgSize = (100.0 + 200.0 + 300.0) / 3.0;
    EXPECT_DOUBLE_EQ(operation.getAverageTransferSize(), avgSize);
}

TEST_F(AggregatedOperationBaseTest, GetAverageTransferTimeWithNoTransfers)
{
    EXPECT_DOUBLE_EQ(operation.getAverageTransferTime(), 0.0);
}

TEST_F(AggregatedOperationBaseTest, GetAverageTransferTimeWithTransfers)
{
    operation.addTransferToCache(100, 1.0);
    operation.addTransferToCache(200, 3.0);
    operation.addTransferToCache(300, 5.0);

    double avgTime = (1.0 + 3.0 + 5.0) / 3.0;
    EXPECT_DOUBLE_EQ(operation.getAverageTransferTime(), avgTime);
}

// Test fixture for AggregatedP2P
class AggregatedP2PTest : public ::testing::Test
{
protected:
    AggregatedP2P p2p;
};

TEST_F(AggregatedP2PTest, InheritsFromBase)
{
    // Should have all base class functionality
    p2p.addOperation(1000, 10.0);
    EXPECT_EQ(p2p.count, 1);
    EXPECT_EQ(p2p.totalBytes, 1000u);
}

TEST_F(AggregatedP2PTest, ConvenienceMethodDelegates)
{
    p2p.addP2P(2000, 20.0);
    EXPECT_EQ(p2p.count, 1);
    EXPECT_EQ(p2p.totalBytes, 2000u);
    EXPECT_DOUBLE_EQ(p2p.totalTimeUs, 20.0);
}

// Test fixture for AggregatedCollective
class AggregatedCollectiveTest : public ::testing::Test
{
protected:
    AggregatedCollective collective;
};

TEST_F(AggregatedCollectiveTest, InheritsFromBase)
{
    collective.addOperation(3000, 30.0);
    EXPECT_EQ(collective.count, 1);
    EXPECT_EQ(collective.totalBytes, 3000u);
}

TEST_F(AggregatedCollectiveTest, ConvenienceMethodDelegates)
{
    collective.addCollective(4000, 40.0);
    EXPECT_EQ(collective.count, 1);
    EXPECT_EQ(collective.totalBytes, 4000u);
    EXPECT_DOUBLE_EQ(collective.totalTimeUs, 40.0);
}

// Test fixture for WindowAggregator
class WindowAggregatorTest : public ::testing::Test
{
protected:
    WindowAggregator* aggregator;

    void SetUp() override
    {
        aggregator = new WindowAggregator(0);  // rank 0
    }

    void TearDown() override
    {
        delete aggregator;
    }

    otelEventHandle_t createCollectiveEvent(const char* func, const char* algo, const char* proto, uint8_t channels,
                                            size_t bytes, double startTs, double endTs)
    {
        otelEventHandle_t event = {};
        event.type              = ncclProfileColl;
        event.coll.func         = func;
        event.coll.algo         = algo;
        event.coll.proto        = proto;
        event.coll.nChannels    = channels;
        event.coll.bytes        = bytes;
        event.startTs           = startTs;
        event.endTs             = endTs;
        event.parentObj         = (void*)0x1234;
        event.rank              = 0;
        return event;
    }

    otelEventHandle_t createP2PEvent(const char* func, int peer, uint8_t channels, size_t bytes, double startTs,
                                     double endTs)
    {
        otelEventHandle_t event = {};
        event.type              = ncclProfileP2p;
        event.p2p.func          = func;
        event.p2p.peer          = peer;
        event.p2p.nChannels     = channels;
        event.p2p.bytes         = bytes;
        event.startTs           = startTs;
        event.endTs             = endTs;
        event.parentObj         = (void*)0x5678;
        event.rank              = 0;
        return event;
    }

    otelEventHandle_t createProxyOpEvent(int peer, uint8_t channelId, int chunkSize, double startTs, double endTs,
                                         void* parentObj = nullptr)
    {
        otelEventHandle_t event = {};
        event.type              = ncclProfileProxyOp;
        event.proxyOp.peer      = peer;
        event.proxyOp.channelId = channelId;
        event.proxyOp.chunkSize = chunkSize;
        event.startTs           = startTs;
        event.endTs             = endTs;
        event.parentObj         = parentObj;
        event.rank              = 0;
        return event;
    }

    otelEventHandle_t createProxyStepEvent(int step, size_t transSize, double startTs, double sendWaitTs, double endTs,
                                           void* parentObj = nullptr)
    {
        otelEventHandle_t event     = {};
        event.type                  = ncclProfileProxyStep;
        event.proxyStep.step        = step;
        event.proxyStep.transSize   = transSize;
        event.proxyStep.sendWaitTs  = sendWaitTs;
        event.proxyStep.hasSendWait = (transSize > 0);
        event.startTs               = startTs;
        event.endTs                 = endTs;
        event.parentObj             = parentObj;
        event.rank                  = 0;
        return event;
    }
};

TEST_F(WindowAggregatorTest, InitialState)
{
    EXPECT_TRUE(aggregator->getCollectives().empty());
    EXPECT_TRUE(aggregator->getP2Ps().empty());
    EXPECT_TRUE(aggregator->getRankTransfers().empty());
    EXPECT_TRUE(aggregator->getChannelTransfers().empty());
}

TEST_F(WindowAggregatorTest, AggregatesSingleCollective)
{
    // Create Coll event (tracked but not aggregated yet)
    auto coll = createCollectiveEvent("AllReduce", "Ring", "Simple", 2, 1024, 0.0, 10.0);
    aggregator->addEvent(coll);

    // Create ProxyOp events that reference this Coll
    auto proxyOp1 = createProxyOpEvent(1, 0, 262144, 10.0, 200.0, &coll);

    // Create ProxyStep with SendWait for ProxyOp1
    auto proxyStep1 = createProxyStepEvent(0, 512, 10.0, 100.0, 200.0, &proxyOp1);

    // Add in sequence: Coll, ProxyStep, ProxyOp
    aggregator->addEvent(proxyStep1);
    aggregator->addEvent(proxyOp1);

    // Finalize to calculate Coll duration
    aggregator->finalize();

    const auto& collectives = aggregator->getCollectives();
    EXPECT_EQ(collectives.size(), 1u);

    auto it = collectives.find("Comm0_AllReduce_Ring_Simple_2Chnl");
    ASSERT_NE(it, collectives.end());
    EXPECT_EQ(it->second.count, 1);
    EXPECT_EQ(it->second.totalBytes, 1024u);
    // Duration is now from Coll.start (0.0) to ProxyOp.end (200.0)
    EXPECT_DOUBLE_EQ(it->second.totalTimeUs, 200.0);
}

TEST_F(WindowAggregatorTest, AggregatesMultipleCollectivesWithSameKey)
{
    // First Coll
    auto coll1 = createCollectiveEvent("AllReduce", "Ring", "Simple", 2, 1024, 0.0, 10.0);
    aggregator->addEvent(coll1);
    auto proxyOp1   = createProxyOpEvent(1, 0, 262144, 10.0, 100.0, &coll1);
    auto proxyStep1 = createProxyStepEvent(0, 512, 10.0, 50.0, 100.0, &proxyOp1);
    aggregator->addEvent(proxyStep1);
    aggregator->addEvent(proxyOp1);

    // Second Coll (same key)
    auto coll2 = createCollectiveEvent("AllReduce", "Ring", "Simple", 2, 2048, 100.0, 110.0);
    aggregator->addEvent(coll2);
    auto proxyOp2   = createProxyOpEvent(1, 0, 262144, 110.0, 200.0, &coll2);
    auto proxyStep2 = createProxyStepEvent(0, 1024, 110.0, 150.0, 200.0, &proxyOp2);
    aggregator->addEvent(proxyStep2);
    aggregator->addEvent(proxyOp2);

    aggregator->finalize();

    const auto& collectives = aggregator->getCollectives();
    EXPECT_EQ(collectives.size(), 1u);

    auto it = collectives.find("Comm0_AllReduce_Ring_Simple_2Chnl");
    ASSERT_NE(it, collectives.end());
    EXPECT_EQ(it->second.count, 2);
    EXPECT_EQ(it->second.totalBytes, 3072u);
    // Durations: coll1 (0->100=100us) + coll2 (100->200=100us) = 200us
    EXPECT_DOUBLE_EQ(it->second.totalTimeUs, 200.0);
}

TEST_F(WindowAggregatorTest, AggregatesMultipleCollectivesWithDifferentKeys)
{
    // First Coll
    auto coll1 = createCollectiveEvent("AllReduce", "Ring", "Simple", 2, 1024, 0.0, 10.0);
    aggregator->addEvent(coll1);
    auto proxyOp1   = createProxyOpEvent(1, 0, 262144, 10.0, 100.0, &coll1);
    auto proxyStep1 = createProxyStepEvent(0, 512, 10.0, 50.0, 100.0, &proxyOp1);
    aggregator->addEvent(proxyStep1);
    aggregator->addEvent(proxyOp1);

    // Second Coll (different key)
    auto coll2 = createCollectiveEvent("AllGather", "Tree", "LL", 4, 2048, 100.0, 110.0);
    aggregator->addEvent(coll2);
    auto proxyOp2   = createProxyOpEvent(1, 0, 262144, 110.0, 200.0, &coll2);
    auto proxyStep2 = createProxyStepEvent(0, 1024, 110.0, 150.0, 200.0, &proxyOp2);
    aggregator->addEvent(proxyStep2);
    aggregator->addEvent(proxyOp2);

    aggregator->finalize();

    const auto& collectives = aggregator->getCollectives();
    EXPECT_EQ(collectives.size(), 2u);
    EXPECT_TRUE(collectives.find("Comm0_AllReduce_Ring_Simple_2Chnl") != collectives.end());
    EXPECT_TRUE(collectives.find("Comm0_AllGather_Tree_LL_4Chnl") != collectives.end());
}

TEST_F(WindowAggregatorTest, AggregatesSingleP2P)
{
    // Create P2P event
    auto p2p = createP2PEvent("Send", 3, 2, 512, 0.0, 5.0);
    aggregator->addEvent(p2p);

    // Create ProxyOp that references this P2P
    auto proxyOp   = createProxyOpEvent(3, 0, 262144, 5.0, 50.0, &p2p);
    auto proxyStep = createProxyStepEvent(0, 256, 5.0, 25.0, 50.0, &proxyOp);
    aggregator->addEvent(proxyStep);
    aggregator->addEvent(proxyOp);

    aggregator->finalize();

    const auto& p2ps = aggregator->getP2Ps();
    EXPECT_EQ(p2ps.size(), 1u);

    // New P2P key format: Comm<hash>_(<hostname>)_<func>_Pipeline<src>ToPipeline<peer>_<nChannels>Chnl
    // When commState is NULL: hash=0, hostname="unknown", src=rank=0
    auto it = p2ps.find("Comm0_(unknown)_Send_Pipeline0ToPipeline3_2Chnl");
    ASSERT_NE(it, p2ps.end());
    EXPECT_EQ(it->second.count, 1);
    EXPECT_EQ(it->second.totalBytes, 512u);
    // Duration from P2P.start (0.0) to ProxyOp.end (50.0)
    EXPECT_DOUBLE_EQ(it->second.totalTimeUs, 50.0);
}

TEST_F(WindowAggregatorTest, AggregatesSingleProxyOp)
{
    // Create ProxyOp with ProxyStep transfer data
    auto proxyOp   = createProxyOpEvent(2, 1, 256, 0.0, 3.0);
    auto proxyStep = createProxyStepEvent(0, 128, 0.0, 1.0, 3.0, &proxyOp);
    aggregator->addEvent(proxyStep);
    aggregator->addEvent(proxyOp);
    aggregator->finalize();  // Must finalize to link ProxyOps to transfers

    const auto& rankTransfers = aggregator->getRankTransfers();
    EXPECT_EQ(rankTransfers.size(), 1u);

    // New key format: Comm<hash>_Rank<X>_ToPeer<peer> for COLLECTIVE (commState NULL = not P2P)
    auto it = rankTransfers.find("Comm0_Rank0_ToPeer2");
    ASSERT_NE(it, rankTransfers.end());
    EXPECT_EQ(it->second.count, 1);
    // Uses ProxyStep.transSize (128), not ProxyOp.chunkSize (256)
    EXPECT_EQ(it->second.totalBytes, 128u);
}

TEST_F(WindowAggregatorTest, LinksProxyOpToCollective)
{
    // First add a collective
    auto collEvent = createCollectiveEvent("AllReduce", "Ring", "Simple", 2, 1024, 0.0, 10.0);
    aggregator->addEvent(collEvent);

    // Then add a proxy op with parentObj pointing to the Coll event
    auto proxyOp   = createProxyOpEvent(1, 0, 128, 1.0, 2.0, &collEvent);
    auto proxyStep = createProxyStepEvent(0, 64, 1.0, 1.5, 2.0, &proxyOp);
    aggregator->addEvent(proxyStep);
    aggregator->addEvent(proxyOp);

    aggregator->finalize();

    // Check that the collective has the transfer cached
    const auto& collectives = aggregator->getCollectives();
    auto it                 = collectives.find("Comm0_AllReduce_Ring_Simple_2Chnl");
    ASSERT_NE(it, collectives.end());
    EXPECT_EQ(it->second.cachedTotalTransferCount, 1);
    // Uses ProxyStep.transSize (64), not ProxyOp.chunkSize (128)
    EXPECT_EQ(it->second.cachedTotalTransferBytes, 64u);
    EXPECT_DOUBLE_EQ(it->second.cachedTotalTransferTimeUs, 0.5);  // 2.0 - 1.5 = 0.5
}

TEST_F(WindowAggregatorTest, LinksMultipleProxyOpsToCollective)
{
    auto collEvent = createCollectiveEvent("AllReduce", "Ring", "Simple", 2, 1024, 0.0, 10.0);
    aggregator->addEvent(collEvent);

    // Add multiple proxy ops with ProxySteps
    auto proxyOp1   = createProxyOpEvent(1, 0, 128, 1.0, 2.0, &collEvent);
    auto proxyStep1 = createProxyStepEvent(0, 100, 1.0, 1.5, 2.0, &proxyOp1);  // transfer time = 0.5
    aggregator->addEvent(proxyStep1);
    aggregator->addEvent(proxyOp1);

    auto proxyOp2   = createProxyOpEvent(1, 1, 256, 2.0, 4.0, &collEvent);
    auto proxyStep2 = createProxyStepEvent(0, 200, 2.0, 3.0, 4.0, &proxyOp2);  // transfer time = 1.0
    aggregator->addEvent(proxyStep2);
    aggregator->addEvent(proxyOp2);

    auto proxyOp3   = createProxyOpEvent(2, 0, 512, 3.0, 6.0, &collEvent);
    auto proxyStep3 = createProxyStepEvent(0, 300, 3.0, 4.5, 6.0, &proxyOp3);  // transfer time = 1.5
    aggregator->addEvent(proxyStep3);
    aggregator->addEvent(proxyOp3);

    aggregator->finalize();

    const auto& collectives = aggregator->getCollectives();
    auto it                 = collectives.find("Comm0_AllReduce_Ring_Simple_2Chnl");
    ASSERT_NE(it, collectives.end());
    EXPECT_EQ(it->second.cachedTotalTransferCount, 3);
    // Uses ProxyStep.transSize: 100 + 200 + 300 = 600
    EXPECT_EQ(it->second.cachedTotalTransferBytes, 600u);
    // Transfer times: 0.5 + 1.0 + 1.5 = 3.0
    EXPECT_DOUBLE_EQ(it->second.cachedTotalTransferTimeUs, 3.0);
    EXPECT_DOUBLE_EQ(it->second.getAverageTransferCount(), 3.0);
}

TEST_F(WindowAggregatorTest, LinksProxyOpToP2P)
{
    auto p2pEvent = createP2PEvent("Send", 3, 2, 512, 0.0, 5.0);
    aggregator->addEvent(p2pEvent);

    auto proxyOp   = createProxyOpEvent(3, 0, 64, 1.0, 1.5, &p2pEvent);
    auto proxyStep = createProxyStepEvent(0, 32, 1.0, 1.2, 1.5, &proxyOp);
    aggregator->addEvent(proxyStep);
    aggregator->addEvent(proxyOp);

    aggregator->finalize();

    const auto& p2ps = aggregator->getP2Ps();
    // New P2P key format
    auto it = p2ps.find("Comm0_(unknown)_Send_Pipeline0ToPipeline3_2Chnl");
    ASSERT_NE(it, p2ps.end());
    EXPECT_EQ(it->second.cachedTotalTransferCount, 1);
    // Uses ProxyStep.transSize (32), not ProxyOp.chunkSize (64)
    EXPECT_EQ(it->second.cachedTotalTransferBytes, 32u);
}

TEST_F(WindowAggregatorTest, HandlesNegativeDuration)
{
    // ProxyOp.endTs < Coll.startTs should result in 0 duration
    auto coll = createCollectiveEvent("AllReduce", "Ring", "Simple", 2, 1024, 10.0, 15.0);
    aggregator->addEvent(coll);

    // ProxyOp ends BEFORE Coll starts (negative duration)
    auto proxyOp   = createProxyOpEvent(1, 0, 128, 0.0, 5.0, &coll);
    auto proxyStep = createProxyStepEvent(0, 64, 0.0, 2.0, 5.0, &proxyOp);
    aggregator->addEvent(proxyStep);
    aggregator->addEvent(proxyOp);

    aggregator->finalize();

    const auto& collectives = aggregator->getCollectives();
    auto it                 = collectives.find("Comm0_AllReduce_Ring_Simple_2Chnl");
    ASSERT_NE(it, collectives.end());
    EXPECT_DOUBLE_EQ(it->second.totalTimeUs, 0.0);  // Should be clamped to 0
}

TEST_F(WindowAggregatorTest, HandlesZeroDuration)
{
    auto coll = createCollectiveEvent("AllReduce", "Ring", "Simple", 2, 1024, 10.0, 15.0);
    aggregator->addEvent(coll);

    // ProxyOp ends at same time as Coll starts (zero duration)
    auto proxyOp   = createProxyOpEvent(1, 0, 128, 5.0, 10.0, &coll);
    auto proxyStep = createProxyStepEvent(0, 64, 5.0, 7.0, 10.0, &proxyOp);
    aggregator->addEvent(proxyStep);
    aggregator->addEvent(proxyOp);

    aggregator->finalize();

    const auto& collectives = aggregator->getCollectives();
    auto it                 = collectives.find("Comm0_AllReduce_Ring_Simple_2Chnl");
    ASSERT_NE(it, collectives.end());
    EXPECT_DOUBLE_EQ(it->second.totalTimeUs, 0.0);
}

TEST_F(WindowAggregatorTest, HandlesProxyOpWithoutParent)
{
    // Proxy op without parentObj should still be aggregated in rankTransfers
    auto proxyOp   = createProxyOpEvent(1, 0, 128, 0.0, 1.0, nullptr);
    auto proxyStep = createProxyStepEvent(0, 64, 0.0, 0.5, 1.0, &proxyOp);
    aggregator->addEvent(proxyStep);
    aggregator->addEvent(proxyOp);
    aggregator->finalize();  // Must finalize to link ProxyOps to transfers

    const auto& rankTransfers = aggregator->getRankTransfers();
    EXPECT_EQ(rankTransfers.size(), 1u);
    // New key format: Comm<hash>_Rank<X>_ToPeer<peer> for COLLECTIVE (commState NULL = not P2P)
    auto it = rankTransfers.find("Comm0_Rank0_ToPeer1");
    ASSERT_NE(it, rankTransfers.end());
    EXPECT_EQ(it->second.count, 1);
}

TEST_F(WindowAggregatorTest, ComplexScenarioWithMultipleEventTypes)
{
    // Collective 1
    auto coll1 = createCollectiveEvent("AllReduce", "Ring", "Simple", 2, 2048, 0.0, 20.0);
    aggregator->addEvent(coll1);

    // Proxy ops for collective 1 (with ProxySteps)
    auto proxyOp1   = createProxyOpEvent(1, 0, 256, 1.0, 2.0, &coll1);
    auto proxyStep1 = createProxyStepEvent(0, 128, 1.0, 1.5, 2.0, &proxyOp1);
    aggregator->addEvent(proxyStep1);
    aggregator->addEvent(proxyOp1);

    auto proxyOp2   = createProxyOpEvent(1, 1, 256, 2.0, 3.0, &coll1);
    auto proxyStep2 = createProxyStepEvent(0, 128, 2.0, 2.5, 3.0, &proxyOp2);
    aggregator->addEvent(proxyStep2);
    aggregator->addEvent(proxyOp2);

    // P2P
    auto p2p1 = createP2PEvent("Send", 2, 1, 1024, 20.0, 30.0);
    aggregator->addEvent(p2p1);

    // Proxy op for P2P (with ProxyStep)
    auto proxyOp3   = createProxyOpEvent(2, 0, 128, 21.0, 22.0, &p2p1);
    auto proxyStep3 = createProxyStepEvent(0, 64, 21.0, 21.5, 22.0, &proxyOp3);
    aggregator->addEvent(proxyStep3);
    aggregator->addEvent(proxyOp3);

    // Standalone proxy op (with ProxyStep)
    auto proxyOp4   = createProxyOpEvent(3, 0, 512, 30.0, 35.0, nullptr);
    auto proxyStep4 = createProxyStepEvent(0, 256, 30.0, 32.0, 35.0, &proxyOp4);
    aggregator->addEvent(proxyStep4);
    aggregator->addEvent(proxyOp4);

    // Finalize to calculate durations
    aggregator->finalize();

    // Verify aggregation
    EXPECT_EQ(aggregator->getCollectives().size(), 1u);
    EXPECT_EQ(aggregator->getP2Ps().size(), 1u);
    EXPECT_EQ(aggregator->getRankTransfers().size(), 3u);  // Rank0->1, Rank0->2, Rank0->3

    // Check collective has transfers
    auto collIt = aggregator->getCollectives().find("Comm0_AllReduce_Ring_Simple_2Chnl");
    ASSERT_NE(collIt, aggregator->getCollectives().end());
    EXPECT_EQ(collIt->second.cachedTotalTransferCount, 2);

    // Check P2P has transfer (new key format)
    auto p2pIt = aggregator->getP2Ps().find("Comm0_(unknown)_Send_Pipeline0ToPipeline2_1Chnl");
    ASSERT_NE(p2pIt, aggregator->getP2Ps().end());
    EXPECT_EQ(p2pIt->second.cachedTotalTransferCount, 1);
}

TEST_F(WindowAggregatorTest, StressTestManyEvents)
{
    // Stress test: add thousands of events
    // For simplicity, create complete event sequences (Coll/P2P with ProxyOp+ProxyStep)
    const int NUM_COLLS              = 1000;
    const int NUM_P2PS               = 500;
    const int NUM_STANDALONE_PROXIES = 500;

    // Create collectives with ProxyOps
    for (int i = 0; i < NUM_COLLS; i++)
    {
        auto coll = createCollectiveEvent("AllReduce", "Ring", "Simple", 2, 1024, i * 10.0, i * 10.0 + 5.0);
        aggregator->addEvent(coll);

        // Add 2 ProxyOps for this Coll
        for (int ch = 0; ch < 2; ch++)
        {
            auto proxyOp = createProxyOpEvent(i % 4, ch, 128, i * 10.0 + ch, i * 10.0 + ch + 1.0, &coll);
            auto proxyStep =
                createProxyStepEvent(0, 64, i * 10.0 + ch, i * 10.0 + ch + 0.5, i * 10.0 + ch + 1.0, &proxyOp);
            aggregator->addEvent(proxyStep);
            aggregator->addEvent(proxyOp);
        }
    }

    // Create P2Ps with ProxyOps
    for (int i = 0; i < NUM_P2PS; i++)
    {
        auto p2p = createP2PEvent("Send", i % 4, 1, 512, i * 5.0, i * 5.0 + 3.0);
        aggregator->addEvent(p2p);

        auto proxyOp   = createProxyOpEvent(i % 4, 0, 64, i * 5.0, i * 5.0 + 2.0, &p2p);
        auto proxyStep = createProxyStepEvent(0, 32, i * 5.0, i * 5.0 + 1.0, i * 5.0 + 2.0, &proxyOp);
        aggregator->addEvent(proxyStep);
        aggregator->addEvent(proxyOp);
    }

    // Create standalone ProxyOps (orphaned)
    for (int i = 0; i < NUM_STANDALONE_PROXIES; i++)
    {
        auto proxyOp   = createProxyOpEvent(i % 4, i % 8, 128, i * 2.0, i * 2.0 + 1.0, nullptr);
        auto proxyStep = createProxyStepEvent(0, 64, i * 2.0, i * 2.0 + 0.5, i * 2.0 + 1.0, &proxyOp);
        aggregator->addEvent(proxyStep);
        aggregator->addEvent(proxyOp);
    }

    // Finalize before checking
    aggregator->finalize();

    // Should have aggregated many events
    EXPECT_GT(aggregator->getCollectives().size(), 0u);
    EXPECT_GT(aggregator->getP2Ps().size(), 0u);
    EXPECT_GT(aggregator->getRankTransfers().size(), 0u);
}

TEST_F(WindowAggregatorTest, StressTestDeepProxyOpNesting)
{
    // Create one collective with many proxy ops
    auto coll = createCollectiveEvent("AllReduce", "Ring", "Simple", 2, 1024, 0.0, 100.0);
    aggregator->addEvent(coll);

    // Add 1000 proxy ops (with ProxySteps) to the same collective
    for (int i = 0; i < 1000; i++)
    {
        auto proxy     = createProxyOpEvent(i % 4, i % 8, 128 + i, i * 0.1, i * 0.1 + 0.05, &coll);
        auto proxyStep = createProxyStepEvent(0, 64 + i, i * 0.1, i * 0.1 + 0.02, i * 0.1 + 0.05, &proxy);
        aggregator->addEvent(proxyStep);
        aggregator->addEvent(proxy);
    }

    // Finalize before checking
    aggregator->finalize();

    const auto& collectives = aggregator->getCollectives();
    auto it                 = collectives.find("Comm0_AllReduce_Ring_Simple_2Chnl");
    ASSERT_NE(it, collectives.end());

    // Check cache was properly updated
    EXPECT_EQ(it->second.cachedTotalTransferCount, 1000);
    EXPECT_GT(it->second.cachedTotalTransferBytes, 0u);
    EXPECT_GT(it->second.cachedTotalTransferTimeUs, 0.0);

    // Verify O(1) access works correctly
    EXPECT_DOUBLE_EQ(it->second.getAverageTransferCount(), 1000.0);
}

TEST_F(WindowAggregatorTest, OrphanedProxyOps)
{
    // Add proxy ops before their parent collectives (orphaned)
    // These ProxyOps point to a fake parent that doesn't exist yet
    auto proxy1     = createProxyOpEvent(1, 0, 128, 1.0, 2.0, (void*)0x1234);
    auto proxyStep1 = createProxyStepEvent(0, 64, 1.0, 1.5, 2.0, &proxy1);
    aggregator->addEvent(proxyStep1);
    aggregator->addEvent(proxy1);

    auto proxy2     = createProxyOpEvent(1, 1, 256, 2.0, 4.0, (void*)0x1234);
    auto proxyStep2 = createProxyStepEvent(0, 128, 2.0, 3.0, 4.0, &proxy2);
    aggregator->addEvent(proxyStep2);
    aggregator->addEvent(proxy2);

    // Add parent collective later (but with different identity, so still orphaned)
    // Since no ProxyOps are linked, the collective uses endTs-startTs for duration
    // (internal links fallback behavior)
    auto coll = createCollectiveEvent("AllReduce", "Ring", "Simple", 2, 1024, 0.0, 10.0);
    aggregator->addEvent(coll);

    // Finalize to calculate durations
    aggregator->finalize();

    // Orphaned proxy ops should still be in rankTransfers
    EXPECT_GT(aggregator->getRankTransfers().size(), 0u);

    // Collective without linked ProxyOps now uses endTs-startTs for duration (internal links fallback)
    // So it should be found with duration = 10.0 - 0.0 = 10.0 us
    const auto& collectives = aggregator->getCollectives();
    auto it                 = collectives.find("Comm0_AllReduce_Ring_Simple_2Chnl");
    EXPECT_NE(it, collectives.end());  // Should be found (uses endTs for duration)
    if (it != collectives.end())
    {
        EXPECT_EQ(it->second.count, 1u);
    }
}
