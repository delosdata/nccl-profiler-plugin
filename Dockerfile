# SPDX-FileCopyrightText: 2025 Delos Data Inc
# SPDX-License-Identifier: LicenseRef-FSL-1.1-ALv2

# Add Ray, OpenTelemetry, and NCCL OTEL Profiler Plugin to the official vLLM OpenAI image

# To build:
#   docker build -t vllm/vllm-openai:profiler-otel -f Dockerfile .
# To build with specific NCCL tag:
#   docker build -t vllm/vllm-openai:profiler-otel --build-arg NCCL_TAG=<nccl-tag> -f Dockerfile .
# To run:
#   docker run -it --rm vllm/vllm-openai:profiler-otel ray ...

# Start from the official vLLM OpenAI image
FROM vllm/vllm-openai:v0.11.0

ARG NCCL_TAG=2.27.3-1
ARG NCCL_PATH=/tmp/nccl

# Set up environment variables (optional, but useful for reproducibility)
ENV PYTHONUNBUFFERED=1 \
    PIP_NO_CACHE_DIR=1

# --- Install Ray (Python) ---
    RUN pip install --upgrade pip && \
    pip install "ray[default]"

# --- Install deps for OTel C++ & plugin toolchain ---
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
      libssl-dev \
      libgrpc++-dev \
      protobuf-compiler-grpc \
      cmake \
      build-essential \
      git \
  && rm -rf /var/lib/apt/lists/*

# --- Build and install OpenTelemetry SDK ---
WORKDIR /tmp
RUN git clone https://github.com/open-telemetry/opentelemetry-cpp.git && \
    cd opentelemetry-cpp && \
    mkdir build && cd build && \
    cmake \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
      -DBUILD_SHARED_LIBS=ON \
      -DWITH_OTLP_GRPC=ON \
      -DWITH_OTLP_HTTP=ON \
      -DBUILD_TESTING=OFF \
      -DOPENTELEMETRY_INSTALL=ON \
      -DWITH_PROMETHEUS=ON \
      .. && \
    cmake --build . --parallel $(nproc) && \
    cmake --install . && \
    rm -rf /tmp/opentelemetry-cpp

# --- Remove pre-installed NCCL to ensure consistency ---
RUN apt-get update && \
DEBIAN_FRONTEND=noninteractive apt-get remove -y --allow-change-held-packages --purge libnccl2 libnccl-dev || true && \
apt-get autoremove -y && apt-get clean

# --- Build and install NCCL ---
ENV CUDA_HOME=/usr/local/cuda
WORKDIR /tmp
RUN git clone --branch v${NCCL_TAG} --depth 1 https://github.com/NVIDIA/nccl.git && \
    cd nccl && \
    make -j"$(nproc)" src.build NVCC_GENCODE+=-Wno-deprecated-gpu-targets && \
    mkdir -p /usr/local/include/nccl /usr/local/lib && \
    cp -av build/include/* /usr/local/include/nccl/ && \
    cp -av build/lib/libnccl.so* /usr/local/lib/ && \
    ldconfig

# --- Build and install NCCL Profiler OTEL Plugin  ---
COPY . /tmp/nccl-profiler-otel-plugin/
WORKDIR /tmp
RUN cd /tmp/nccl-profiler-otel-plugin && /bin/bash -lc 'make clean && make TRACE=1' \
    && \
    mkdir -p /usr/local/plugins/lib && \
    cp -v build/libnccl-profiler-otel.so /usr/local/plugins/lib && \
    (ldconfig || true) && \
    rm -rf /tmp/nccl-profiler-otel-plugin

# --- Clean up ---
RUN rm -rf ${NCCL_PATH}

# --- Set environment variables ---
ENV NCCL_VERSION=${NCCL_TAG}
#ENV LD_LIBRARY_PATH=/usr/local/cuda/lib64:/usr/local/lib:$LD_LIBRARY_PATH
#ENV LD_PRELOAD=/usr/local/lib/libnccl.so.2
#ENV VLLM_NCCL_SO_PATH=/usr/local/lib/libnccl.so.2
#ENV NCCL_NET_PLUGIN=/usr/local/plugins/lib/libnccl-net-reliable.so
ENV LD_LIBRARY_PATH=/usr/local/plugins/lib:$LD_LIBRARY_PATH
# 2.27.3-1 use the environment variable NCCL_PROFILER_PLUGIN differently...
ENV NCCL_PROFILER_PLUGIN=otel
ENV NV_LIBNCCL_DEV_PACKAGE=libnccl-dev=${NCCL_TAG}+cuda12.8
ENV NV_LIBNCCL_DEV_PACKAGE_VERSION=${NCCL_TAG}
ENV NV_LIBNCCL_DEV_PACKAGE_NAME=libnccl-dev
ENV NV_LIBNCCL_PACKAGE=libnccl2=${NCCL_TAG}+cuda12.8
ENV NV_LIBNCCL_PACKAGE_NAME=libnccl2
ENV NV_LIBNCCL_PACKAGE_VERSION=${NCCL_TAG}

# Reset working directory
WORKDIR /
ENTRYPOINT ["/bin/bash", "-c"]
