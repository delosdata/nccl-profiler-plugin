// Tests for the NCCL profiler v5/v6/v7 entry points (profiler_compat.h + profiler_otel.cc).
// They only build against NCCL headers that ship the corresponding profiler_vN.h.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>

#include "../../aggregation.h"
#include "../../communicator_state.h"
#include "../../events.h"
#include "../../profiler_compat.h"
#include "../../profiler_otel.h"
#include "../test_helpers.h"

#if OTEL_HAS_PROFILER_V5

extern "C"
{
    extern volatile ncclProfiler_v4_t ncclProfiler_v4;
    extern volatile ncclProfiler_v5_t ncclProfiler_v5;
#if OTEL_HAS_PROFILER_V6
    extern volatile ncclProfiler_v6_t ncclProfiler_v6;
#endif
#if OTEL_HAS_PROFILER_V7
    extern volatile ncclProfiler_v7_t ncclProfiler_v7;
#endif
}

static void quiet_logger(ncclDebugLogLevel, unsigned long, const char*, int, const char*, ...) {}

static uint32_t inProgress(void* context, uint8_t idx)
{
    auto* ctx = (struct eventContext*)context;
    return ctx->commState->windows[idx].in_progress_count.load();
}

// ---------------------------------------------------------------------------
// Descriptor translation (pure functions)
// ---------------------------------------------------------------------------

template <typename DescrT>
class TranslateTest : public ::testing::Test
{
};

#if OTEL_HAS_PROFILER_V7
using DescrTypes = ::testing::Types<ncclProfilerEventDescr_v5_t, ncclProfilerEventDescr_v6_t,
                                    ncclProfilerEventDescr_v7_t>;
#elif OTEL_HAS_PROFILER_V6
using DescrTypes = ::testing::Types<ncclProfilerEventDescr_v5_t, ncclProfilerEventDescr_v6_t>;
#else
using DescrTypes = ::testing::Types<ncclProfilerEventDescr_v5_t>;
#endif
TYPED_TEST_SUITE(TranslateTest, DescrTypes);

TYPED_TEST(TranslateTest, CollUsesParentGroupAndCopiesFields)
{
    TypeParam in;
    memset(&in, 0, sizeof(in));
    in.type             = ncclProfileColl;
    in.parentObj        = (void*)0x1111;  // CollApi handle in v5+
    in.rank             = 3;
    in.coll.seqNumber   = 42;
    in.coll.func        = "AllReduce";
    in.coll.count       = 1024;
    in.coll.root        = 1;
    in.coll.datatype    = "ncclFloat32";
    in.coll.nChannels   = 8;
    in.coll.nWarps      = 16;
    in.coll.algo        = "Ring";
    in.coll.proto       = "LL128";
    in.coll.parentGroup = (void*)0x2222;

    ncclProfilerEventDescr_v4_t out;
    ASSERT_TRUE(otel_compat::translateToV4(&in, &out));
    EXPECT_EQ(out.type, ncclProfileColl);
    EXPECT_EQ(out.parentObj, (void*)0x2222);  // the group, like NCCL's own v4 shim
    EXPECT_EQ(out.rank, 3);
    EXPECT_EQ(out.coll.seqNumber, 42u);
    EXPECT_STREQ(out.coll.func, "AllReduce");
    EXPECT_EQ(out.coll.count, 1024u);
    EXPECT_EQ(out.coll.root, 1);
    EXPECT_STREQ(out.coll.datatype, "ncclFloat32");
    EXPECT_EQ(out.coll.nChannels, 8);
    EXPECT_EQ(out.coll.nWarps, 16);
    EXPECT_STREQ(out.coll.algo, "Ring");
    EXPECT_STREQ(out.coll.proto, "LL128");
}

TYPED_TEST(TranslateTest, P2pKeepsChannelCount)
{
    // NCCL's v4 shim does not copy p2p.nChannels; the native path must.
    TypeParam in;
    memset(&in, 0, sizeof(in));
    in.type            = ncclProfileP2p;
    in.p2p.func        = "Send";
    in.p2p.count       = 256;
    in.p2p.datatype    = "ncclInt8";
    in.p2p.peer        = 5;
    in.p2p.nChannels   = 4;
    in.p2p.parentGroup = (void*)0x3333;

    ncclProfilerEventDescr_v4_t out;
    memset(&out, 0xAB, sizeof(out));  // garbage, like an uninitialized stack descriptor
    ASSERT_TRUE(otel_compat::translateToV4(&in, &out));
    EXPECT_EQ(out.p2p.nChannels, 4);
    EXPECT_EQ(out.p2p.peer, 5);
    EXPECT_EQ(out.p2p.count, 256u);
    EXPECT_EQ(out.parentObj, (void*)0x3333);
}

TYPED_TEST(TranslateTest, ProxyEventsAndKernelChannel)
{
    TypeParam in;
    memset(&in, 0, sizeof(in));
    in.type              = ncclProfileProxyOp;
    in.parentObj         = (void*)0x4444;
    in.proxyOp.pid       = 77;
    in.proxyOp.channelId = 2;
    in.proxyOp.peer      = 6;
    in.proxyOp.nSteps    = 9;
    in.proxyOp.chunkSize = 4096;
    in.proxyOp.isSend    = 1;

    ncclProfilerEventDescr_v4_t out;
    ASSERT_TRUE(otel_compat::translateToV4(&in, &out));
    EXPECT_EQ(out.parentObj, (void*)0x4444);  // ProxyOp keeps its Coll/P2p parent
    EXPECT_EQ(out.proxyOp.pid, 77);
    EXPECT_EQ(out.proxyOp.channelId, 2);
    EXPECT_EQ(out.proxyOp.peer, 6);
    EXPECT_EQ(out.proxyOp.nSteps, 9);
    EXPECT_EQ(out.proxyOp.chunkSize, 4096);
    EXPECT_EQ(out.proxyOp.isSend, 1);

    memset(&in, 0, sizeof(in));
    in.type           = ncclProfileProxyStep;
    in.proxyStep.step = 12;
    ASSERT_TRUE(otel_compat::translateToV4(&in, &out));
    EXPECT_EQ(out.proxyStep.step, 12);

    memset(&in, 0, sizeof(in));
    in.type               = ncclProfileKernelCh;
    in.kernelCh.channelId = 3;
    in.kernelCh.pTimer    = 123456789ull;
    ASSERT_TRUE(otel_compat::translateToV4(&in, &out));
    EXPECT_EQ(out.kernelCh.channelId, 3);
    EXPECT_EQ(out.kernelCh.pTimer, 123456789ull);
}

TYPED_TEST(TranslateTest, EventsUnknownToV4AreNotTranslated)
{
    TypeParam in;
    memset(&in, 0, sizeof(in));
    ncclProfilerEventDescr_v4_t out;
    for (uint64_t type : {(uint64_t)ncclProfileGroupApi, (uint64_t)ncclProfileCollApi, (uint64_t)ncclProfileP2pApi,
                          (uint64_t)ncclProfileKernelLaunch})
    {
        in.type = type;
        EXPECT_FALSE(otel_compat::translateToV4(&in, &out)) << "type 0x" << std::hex << type;
    }
}

TEST(TranslateStateArgs, CopiesTransferSizeForProxySteps)
{
    ncclProfilerEventStateArgs_v5_t in;
    memset(&in, 0, sizeof(in));
    in.proxyStep.transSize = 65536;
    ncclProfilerEventStateArgs_v4_t out;
    otel_compat::translateStateArgsToV4(ncclProfilerProxyStepSendWait, &in, &out);
    EXPECT_EQ(out.proxyStep.transSize, 65536u);

    memset(&in, 0, sizeof(in));
    in.kernelCh.pTimer = 99;
    otel_compat::translateStateArgsToV4(ncclProfilerKernelChStop, &in, &out);
    EXPECT_EQ(out.kernelCh.pTimer, 99u);
}

// ---------------------------------------------------------------------------
// Exported plugin structures
// ---------------------------------------------------------------------------

TEST(ExportedProfilerStructs, AllVersionsAreWired)
{
    EXPECT_STREQ(ncclProfiler_v4.name, "otel-profiler");
    EXPECT_STREQ(ncclProfiler_v5.name, "otel-profiler");
    EXPECT_EQ(ncclProfiler_v5.init, &profiler_otel_init_v5);
    EXPECT_EQ(ncclProfiler_v5.startEvent, &profiler_otel_start_event_v5);
    EXPECT_EQ(ncclProfiler_v5.stopEvent, &profiler_otel_stop_event_v4);
    EXPECT_EQ(ncclProfiler_v5.finalize, &profiler_otel_finalize_v4);
#if OTEL_HAS_PROFILER_V6
    EXPECT_STREQ(ncclProfiler_v6.name, "otel-profiler");
    EXPECT_EQ(ncclProfiler_v6.startEvent, &profiler_otel_start_event_v6);
#endif
#if OTEL_HAS_PROFILER_V7
    EXPECT_STREQ(ncclProfiler_v7.name, "otel-profiler");
    EXPECT_EQ(ncclProfiler_v7.init, &profiler_otel_init_v6);
    EXPECT_EQ(ncclProfiler_v7.startEvent, &profiler_otel_start_event_v7);
    EXPECT_EQ(ncclProfiler_v7.recordEventState, &profiler_otel_record_event_state_v7);
#endif
}

// ---------------------------------------------------------------------------
// End-to-end through the v5 entry points
// ---------------------------------------------------------------------------

class ProfilerV5Test : public ::testing::Test
{
protected:
    void* context = nullptr;
    int mask      = 0;

    void SetUp() override
    {
        resetProfilerState();
        ASSERT_EQ(profiler_otel_init_v5(&context, 0xC0FFEEull, &mask, "v5_comm", 1, 8, 2, quiet_logger),
                  ncclSuccess);
        ASSERT_NE(context, nullptr);
    }
    void TearDown() override
    {
        if (context) profiler_otel_finalize_v4(context);
        context = nullptr;
    }
};

TEST_F(ProfilerV5Test, InitPassesCommIdAsCommHash)
{
    auto* ctx = (struct eventContext*)context;
    EXPECT_EQ(ctx->commHash, 0xC0FFEEull);
    EXPECT_EQ(ctx->nranks, 8);
    EXPECT_EQ(ctx->rank, 2);
    EXPECT_EQ(mask, 0x1E);  // default v4 telemetry mask, no CE events in v5
}

TEST_F(ProfilerV5Test, P2pSendThroughV5HasRealChannelCount)
{
    ncclProfilerEventDescr_v5_t d;
    memset(&d, 0, sizeof(d));
    d.type            = ncclProfileP2p;
    d.p2p.func        = "Send";
    d.p2p.datatype    = "ncclFloat32";
    d.p2p.count       = 1000;
    d.p2p.peer        = 1;
    d.p2p.nChannels   = 2;
    d.p2p.parentGroup = nullptr;

    void* h = nullptr;
    ASSERT_EQ(profiler_otel_start_event_v5(context, &h, &d), ncclSuccess);
    ASSERT_NE(h, nullptr);
    auto* ev = (otelEventHandle_t*)h;
    EXPECT_EQ(ev->type, ncclProfileP2p);
    EXPECT_EQ(ev->p2p.nChannels, 2);
    EXPECT_EQ(ev->p2p.bytes, 4000u);
    EXPECT_EQ(profiler_otel_stop_event_v4(h), ncclSuccess);
}

TEST_F(ProfilerV5Test, ApiEventsGetNoHandleAndNoPendingOps)
{
    uint8_t idx     = ((struct eventContext*)context)->commState->get_active_buffer_idx();
    uint32_t before = inProgress(context, idx);
    for (uint64_t type : {(uint64_t)ncclProfileGroupApi, (uint64_t)ncclProfileCollApi, (uint64_t)ncclProfileP2pApi,
                          (uint64_t)ncclProfileKernelLaunch})
    {
        ncclProfilerEventDescr_v5_t d;
        memset(&d, 0, sizeof(d));
        d.type  = type;
        void* h = (void*)0x1;
        EXPECT_EQ(profiler_otel_start_event_v5(context, &h, &d), ncclSuccess);
        EXPECT_EQ(h, nullptr);
    }
    EXPECT_EQ(inProgress(context, idx), before);
}

TEST_F(ProfilerV5Test, ProxyStepTransferSizeFlowsThroughRecordEventState)
{
    // Coll -> send ProxyOp -> ProxyStep, all via v5.
    ncclProfilerEventDescr_v5_t coll;
    memset(&coll, 0, sizeof(coll));
    coll.type           = ncclProfileColl;
    coll.coll.func      = "AllGather";
    coll.coll.datatype  = "ncclInt8";
    coll.coll.count     = 4096;
    coll.coll.nChannels = 1;
    coll.coll.algo      = "Ring";
    coll.coll.proto     = "Simple";
    void* hc            = nullptr;
    ASSERT_EQ(profiler_otel_start_event_v5(context, &hc, &coll), ncclSuccess);
    ASSERT_NE(hc, nullptr);

    ncclProfilerEventDescr_v5_t op;
    memset(&op, 0, sizeof(op));
    op.type              = ncclProfileProxyOp;
    op.parentObj         = hc;
    op.proxyOp.pid       = getPid();
    op.proxyOp.isSend    = 1;
    op.proxyOp.peer      = 3;
    op.proxyOp.chunkSize = 4096;
    void* ho             = nullptr;
    ASSERT_EQ(profiler_otel_start_event_v5(context, &ho, &op), ncclSuccess);
    ASSERT_NE(ho, nullptr);

    ncclProfilerEventDescr_v5_t st;
    memset(&st, 0, sizeof(st));
    st.type           = ncclProfileProxyStep;
    st.parentObj      = ho;
    st.proxyStep.step = 0;
    void* hs          = nullptr;
    ASSERT_EQ(profiler_otel_start_event_v5(context, &hs, &st), ncclSuccess);
    ASSERT_NE(hs, nullptr);

    ncclProfilerEventStateArgs_v5_t args;
    memset(&args, 0, sizeof(args));
    args.proxyStep.transSize = 4096;
    ASSERT_EQ(profiler_otel_record_event_state_v5(hs, ncclProfilerProxyStepSendWait, &args), ncclSuccess);
    auto* step = (otelEventHandle_t*)hs;
    EXPECT_EQ(step->proxyStep.transSize, 4096u);
    EXPECT_TRUE(step->proxyStep.hasSendWait);

    // Null state args are accepted (NCCL passes NULL for some states).
    EXPECT_EQ(profiler_otel_record_event_state_v5(ho, ncclProfilerProxyOpInProgress_v4, nullptr), ncclSuccess);

    EXPECT_EQ(profiler_otel_stop_event_v4(hs), ncclSuccess);
    EXPECT_EQ(profiler_otel_stop_event_v4(ho), ncclSuccess);
    EXPECT_EQ(profiler_otel_stop_event_v4(hc), ncclSuccess);
}

// ---------------------------------------------------------------------------
// Copy-engine collectives (v6 / v7)
// ---------------------------------------------------------------------------

#if OTEL_HAS_PROFILER_V7

class ProfilerV7Test : public ::testing::Test
{
protected:
    void* context = nullptr;
    int mask      = 0;

    void SetUp() override
    {
        resetProfilerState();
        ASSERT_EQ(ncclProfiler_v7.init(&context, 0xABCDull, &mask, "v7_comm", 1, 4, 0, quiet_logger), ncclSuccess);
        ASSERT_NE(context, nullptr);
    }
    void TearDown() override
    {
        if (context) ncclProfiler_v7.finalize(context);
        context = nullptr;
    }

    static ncclProfilerEventDescr_v7_t ceColl(const char* func, size_t count, const char* dtype, const char* sync)
    {
        ncclProfilerEventDescr_v7_t d;
        memset(&d, 0, sizeof(d));
        d.type                = ncclProfileCeColl;
        d.ceColl.seqNumber    = 7;
        d.ceColl.func         = func;
        d.ceColl.count        = count;
        d.ceColl.datatype     = dtype;
        d.ceColl.syncStrategy = sync;
        d.ceColl.userTag      = 0;
        return d;
    }
};

TEST_F(ProfilerV7Test, DefaultMaskAddsCopyEngineCollectives)
{
    EXPECT_EQ(mask, 0x1E | ncclProfileCeColl);
    EXPECT_EQ(mask & ncclProfileCeSync, 0);
    EXPECT_EQ(mask & ncclProfileCeBatch, 0);
}

TEST_F(ProfilerV7Test, CeCollectiveIsRecordedAsCollective)
{
    ncclProfilerEventDescr_v7_t d = ceColl("AllGather", 1 << 20, "ncclBfloat16", "MC");
    void* h                       = nullptr;
    ASSERT_EQ(ncclProfiler_v7.startEvent(context, &h, &d), ncclSuccess);
    ASSERT_NE(h, nullptr);

    auto* ev = (otelEventHandle_t*)h;
    EXPECT_EQ(ev->type, ncclProfileColl);
    EXPECT_TRUE(ev->coll.isCopyEngine);
    EXPECT_STREQ(ev->coll.func, "AllGather");
    EXPECT_STREQ(ev->coll.algo, "CE");
    EXPECT_STREQ(ev->coll.proto, "MC");
    EXPECT_EQ(ev->coll.nChannels, 0);
    EXPECT_EQ(ev->coll.bytes, (size_t)(1 << 20) * 2);
    EXPECT_EQ(ncclProfiler_v7.stopEvent(h), ncclSuccess);
    EXPECT_GE(ev->endTs, ev->startTs);
}

TEST_F(ProfilerV7Test, CeCollectiveReleasesItsPendingOpOnStop)
{
    // A CE collective has no ProxyOps, so without releasing on stop the window could never finish closing.
    uint8_t idx     = ((struct eventContext*)context)->commState->get_active_buffer_idx();
    uint32_t before = inProgress(context, idx);

    ncclProfilerEventDescr_v7_t d = ceColl("AlltoAll", 4096, "ncclFloat32", "UC");
    void* h                       = nullptr;
    ASSERT_EQ(ncclProfiler_v7.startEvent(context, &h, &d), ncclSuccess);
    ASSERT_NE(h, nullptr);
    EXPECT_EQ(inProgress(context, idx), before + 1);

    EXPECT_EQ(ncclProfiler_v7.stopEvent(h), ncclSuccess);
    EXPECT_EQ(inProgress(context, idx), before);
}

TEST_F(ProfilerV7Test, CeSubEventsAndKernelPhasesAreSkipped)
{
    ncclProfilerEventDescr_v7_t parent = ceColl("Gather", 64, "ncclInt32", "UC");
    void* hp                           = nullptr;
    ASSERT_EQ(ncclProfiler_v7.startEvent(context, &hp, &parent), ncclSuccess);
    ASSERT_NE(hp, nullptr);

    for (uint64_t type : {(uint64_t)ncclProfileCeSync, (uint64_t)ncclProfileCeBatch,
                          (uint64_t)ncclProfileKernelPhase})
    {
        ncclProfilerEventDescr_v7_t d;
        memset(&d, 0, sizeof(d));
        d.type      = type;
        d.parentObj = hp;
        void* h     = (void*)0x1;
        EXPECT_EQ(ncclProfiler_v7.startEvent(context, &h, &d), ncclSuccess);
        EXPECT_EQ(h, nullptr) << "type 0x" << std::hex << type;
    }
    EXPECT_EQ(ncclProfiler_v7.stopEvent(hp), ncclSuccess);
}

TEST_F(ProfilerV7Test, CeCollectiveAggregatesUnderItsOwnKey)
{
    ncclProfilerEventDescr_v7_t d = ceColl("AllGather", 1024, "ncclFloat32", "UC");
    void* h                       = nullptr;
    ASSERT_EQ(ncclProfiler_v7.startEvent(context, &h, &d), ncclSuccess);
    ASSERT_NE(h, nullptr);
    auto* ev    = (otelEventHandle_t*)h;
    ev->startTs = 100.0;  // deterministic timing for the aggregation check
    ev->endTs   = 150.0;

    WindowAggregator agg(0);
    agg.addEvent(*ev);
    agg.finalize();
    const auto& colls = agg.getCollectives();
    ASSERT_EQ(colls.size(), 1u);
    auto it = colls.find("Comm43981_AllGather_CE_UC_0Chnl");  // 0xABCD
    ASSERT_NE(it, colls.end()) << colls.begin()->first;
    EXPECT_EQ(it->second.count, 1);
    EXPECT_EQ(it->second.totalBytes, 4096u);
    EXPECT_DOUBLE_EQ(it->second.totalTimeUs, 50.0);
    EXPECT_EQ(ncclProfiler_v7.stopEvent(h), ncclSuccess);
}

#endif  // OTEL_HAS_PROFILER_V7

#endif  // OTEL_HAS_PROFILER_V5
