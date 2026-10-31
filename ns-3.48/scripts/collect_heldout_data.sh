#!/usr/bin/env bash
# collect_heldout_data.sh — Collects held-out measurement dataset across seeds 6-10 for offline evaluation

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NS3_ROOT="$(dirname "$SCRIPT_DIR")"

OUT_DIR="$NS3_ROOT/results/eval"
HELDOUT_CSV="$OUT_DIR/heldout_dataset.csv"

echo "time_s,rnti,speed_kmh,serving_rsrp_dbm,target_rsrp_dbm,delta_rsrp_db,serving_rsrq_db,target_rsrq_db,delta_rsrq_db,num_neighbours,target_cell_id,handover_label" > "$HELDOUT_CSV"

SEEDS="6 7 8 9 10"
SPEEDS="120 30"

echo "Starting held-out test data collection (seeds 6-10)..."
cd "$NS3_ROOT"

for speed in $SPEEDS; do
    for seed in $SEEDS; do
        tmp_dir="/tmp/heldout_${speed}kmh_seed${seed}"
        mkdir -p "$tmp_dir"
        ./ns3 run "lostpacket-lte-handover \
            --policy=train \
            --speedKmh=${speed} \
            --seed=${seed} \
            --run=1 \
            --trainingFile=${HELDOUT_CSV} \
            --outDir=${tmp_dir}" > /dev/null 2>&1
        rm -rf "$tmp_dir"
    done
done

echo "Held-out test dataset collection complete: $(wc -l < "$HELDOUT_CSV") lines."
