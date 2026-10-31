#!/usr/bin/env bash
# run_baseline.sh — Reproducible multi-seed, multi-speed A3 baseline runner
#
# Usage:
#   ./scripts/run_baseline.sh [--seeds "1 2 3 4 5"] [--speeds "30 120"] [--policy a3] [--jobs 2]
#
# Outputs per-run CSVs to results/a3/<speed>kmh/seed<S>/
# Appends one row to results/a3/all_runs_summary.csv for each completed run.
#
# Requirements: ns3 binary must be built at ../ns-3 (run from ns-3.48/ root).
#
# Example:
#   cd /home/arjya/projects/lostpacket/ns-3.48
#   bash scripts/run_baseline.sh --seeds "1 2 3 4 5" --speeds "30 120" --jobs 1

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NS3_ROOT="$(dirname "$SCRIPT_DIR")"

# Defaults
SEEDS="1 2 3 4 5"
SPEEDS="30 120"
POLICY="a3"
JOBS=1   # Sequential by default to avoid simulator conflicts

# Parse args
while [[ $# -gt 0 ]]; do
    case "$1" in
        --seeds)   SEEDS="$2";  shift 2 ;;
        --speeds)  SPEEDS="$2"; shift 2 ;;
        --policy)  POLICY="$2"; shift 2 ;;
        --jobs)    JOBS="$2";   shift 2 ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done

SUMMARY_DIR="$NS3_ROOT/results/$POLICY"
SUMMARY_CSV="$SUMMARY_DIR/all_runs_summary.csv"

mkdir -p "$SUMMARY_DIR"

# Write CSV header if file doesn't exist
if [[ ! -f "$SUMMARY_CSV" ]]; then
    echo "policy,speed_kmh,seed,run,sim_time_s,ho_attempts,ho_successes,ho_failures,ping_pong,tx_packets,rx_packets,rx_bytes,mean_throughput_mbps,mean_ho_rx_gap_s,valid_gap_measurements" \
        > "$SUMMARY_CSV"
fi

echo "=== LostPacket Baseline Runner ==="
echo "Policy:  $POLICY"
echo "Speeds:  $SPEEDS km/h"
echo "Seeds:   $SEEDS"
echo "Jobs:    $JOBS"
echo "Output:  $SUMMARY_DIR"
echo ""

run_single() {
    local speed="$1"
    local seed="$2"
    local run="1"

    local out_dir="$SUMMARY_DIR/${speed}kmh/seed${seed}"
    mkdir -p "$out_dir"

    local log_file="$out_dir/sim.log"

    echo "[$(date +%H:%M:%S)] Running speed=${speed}km/h seed=${seed} -> $out_dir"

    cd "$NS3_ROOT"
    ./ns3 run "lostpacket-lte-handover \
        --speedKmh=${speed} \
        --seed=${seed} \
        --run=${run} \
        --policy=${POLICY} \
        --outDir=${out_dir}" \
        > "$log_file" 2>&1

    local exit_code=$?
    if [[ $exit_code -ne 0 ]]; then
        echo "  ERROR: simulation failed (exit $exit_code). See $log_file"
        return $exit_code
    fi

    # Append summary row (skip header line from per-run summary.csv)
    if [[ -f "$out_dir/summary.csv" ]]; then
        tail -n +2 "$out_dir/summary.csv" >> "$SUMMARY_CSV"
        echo "  Done. $(tail -n 1 "$out_dir/summary.csv")"
    else
        echo "  WARNING: summary.csv not found in $out_dir"
    fi
}

export -f run_single

if [[ "$JOBS" -gt 1 ]]; then
    # Parallel execution using bash background jobs
    pids=()
    for speed in $SPEEDS; do
        for seed in $SEEDS; do
            run_single "$speed" "$seed" &
            pids+=($!)
            # Limit concurrency
            while [[ ${#pids[@]} -ge $JOBS ]]; do
                wait "${pids[0]}"
                pids=("${pids[@]:1}")
            done
        done
    done
    # Wait for remaining
    for pid in "${pids[@]}"; do
        wait "$pid"
    done
else
    # Sequential
    for speed in $SPEEDS; do
        for seed in $SEEDS; do
            run_single "$speed" "$seed"
        done
    done
fi

echo ""
echo "=== All runs complete ==="
echo "Aggregate summary: $SUMMARY_CSV"
echo ""
echo "Rows written:"
wc -l "$SUMMARY_CSV"
