# SPDX-FileCopyrightText: 2025 Delos Data Inc
# SPDX-License-Identifier: LicenseRef-FSL-1.1-ALv2

FROM python:3.11-slim

# Set working directory
WORKDIR /app

# Install system dependencies that might be needed
# Note: nvidia-smi and rocm-smi should be available on the host
# and accessible via bind mounts or host network
RUN apt-get update && apt-get install -y --no-install-recommends \
    && rm -rf /var/lib/apt/lists/*

# Upgrade pip to ensure we can install packages
RUN pip install --upgrade pip

# Copy requirements and install Python dependencies
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# Verify prometheus_client is installed
RUN python3 -c "import prometheus_client; print('prometheus_client installed successfully')"

# Copy the exporter script
COPY gpu_pcie_exporter.py .

# Make the script executable
RUN chmod +x gpu_pcie_exporter.py

# Expose the default port
EXPOSE 8000

# Run the exporter
CMD ["python3", "gpu_pcie_exporter.py"]
