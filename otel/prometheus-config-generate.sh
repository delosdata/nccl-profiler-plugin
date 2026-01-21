#!/bin/bash
# SPDX-FileCopyrightText: 2025 Delos Data Inc
# SPDX-License-Identifier: LicenseRef-FSL-1.1-ALv2

#
# Generate Prometheus configuration with dynamic host scrape configs
#
# Usage: ./prometheus-config-generate.sh [OPTIONS]
#
# Options:
#   -b, --base-config FILE     Base prometheus config file (default: prometheus.yaml)
#   -c, --generate-config FILE Generate configuration file (default: prometheus-config-generate.yaml)
#   -o, --output FILE          Output file (default: prometheus-generated.yaml)
#   --help                     Show this help message
#
# Generation configuration file format (YAML):
#   hosts:
#     - name: host_a
#       address: 100.200.300.400
#       # Optional port overrides:
#       # gpu_exporter_port: 5053
#       # gpu_pcie_port: 5052
#       # node_exporter_port: 9100
#       # process_exporter_port: 9256

set -e

# Default values
BASE_CONFIG="prometheus.yaml"
GENERATE_CONFIG="prometheus-config-generate.yaml"
OUTPUT_FILE="prometheus-generated.yaml"

# Default ports
DEFAULT_GPU_EXPORTER_PORT=5053
DEFAULT_GPU_PCIE_PORT=5052
DEFAULT_NODE_EXPORTER_PORT=9100
DEFAULT_PROCESS_EXPORTER_PORT=9256

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -b|--base-config)
            BASE_CONFIG="$2"
            shift 2
            ;;
        -c|--generate-config)
            GENERATE_CONFIG="$2"
            shift 2
            ;;
        -o|--output)
            OUTPUT_FILE="$2"
            shift 2
            ;;
        --help)
            head -21 "$0" | tail -19
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Check if required files exist
if [[ ! -f "$BASE_CONFIG" ]]; then
    echo "Error: Base config file '$BASE_CONFIG' not found"
    exit 1
fi

if [[ ! -f "$GENERATE_CONFIG" ]]; then
    echo "Error: Hosts config file '$GENERATE_CONFIG' not found"
    exit 1
fi

# Get the base config content (everything up to but not including rule_files)
BASE_CONTENT=$(sed '/^rule_files:/,$d' "$BASE_CONFIG")

# Get rule_files section if it exists
RULE_FILES_SECTION=$(sed -n '/^rule_files:/,$p' "$BASE_CONFIG")

# Start building the output
echo "$BASE_CONTENT" > "$OUTPUT_FILE"

# Function to generate scrape config for a single exporter
generate_scrape_config() {
    local job_prefix="$1"
    local host_name="$2"
    local address="$3"
    local port="$4"

    cat << EOF

  - job_name: '${job_prefix}-${host_name}'
    static_configs:
      - targets: ['${address}:${port}']
    metric_relabel_configs:
      - source_labels: [job]
        regex: '.*-([^-]+)\$'
        target_label: host
        replacement: '\${1}'
EOF
}

# Parse hosts.yaml using pure bash
# This simple parser handles the expected YAML format
parse_GENERATE_CONFIG() {
    local in_host=0
    local host_name=""
    local host_address=""
    local gpu_exporter_port=""
    local gpu_pcie_port=""
    local node_exporter_port=""
    local process_exporter_port=""
    local host_count=0

    # Function to emit current host config
    emit_host() {
        if [[ -n "$host_name" && -n "$host_address" ]]; then
            # Use custom ports or defaults
            local gpu_exporter_port="${gpu_exporter_port:-$DEFAULT_GPU_EXPORTER_PORT}"
            local pcie_port="${gpu_pcie_port:-$DEFAULT_GPU_PCIE_PORT}"
            local node_port="${node_exporter_port:-$DEFAULT_NODE_EXPORTER_PORT}"
            local proc_port="${process_exporter_port:-$DEFAULT_PROCESS_EXPORTER_PORT}"

            # Add comment for this host
            echo "  # $host_name" >> "$OUTPUT_FILE"

            # Generate the four scrape configs
            generate_scrape_config "gpu_exporter" "$host_name" "$host_address" "$gpu_exporter_port" >> "$OUTPUT_FILE"
            generate_scrape_config "gpu_pcie_exporter" "$host_name" "$host_address" "$pcie_port" >> "$OUTPUT_FILE"
            generate_scrape_config "node_exporter" "$host_name" "$host_address" "$node_port" >> "$OUTPUT_FILE"
            generate_scrape_config "process_exporter" "$host_name" "$host_address" "$proc_port" >> "$OUTPUT_FILE"

            echo "" >> "$OUTPUT_FILE"
            ((host_count++))
        fi
    }

    echo "" >> "$OUTPUT_FILE"

    while IFS= read -r line || [[ -n "$line" ]]; do
        # Skip empty lines and comments
        [[ -z "$line" || "$line" =~ ^[[:space:]]*# ]] && continue

        # Remove leading/trailing whitespace and inline comments
        trimmed=$(echo "$line" | sed 's/^[[:space:]]*//;s/[[:space:]]*#.*$//;s/[[:space:]]*$//')

        # Check for new host entry (starts with "- name:")
        if [[ "$trimmed" =~ ^-[[:space:]]*name:[[:space:]]*(.*) ]]; then
            # Emit previous host if exists
            emit_host

            # Reset for new host
            host_name="${BASH_REMATCH[1]}"
            host_address=""
            gpu_exporter_port=""
            gpu_pcie_port=""
            node_exporter_port=""
            process_exporter_port=""
            in_host=1
        elif [[ $in_host -eq 1 ]]; then
            # Parse host properties
            if [[ "$trimmed" =~ ^address:[[:space:]]*(.*) ]]; then
                host_address="${BASH_REMATCH[1]}"
            elif [[ "$trimmed" =~ ^gpu_exporter_port:[[:space:]]*(.*) ]]; then
                gpu_exporter_port="${BASH_REMATCH[1]}"
            elif [[ "$trimmed" =~ ^gpu_pcie_port:[[:space:]]*(.*) ]]; then
                gpu_pcie_port="${BASH_REMATCH[1]}"
            elif [[ "$trimmed" =~ ^node_exporter_port:[[:space:]]*(.*) ]]; then
                node_exporter_port="${BASH_REMATCH[1]}"
            elif [[ "$trimmed" =~ ^process_exporter_port:[[:space:]]*(.*) ]]; then
                process_exporter_port="${BASH_REMATCH[1]}"
            fi
        fi
    done < "$GENERATE_CONFIG"

    # Emit the last host
    emit_host

    echo "$host_count"
}

# Parse hosts and generate configs
HOST_COUNT=$(parse_GENERATE_CONFIG)

# Append rule_files section if it exists
if [[ -n "$RULE_FILES_SECTION" ]]; then
    echo "$RULE_FILES_SECTION" >> "$OUTPUT_FILE"
fi

echo "Generated: $OUTPUT_FILE"
echo "Hosts configured: $HOST_COUNT"
