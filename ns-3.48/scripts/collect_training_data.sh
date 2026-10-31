#!/usr/bin/env bash
# collect_training_data.sh — Collects training data across seeds 1-5 for 30 and 120 km/h

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NS3_ROOT="$(dirname "$SCRIPT_DIR")"

OUT_DIR="$NS3_ROOT/results/train"
DATASET_CSV="$OUT_DIR/training_dataset.csv"

mkdir -p "$OUT_DIR"

echo "time_s,rnti,speed_kmh,serving_rsrp_dbm,target_rsrp_dbm,delta_rsrp_db,serving_rsrq_db,target_rsrq_db,delta_rsrq_db,num_neighbours,target_cell_id,handover_label" > "$DATASET_CSV"

SEEDS="1 2 3 4 5"
SPEEDS="120 30"

echo "Starting training data collection..."
echo "Seeds:  $SEEDS"
echo "Speeds: $SPEEDS km/h"
echo "Output: $DATASET_CSV"

cd "$NS3_ROOT"

for speed in $SPEEDS; do
    for seed in $SEEDS; do
        run_out_dir="$OUT_DIR/raw_${speed}kmh_seed${seed}"
        mkdir -p "$run_out_dir"
        echo "[$(date +%H:%M:%S)] Collecting speed=${speed} km/h, seed=${seed}..."
        ./ns3 run "lostpacket-lte-handover \
            --policy=train \
            --speedKmh=${speed} \
            --seed=${seed} \
            --run=1 \
            --trainingFile=${DATASET_CSV} \
            --outDir=${run_out_dir}" > "$run_out_dir/sim.log" 2>&1
    done
done

echo "Training data collection complete."
echo "Total samples collected: $(wc -l < "$DATASET_CSV") lines (including header)"
