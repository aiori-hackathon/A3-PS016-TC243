#!/usr/bin/env bash
# run_evaluation.sh — Evaluates A3 and Random Forest across held-out seeds 6-10 at 30 and 120 km/h

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NS3_ROOT="$(dirname "$SCRIPT_DIR")"

EVAL_DIR="$NS3_ROOT/results/eval"
SUMMARY_CSV="$EVAL_DIR/evaluation_summary.csv"

mkdir -p "$EVAL_DIR"

echo "policy,speed_kmh,seed,run,sim_time_s,ho_attempts,ho_successes,ho_failures,ping_pong,tx_packets,rx_packets,rx_bytes,mean_throughput_mbps,mean_ho_rx_gap_s,valid_gap_measurements" > "$SUMMARY_CSV"

POLICIES="a3 rf"
SEEDS="6 7 8 9 10"
SPEEDS="120 30"

echo "Starting held-out evaluation (Seeds 6-10)..."
echo "Policies: $POLICIES"
echo "Speeds:   $SPEEDS km/h"
echo "Seeds:    $SEEDS"
echo "Summary:  $SUMMARY_CSV"

cd "$NS3_ROOT"

for pol in $POLICIES; do
    for speed in $SPEEDS; do
        for seed in $SEEDS; do
            run_out_dir="$EVAL_DIR/${pol}/${speed}kmh/seed${seed}"
            mkdir -p "$run_out_dir"
            echo "[$(date +%H:%M:%S)] Running policy=${pol}, speed=${speed} km/h, seed=${seed}..."
            ./ns3 run "lostpacket-lte-handover \
                --policy=${pol} \
                --speedKmh=${speed} \
                --seed=${seed} \
                --run=1 \
                --outDir=${run_out_dir}" > "$run_out_dir/sim.log" 2>&1

            if [[ -f "$run_out_dir/summary.csv" ]]; then
                tail -n +2 "$run_out_dir/summary.csv" >> "$SUMMARY_CSV"
            else
                echo "Warning: $run_out_dir/summary.csv not found"
            fi
        done
    done
done

echo "Held-out evaluation complete."
echo "Total evaluation runs recorded: $(tail -n +2 "$SUMMARY_CSV" | wc -l)"
