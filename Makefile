# SPDX-FileCopyrightText: 2025 Delos Data Inc
# SPDX-License-Identifier: LicenseRef-FSL-1.1-ALv2

# NCCL OTEL Profiler Plugin Makefile
# Uses CMake for actual building
# Supports both NVIDIA (CUDA) and AMD (ROCm) GPUs

# Default target
.PHONY: all
all: build

# Build directories
BUILD_DIR = build
TEST_BUILD_DIR = tests/build

# CMake configuration
CMAKE_FLAGS ?= -DCMAKE_BUILD_TYPE=Release

# GPU Platform selection: CUDA, ROCM, or AUTO (default: AUTO)
# AUTO will try to detect the available platform
GPU_PLATFORM ?= AUTO

# Allow `make TRACE=1` to toggle CMake TRACE option
TRACE ?= 0
ifeq ($(filter 1 ON on true TRUE yes YES,$(TRACE)),)
  TRACE_FLAG := OFF
else
  TRACE_FLAG := ON
endif

# ROCm path (default: /opt/rocm)
ROCM_PATH ?= /opt/rocm

# OpenTelemetry C++ SDK path (optional, for custom installations)
OTEL_PATH ?=

# Only pass ROCM_PATH to CMake when relevant, to avoid CMake warnings on CUDA builds.
# Treat ROCM_PATH as user-provided only when set via environment or command line.
ROCM_PATH_ORIGIN := $(origin ROCM_PATH)
ROCM_PATH_USER_SET := $(filter environment command line,$(ROCM_PATH_ORIGIN))
ifeq ($(GPU_PLATFORM),ROCM)
  ROCM_PATH_CMAKE_ARG := -DROCM_PATH=$(ROCM_PATH)
else ifneq ($(strip $(ROCM_PATH_USER_SET)),)
  ROCM_PATH_CMAKE_ARG := -DROCM_PATH=$(ROCM_PATH)
else
  ROCM_PATH_CMAKE_ARG :=
endif


# Configure and build using CMake
.PHONY: configure
configure: $(BUILD_DIR)/Makefile

$(BUILD_DIR)/Makefile: CMakeLists.txt
	@echo "Configuring NCCL OTEL Profiler Plugin..."
	@mkdir -p $(BUILD_DIR)
	@if [ -z "$(NCCL_PATH)" ]; then \
		echo "WARNING: NCCL_PATH is not set. Set NCCL_PATH to your NCCL/RCCL source/build tree (or source config.local)."; \
	fi
	@if [ -n "$(OTEL_PATH)" ]; then \
		cd $(BUILD_DIR) && cmake .. $(CMAKE_FLAGS) -DNCCL_PATH=$(NCCL_PATH) -DTRACE=$(TRACE_FLAG) -DGPU_PLATFORM=$(GPU_PLATFORM) $(ROCM_PATH_CMAKE_ARG) -DOTEL_PATH=$(OTEL_PATH); \
	else \
		cd $(BUILD_DIR) && cmake .. $(CMAKE_FLAGS) -DNCCL_PATH=$(NCCL_PATH) -DTRACE=$(TRACE_FLAG) -DGPU_PLATFORM=$(GPU_PLATFORM) $(ROCM_PATH_CMAKE_ARG); \
	fi

.PHONY: build
build: configure
	@echo "Building NCCL OTEL Profiler Plugin..."
	@$(MAKE) -C $(BUILD_DIR) -j$(shell nproc)
	@echo "Build complete: $(BUILD_DIR)/libnccl-profiler-otel.so"

.PHONY: clean
clean:
	@echo "Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR)
	@rm -rf $(TEST_BUILD_DIR)
	@echo "Clean complete"

# Install the library
.PHONY: install
install: build
	@echo "Installing NCCL OTEL Profiler Plugin..."
	@$(MAKE) -C $(BUILD_DIR) install

# Test targets
.PHONY: test-configure
test-configure: $(TEST_BUILD_DIR)/Makefile

$(TEST_BUILD_DIR)/Makefile: tests/CMakeLists.txt
	@echo "Configuring NCCL OTEL Profiler Plugin Tests..."
	@mkdir -p $(TEST_BUILD_DIR)
	@if [ -z "$(NCCL_PATH)" ]; then \
		echo "WARNING: NCCL_PATH is not set. Tests may not compile correctly (or source config.local)."; \
	fi
	@if [ -n "$(NCCL_PATH)" ]; then \
		cd $(TEST_BUILD_DIR) && cmake .. -DNCCL_PATH=$(NCCL_PATH) -DTRACE=$(TRACE_FLAG) -DGPU_PLATFORM=$(GPU_PLATFORM) $(ROCM_PATH_CMAKE_ARG); \
	else \
		cd $(TEST_BUILD_DIR) && cmake .. -DTRACE=$(TRACE_FLAG) -DGPU_PLATFORM=$(GPU_PLATFORM) $(ROCM_PATH_CMAKE_ARG); \
	fi

.PHONY: test-build
test-build: test-configure
	@echo "Building NCCL OTEL Profiler Plugin Tests..."
	@$(MAKE) -C $(TEST_BUILD_DIR) -j$(shell nproc)
	@echo "Test build complete: $(TEST_BUILD_DIR)/nccl_profiler_otel_tests"

.PHONY: test
test: test-build
	@echo "Running NCCL OTEL Profiler Plugin Tests..."
	@cd $(TEST_BUILD_DIR) && ./nccl_profiler_otel_tests
	@echo "Tests complete."

.PHONY: test-verbose
test-verbose: test-build
	@echo "Running NCCL OTEL Profiler Plugin Tests (verbose)..."
	@$(TEST_BUILD_DIR)/nccl_profiler_otel_tests
	@echo "Tests complete"

# Help target
.PHONY: help
help:
	@echo "NCCL OTEL Profiler Plugin Build System"
	@echo ""
	@echo "Supports both NVIDIA (CUDA) and AMD (ROCm) GPUs"
	@echo ""
	@echo "Targets:"
	@echo "  all            - Build the plugin (default)"
	@echo "  build          - Build the plugin"
	@echo "  configure      - Configure CMake build"
	@echo "  test           - Build and run unit tests"
	@echo "  test-build     - Build unit tests only"
	@echo "  clean          - Clean build artifacts (including tests)"
	@echo "  install        - Install the library"
	@echo "  help           - Show this help"
	@echo ""
	@echo "Variables:"
	@echo "  NCCL_PATH      - Path to NCCL/RCCL installation (default: $(NCCL_PATH))"
	@echo "  GPU_PLATFORM   - GPU platform: CUDA, ROCM, or AUTO (default: $(GPU_PLATFORM))"
	@echo "  ROCM_PATH      - Path to ROCm installation (default: $(ROCM_PATH))"
	@echo "  OTEL_PATH      - Path to OpenTelemetry C++ SDK installation (optional)"
	@echo "  CMAKE_FLAGS    - Additional CMake flags (default: $(CMAKE_FLAGS))"
	@echo "  TRACE          - Enable trace logging (default: $(TRACE), 0=off, 1=on)"
	@echo ""
	@echo "Examples:"
	@echo "  make                                      # Build with auto-detected platform"
	@echo "  make NCCL_PATH=/path/to/nccl              # Build for NVIDIA with custom NCCL path"
	@echo "  make GPU_PLATFORM=ROCM NCCL_PATH=/opt/rocm/rccl  # Build for AMD ROCm"
	@echo "  make GPU_PLATFORM=CUDA                    # Force CUDA platform"
	@echo "  make OTEL_PATH=/opt/opentelemetry-cpp    # Use custom OpenTelemetry installation"
	@echo "  make CMAKE_FLAGS=-DCMAKE_BUILD_TYPE=Debug # Debug build"
	@echo "  make test                                 # Build and run unit tests"
	@echo "  make clean                                # Clean all build artifacts"
