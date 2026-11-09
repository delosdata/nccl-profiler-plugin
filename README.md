# NCCL Profiler OTEL Plugin

The NCCL Profiler OTEL Plugin is a profiling extension for NVIDIA's NCCL (NVIDIA Collective Communications Library) that collects and exports performance metrics to OpenTelemetry collectors. This plugin captures collective operations, P2P operations, and proxy operation transfer rates with detailed performance data for monitoring and analysis.

## Overview

This plugin implements the NCCL Profiler Plugin interfaces v4 through v7 (v5-v7 are compiled in when the NCCL headers provide them; NCCL uses the newest version both sides support) and provides:

- **Real-time Metrics Collection**: Captures NCCL collective, P2P, and proxy operation metrics
- **OpenTelemetry Integration**: Exports metrics to OpenTelemetry collectors via HTTP
- **Lock-free Event Recording**: Zero-allocation critical path with pre-allocated circular buffers
- **Configurable Telemetry**: Supports customizable reporting intervals and batch timeouts
- **Event Filtering**: Configurable event mask to control which NCCL events are profiled
- **Multi-threaded Support**: Thread-safe operation across multiple NCCL contexts
- **Linear Regression Analysis**: Automatic latency and bandwidth calculation from transfer data

The plugin uses a lock-free circular buffer design with pre-allocated memory to ensure minimal overhead on the critical path. Events are processed asynchronously by a background telemetry thread that aggregates metrics and exports them to OpenTelemetry collectors.

## Features

- **Comprehensive Event Tracking**: Monitors collective operations, P2P operations, and proxy operations
- **Histogram Metrics**: Provides detailed metrics for bytes, time, latency, and transfer rates
- **Rank Transfer Metrics**: Tracks data transfer between ranks with latency and bandwidth analysis
- **Channel Transfer Metrics**: Per-channel transfer statistics for granular performance analysis
- **Lock-free Critical Path**: Pre-allocated circular buffers ensure zero memory allocation during event recording
- **Asynchronous Processing**: Background telemetry thread processes and exports metrics without blocking NCCL operations
- **Windowed Aggregation**: Events are aggregated in windows (50k events or the configured time interval) before export
- **Linear Regression Metrics**: Automatic calculation of latency (intercept) and rate (slope) from transfer data
- **Debug Logging**: Optional trace logging for troubleshooting

## Building the Plugin

### Prerequisites

- **NCCL**: NVIDIA NCCL library (version 2.27.3-1 or compatible)
- **CUDA**: NVIDIA CUDA toolkit
- **OpenTelemetry C++ SDK**: Required for telemetry functionality
- **Build Tools**: CMake (3.15+), GCC/G++, Make
- **Dependencies**: libssl-dev, protobuf-compiler, curl

### Build

#### Using Make

The project includes a Makefile that wraps CMake for easier building:

```bash
# Basic build
make

# Build with debug trace logging enabled
make TRACE=1

# Build with custom NCCL path
make NCCL_PATH=/path/to/nccl

# build with custom OTEL path
make OTEL_PATH=/path/to/otel-cpp

# Clean build artifacts
make clean

# Install the plugin
make install
```

### Build Output

The build process creates:
- `build/libnccl-profiler-otel.so` - The main plugin shared library
- `build/libnccl-profiler-otel.so.1` - Versioned symlink
- `build/libnccl-profiler-otel.so.1.0.0` - Full versioned library

## OpenTelemetry C++ SDK Installation

The plugin requires the OpenTelemetry C++ SDK. Install it as follows:

1. Clone the C++ SDK from GitHub:
   ```bash
   git clone https://github.com/open-telemetry/opentelemetry-cpp.git
   ```

2. Change to the OpenTelemetry C++ SDK directory:
   ```bash
   cd opentelemetry-cpp
   ```

3. Create a build directory and navigate into it:
   ```bash
   mkdir build && cd build
   ```

4. Configure CMake with required options:
   ```bash
   cmake \
     -DCMAKE_INSTALL_PREFIX=/path/to/install/otel-cpp
     -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
     -DBUILD_SHARED_LIBS=ON \
     -DWITH_OTLP_GRPC=ON \
     -DWITH_OTLP_HTTP=ON \
     -DBUILD_TESTING=OFF \
     -DOPENTELEMETRY_INSTALL=ON \
     -DWITH_PROMETHEUS=ON \
     ..
   ```

5. Build the SDK:
   ```bash
   cmake --build . -j$(nproc)
   ```

6. Install OpenTelemetry C++ into the `otel-cpp` directory:
   ```bash
   cmake --install .
   ```

## Prometheus + Grafana dashboards (LGTM) + Pipeline Analyzer (Docker)

The `otel/` directory contains a ready-to-run local observability stack:

- **LGTM stack** via `grafana/otel-lgtm` (Grafana + OTel Collector + Prometheus)
- **Pipeline Analyzer**: a small HTTP service that reads Prometheus metrics and helps analyze pipeline traffic

### Start the stack

From the repo root:

```bash
cd otel

# First time (or after changing pipeline-analyzer image requirements)
docker compose build

# Start the stack
docker compose up
```

### What you get

- **Grafana UI**: `http://localhost:3000`
  - Dashboards are auto-provisioned from `otel/dashboards/` (see `otel/dashboards/dashboards.yml`)
- **OTLP endpoints** (for the profiler plugin):
  - gRPC: `localhost:4317`
  - HTTP: `localhost:4318` (plugin default)
- **Prometheus (debug)**:
  - Metrics endpoint: `http://localhost:8889/metrics`
  - Web UI: `http://localhost:9090`
- **Pipeline Analyzer API**: `http://localhost:55001`

### Point the plugin at the local collector

By default the plugin exports OTLP/HTTP to `http://localhost:4318`, which matches the compose stack.
If you need to set it explicitly:

```bash
export NCCL_PROFILER_OTEL_TELEMETRY_ENDPOINT=http://localhost:4318
```

## Environment Variables

The plugin uses several environment variables for configuration. All variables use the `NCCL_` prefix to follow NCCL conventions.

### Required Environment Variables

#### `NCCL_PROFILER_PLUGIN`
- **Description**: Specifies which profiler plugin to use
- **Value**: `otel`
- **Example**: `export NCCL_PROFILER_PLUGIN=otel`

### Optional Environment Variables

#### Telemetry Endpoint Configuration
- **`NCCL_PROFILER_OTEL_TELEMETRY_ENDPOINT`** (default: `http://localhost:4318`)
  - OpenTelemetry collector base URL (the plugin automatically appends `/v1/metrics`)
  - Format: `http://host:port` or `https://host:port`
  - Example: `export NCCL_PROFILER_OTEL_TELEMETRY_ENDPOINT=http://otel-collector:4318`

#### Plugin Control
- **`NCCL_PROFILER_OTEL_ENABLE`** (default: `1`)
  - Enable/disable the plugin entirely
  - Set to `0` to disable the plugin
  - Example: `export NCCL_PROFILER_OTEL_ENABLE=1`

#### Telemetry Configuration
- **`NCCL_PROFILER_OTEL_TELEMETRY_ENABLE`** (default: `1`)
  - Enable/disable telemetry collection
  - Set to `0` to disable telemetry while keeping plugin active
  - Example: `export NCCL_PROFILER_OTEL_TELEMETRY_ENABLE=1`

- **`NCCL_PROFILER_OTEL_TELEMETRY_INTERVAL_SEC`** (default: `5`)
  - Reporting interval in seconds
  - Controls how often metrics are exported to the collector
  - Example: `export NCCL_PROFILER_OTEL_TELEMETRY_INTERVAL_SEC=10`

- **`NCCL_PROFILER_OTEL_TELEMETRY_BATCH_TIMEOUT_MS`** (default: `3000`)
  - Batch timeout in milliseconds
  - Maximum time to wait before sending a batch of metrics
  - Example: `export NCCL_PROFILER_OTEL_TELEMETRY_BATCH_TIMEOUT_MS=5000`

#### Linear Regression Configuration
- **`NCCL_PROFILER_LINEAR_REGRESSION_MODE`** (default: `MIN`)
  - Controls how latency/rate are estimated from transfer size/time samples
  - Supported values:
    - `MIN`: for each transfer size, keep the minimum time observed (more robust to jitter)
    - `AVG`: use all points as observed
  - Example: `export NCCL_PROFILER_LINEAR_REGRESSION_MODE=MIN`

#### Event Filtering
- **`NCCL_PROFILE_EVENT_MASK`** (auto-configured to `0x1E` if not set)
  - Bitmask to control which NCCL events are profiled
  - Available event types:
    - `0x02`: Collective operations (ncclProfileColl)
    - `0x04`: Point-to-point operations (ncclProfileP2p)
    - `0x08`: Proxy operations (ncclProfileProxyOp)
    - `0x10`: Proxy step operations (ncclProfileProxyStep)
    - `0x1000`: Copy-engine collectives (ncclProfileCeColl, profiler v6+)
  - Default auto-configuration: `0x1E` (Coll + P2P + ProxyOp + ProxyStep)
  - Example: `export NCCL_PROFILE_EVENT_MASK=0x1E`
  - Note: The plugin automatically configures this mask unless explicitly set. `ncclProfileProxyCtrl` (0x20) is not tracked as it doesn't contribute to metrics. `ncclProfileGroup` (0x01) is supported (a handle is returned for correct parent-chain behavior) but it is not exported as a metric. When NCCL loads the plugin through profiler v6 or v7, the default mask also includes `ncclProfileCeColl` (0x1000), i.e. `0x101E`.

#### Library Path Configuration
- **`LD_LIBRARY_PATH`**
  - Add the plugin directory to the library search path
  - Example: `export LD_LIBRARY_PATH=/usr/local/plugins/lib:$LD_LIBRARY_PATH`

## Usage Examples

### Basic Usage

1. **Set up environment variables**:
   ```bash
   export NCCL_PROFILER_PLUGIN=otel
   # Optional: customize endpoint (defaults to http://localhost:4318)
   # export NCCL_PROFILER_OTEL_TELEMETRY_ENDPOINT=http://otel-collector:4318
   export LD_LIBRARY_PATH=/usr/local/plugins/lib:$LD_LIBRARY_PATH
   ```

2. **Run your NCCL application**:
   ```bash
   ./your_nccl_application
   ```

### Advanced Configuration

**High-frequency monitoring**:
```bash
export NCCL_PROFILER_PLUGIN=otel
export NCCL_PROFILER_OTEL_TELEMETRY_ENDPOINT=http://otel-collector:4318
export NCCL_PROFILER_OTEL_TELEMETRY_INTERVAL_SEC=1
export LD_LIBRARY_PATH=/usr/local/plugins/lib:$LD_LIBRARY_PATH
```

**Debug mode with trace logging**:
```bash
# Build with trace logging enabled
make TRACE=1

# Set environment variables
export NCCL_PROFILER_PLUGIN=otel
export NCCL_PROFILER_OTEL_TELEMETRY_ENDPOINT=http://localhost:4318
export NCCL_PROFILER_OTEL_ENABLE=1
export NCCL_DEBUG=INFO
export LD_LIBRARY_PATH=/usr/local/plugins/lib:$LD_LIBRARY_PATH
```

## Codebase Structure

The plugin is organized into several key components:

### Core Plugin Interface (`profiler_otel.h` / `profiler_otel.cc`)
- **Purpose**: Implements the NCCL Profiler Plugin v4 interface, plus v5/v6/v7 entry points that translate newer event descriptors (`profiler_compat.h`) into the same code path
- **Key Functions**:
  - `profiler_otel_init_v4()`: Initialize plugin for a communicator
  - `profiler_otel_start_event_v4()`: Handle event start (Coll, P2P, ProxyOp, ProxyStep, Group)
  - `profiler_otel_stop_event_v4()`: Handle event stop
  - `profiler_otel_record_event_state_v4()`: Record ProxyStep state transitions (SendWait, etc.)
  - `profiler_otel_finalize_v4()`: Cleanup plugin for a communicator
- **Event Filtering**: Skips ProxyCtrl, receive-side ProxyOps/ProxySteps, and P2P Recv events. Group events return a valid handle for correct parent-chain behavior, but they are not exported as metrics.

### Circular Buffer Management (`communicator_state.h` / `communicator_state.cc`)
- **Purpose**: Lock-free circular buffer system for event storage
- **Key Components**:
  - `CommunicatorState`: Manages 4 circular buffers per communicator
  - `WindowMetadata`: Tracks window state, element count, and in-progress operations
  - Window state machine: FILLING → CLOSING → PROCESSING → READY
- **Features**:
  - Lock-free allocation using atomic operations
  - Parent-aware routing (events route to parent's window if parent is CLOSING)
  - Time-based and count-based window triggers
  - In-progress operation tracking for window boundary handling

### Event Aggregation (`aggregation.h` / `aggregation.cc`)
- **Purpose**: Aggregate events within a window and calculate metrics
- **Key Components**:
  - `WindowAggregator`: Processes a single window of events
  - `AggregatedCollective`: Aggregated collective operation statistics
  - `AggregatedP2P`: Aggregated P2P operation statistics
  - `AggregatedTransfer`: Aggregated transfer statistics (rank-to-rank and per-channel)
- **Processing Phases**:
  1. Track Coll/P2P operations
  2. Aggregate ProxyStep transfers to ProxyOps
  3. Link ProxyOps to parent Collectives/P2Ps
  4. Calculate durations and prepare metrics for export

### Telemetry Export (`telemetry.h` / `telemetry.cc`)
- **Purpose**: Export metrics to OpenTelemetry collectors
- **Key Components**:
  - Background telemetry thread for asynchronous processing
  - OpenTelemetry metrics API integration (histograms, counters)
  - Metric export functions for Collectives, P2P, rank transfers, and channel transfers
- **Metrics Exported**:
  - Collective metrics: bytes, time, transfer counts, transfer sizes/times
  - P2P metrics: bytes, time, transfer counts, transfer sizes/times
  - Rank transfer metrics: total bytes, latency (from linear regression), rate (from linear regression)
  - Channel transfer metrics: average transfer size, average transfer time, latency

### Linear Regression (`linear_regression.h` / `linear_regression.cc`)
- **Purpose**: Calculate latency and transfer rate from transfer data
- **Key Features**:
  - Two modes: AVG (use all points) and MIN (use minimum time per size)
  - Calculates slope (rate in bytes/us) and intercept (latency in us)
  - R-squared calculation for goodness of fit
  - Supports merging data from multiple instances

### Event Structures (`events.h`)
- **Purpose**: Define event handle structure for circular buffer storage
- **Key Structures**:
  - `otelEventHandle_t`: Lightweight event handle with union for type-specific data
  - `eventContext`: Plugin context per communicator
- **Event Types Supported**:
  - `ncclProfileGroup`: Group events (handle returned; used for parent-chain/window management; not exported as metrics)
  - `ncclProfileColl`: Collective operations
  - `ncclProfileP2p`: Point-to-point operations
  - `ncclProfileProxyOp`: Proxy operations (per channel)
  - `ncclProfileProxyStep`: Individual transfer steps

### Plugin Registration (`nccl_plugin.cc`)
- **Purpose**: Register plugin with NCCL
- **Structure**: `ncclProfiler_v4` structure exported to NCCL, plus `ncclProfiler_v5`, `ncclProfiler_v6` and `ncclProfiler_v7` when built against NCCL headers that define them (NCCL 2.28+ for v5, 2.31+ for v7)

### NCCL Profiler Interface Versions (`profiler_compat.h`)
- NCCL looks up `ncclProfiler_v7` first and falls back to older versions, so one build works with NCCL 2.27 (v4) and newer releases.
- Events that exist in v4 (Group, Coll, P2p, ProxyOp, ProxyStep, ProxyCtrl, KernelCh, NetPlugin) are translated field by field and handled exactly as in v4. Like NCCL's own v4 shim, Coll/P2p events take their parent from `parentGroup`.
- Why native support matters on newer NCCL: NCCL's v4 compatibility shim does not copy `p2p.nChannels` (the v4 descriptor is built on the stack), so a v4-only plugin sees an indeterminate channel count on P2P events; and the shim drops copy-engine collectives entirely.
- Copy-engine (CE) collectives (v6+, used by NCCL's zero-SM AllGather/AlltoAll/Scatter/Gather paths) are recorded as collectives with algo `CE` and the sync strategy (`MC` multicast or `UC` unicast) in the protocol slot, e.g. `Comm<hash>_AllGather_CE_UC_0Chnl`. They have no ProxyOps, so their duration uses the same Coll start to stop timing as the existing internal-links path, and their pending-op slot is released when the event stops. CE sync/batch sub-events, API events (GroupApi/CollApi/P2pApi/KernelLaunch) and v7 kernel phase events are not aggregated and get no handle.

### Configuration (`param.h`)
- **Purpose**: Environment variable parameter loading
- **Features**: Thread-safe parameter caching with atomic operations

## Architecture

### Overview

The NCCL Profiler Plugin tracks NCCL collective and P2P operations through their complete lifecycle, from the high-level API call to the actual data transfers. It uses NCCL's event hierarchy to correlate operations:

```
NCCL API Call (Coll/P2P)
  └── Collective/P2P Event (START) ← Enqueues work
       │
       ├── Collective/P2P Event (STOP) ← Work enqueued (happens quickly)
       │
       ├── ProxyOp (send, per channel) ← Links back to Coll/P2P via parentObj
       │    │
       │    ├── ProxyStep (multiple per ProxyOp)
       │    │    ├── RecordEventState(ProxyStepSendWait) ← Actual transfer start + size
       │    │    └── ProxyStep STOP ← Transfer complete
       │    │
       │    └── ProxyOp STOP ← All steps for this channel complete
       │
       └── [Timing: Coll/P2P START → Last ProxyOp STOP = Total collective time]
```

### Event Flow and Timing

**Key Insight**: NCCL's collective operations are asynchronous. The `Coll/P2P START` and `STOP` events only reflect work enqueuing, not actual data transfer.

1. **Collective/P2P Start**: NCCL API called, work begins enqueuing
2. **Collective/P2P Stop**: Work enqueued (happens in microseconds)
3. **ProxyOp Start** (per channel): Proxy thread begins processing channel operations
   - `parentObj` points back to the parent `Coll/P2P` event
   - Only send-side `ProxyOp` events are tracked (receive-side filtered out)
4. **ProxyStep Start**: Individual transfer step begins
   - `parentObj` points to the parent `ProxyOp`
5. **RecordEventState(ProxyStepSendWait)**: Actual transfer starts
   - Provides the **real transfer size** (not the buffer size)
   - Timestamp marks the **start of actual data transfer**
6. **ProxyStep Stop**: Transfer complete for this step
   - Transfer time = `ProxyStep Stop - SendWait timestamp`
7. **ProxyOp Stop**: All steps for this channel complete
8. **Total Collective Time** = `Coll START → Last ProxyOp STOP`

### Circular Buffer Design

The plugin uses a lock-free circular buffer design with 4 pre-allocated buffers per communicator:
- Each buffer holds 100,000 events
- Windows are triggered when:
  - **Count-based**: 50,000 events collected, OR
  - **Time-based**: Configured interval elapsed (default 5 seconds, configurable via `NCCL_PROFILER_OTEL_TELEMETRY_INTERVAL_SEC`)
- Event recording on the critical path involves only:
  - Atomic increment of buffer index
  - Writing to pre-allocated buffer slot
  - No memory allocation or locks

### Window Management and State Machine

Each window transitions through these states:

1. **FILLING**: Actively collecting events
   - Events written to buffer via atomic operations
   - Count and time triggers checked on each event
   - When trigger reached: transition to CLOSING

2. **CLOSING**: Waiting for in-progress operations to complete
   - New events directed to next buffer
   - `in_progress_count` tracks operations that span window boundary
   - When `in_progress_count` reaches 0: transition to PROCESSING

3. **PROCESSING**: Telemetry thread processes events
   - Aggregates events by type
   - Links events via `parentObj` hierarchy
   - Calculates metrics and exports to OpenTelemetry
   - When processing complete: transition to READY

4. **READY**: Window cleared and available for reuse

**Window Boundary Handling**: The plugin ensures that related events (a `Coll/P2P` and its `ProxyOp`/`ProxyStep` children) stay in the same window by:
- Tracking `in_progress_count` for each window
- Incrementing on `Coll/P2P START` (by `nChannels` for expected ProxyOps)
- Decrementing on `ProxyOp STOP`
- Delaying buffer switch until `in_progress_count == 0`

### Aggregation and Metrics

The telemetry thread processes windows in multiple phases:

**Phase 1: Track Collective/P2P Operations**
- Stores `Coll/P2P` START events with expected number of `ProxyOps`
- Maps event handle pointer to operation metadata

**Phase 2: Aggregate ProxyStep Transfers**
- Groups `ProxyStep` transfers by parent `ProxyOp` (via `parentObj`)
- Only processes `ProxySteps` with `SendWait` state (actual transfers)
- Calculates transfer time = `ProxyStep STOP - SendWait timestamp`
- Uses transfer size from `SendWait` state (not buffer size)

**Phase 3: Link ProxyOps to Collectives**
- Matches `ProxyOp` to parent `Coll/P2P` (via `parentObj`)
- Aggregates all `ProxyStep` transfers to parent collective
- Updates collective duration with latest `ProxyOp STOP` time
- Calculates: `Total Collective Time = START → Last ProxyOp STOP`

**Phase 4: Export Metrics**
- Collective/P2P metrics: average bytes, time, transfer counts
- Rank-to-rank metrics: total bytes, latency, rate (via linear regression)
- Per-channel metrics: average transfer size and time

### Metric Types Exported

1. **Collective Metrics**:
   - Average bytes per collective operation
   - Average time per collective operation (START → Last ProxyOp STOP)
   - Average number of transfers (ProxySteps) per collective
   - Average transfer size and time

2. **P2P Metrics**:
   - Average bytes per P2P operation
   - Average time per P2P operation
   - Average number of transfers per P2P operation

3. **Rank Transfer Metrics**:
   - Total bytes transferred between ranks
   - Latency (from linear regression intercept)
   - Transfer rate in MB/s (from linear regression slope)
   - **Note**: Keys include communicator hash prefix (e.g., `Comm<hash>_RankXToRankY`) to avoid collisions in pipeline parallelism scenarios

4. **Channel Transfer Metrics**:
   - Average transfer size per channel
   - Average transfer time per channel

## Troubleshooting

### Common Issues

1. **Plugin not loading**:
   - Verify `NCCL_PROFILER_PLUGIN=otel` is set
   - Check that `LD_LIBRARY_PATH` includes the plugin directory
   - Ensure the plugin library exists and is executable

2. **No metrics being sent**:
   - Verify `NCCL_PROFILER_OTEL_TELEMETRY_ENDPOINT` is correctly set
   - Check that `NCCL_PROFILER_OTEL_ENABLE=1` (default)
   - Check that `NCCL_PROFILER_OTEL_TELEMETRY_ENABLE=1` (default)
   - Verify OpenTelemetry collector is running and accessible

3. **Connection issues**:
   - Verify the OpenTelemetry collector is running and accessible
   - Check network connectivity to the collector endpoint
   - Ensure the endpoint URL format is correct (`http://host:port`)

4. **Buffer overflow warnings**:
   - Increase window trigger settings if events are generated too quickly
   - Check that telemetry thread is processing windows in time
   - Verify system has sufficient CPU for background processing

### Debug Logging

Enable trace logging for detailed debugging:

```bash
# Build with trace logging
make TRACE=1

# Run with NCCL debug output
export NCCL_DEBUG=INFO
export NCCL_PROFILER_PLUGIN=otel
# ... other environment variables
```

This will provide detailed logs about plugin initialization, metric collection, and telemetry export.

## Code Documentation

All functions in the codebase are documented with Doxygen-style comments, including:
- Function descriptions
- Parameter documentation (`@param[in]`, `@param[out]`)
- Return value documentation (`@return`)
- Notes about thread safety, usage, and implementation details

To generate documentation, use Doxygen:
```bash
doxygen Doxyfile  # If Doxyfile exists
# Or use default Doxygen configuration
doxygen -g Doxyfile
doxygen Doxyfile
```

## Testing

The plugin includes unit tests in the `tests/` directory:
- `test_aggregation.cc`: Tests for event aggregation logic
- `test_communicator_state.cc`: Tests for circular buffer management
- `test_linear_regression.cc`: Tests for linear regression calculations
- `test_profiler_otel.cc`: Tests for plugin interface functions
- `test_profiler_events.cc`: Tests for event handling
- `test_edge_cases.cc`: Tests for edge cases and error handling
- `test_race_conditions.cc`: Tests for thread safety

Run tests with:
```bash
make test
```

## Contributing

When adding new features or modifying existing code:
1. Add Doxygen documentation headers to all new functions
2. Update this README if architecture or usage changes
3. Add unit tests for new functionality
4. Ensure thread safety for lock-free operations
5. Follow the existing code style and conventions
