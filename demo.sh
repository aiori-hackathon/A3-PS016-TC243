#!/usr/bin/env bash
# demo.sh — LostPacket Demonstration Runner
# Team ID: A3-PS016-TC243
# Problem Statement: PS-016 — Simulation-Based Handover Decision Evaluation for High-Mobility Users

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -f "$SCRIPT_DIR/ns3" ]]; then
    NS3_ROOT="$SCRIPT_DIR"
elif [[ -f "$SCRIPT_DIR/../ns3" ]]; then
    NS3_ROOT="$(dirname "$SCRIPT_DIR")"
else
    NS3_ROOT="/home/arjya/projects/lostpacket/ns-3.48"
fi

DEMO_OUT="$NS3_ROOT/results/demo"
mkdir -p "$DEMO_OUT/a3_120kmh" "$DEMO_OUT/rf_120kmh"

echo "================================================================================"
echo "LostPacket Prototype Demonstration — Problem Statement PS-016"
echo "Team ID: A3-PS016-TC243"
echo "Simulator: ns-3.48 (LTE/EPC Module)"
echo "Topology: 3 Linear eNodeBs (0m, 500m, 1000m) | Radio Model: Friis Propagation"
echo "================================================================================"
echo ""

# Part 1: Baseline A3 Run
echo "--- STEP 1: Running 3GPP Event A3 Baseline (120 km/h, Held-out Seed 6) ---"
cd "$NS3_ROOT"
./ns3 run "lostpacket-lte-handover \
    --policy=a3 \
    --speedKmh=120 \
    --seed=6 \
    --run=1 \
    --outDir=$DEMO_OUT/a3_120kmh" | grep -E "===|HO_|Throughput: [0-9]|Packets|Throughput Mbps|Gap" | head -n 25

echo ""
echo "A3 Baseline Run Completed."
echo ""

# Part 2: Random Forest Controller Run
echo "--- STEP 2: Running Random Forest Controller (120 km/h, Held-out Seed 6) ---"
./ns3 run "lostpacket-lte-handover \
    --policy=rf \
    --speedKmh=120 \
    --seed=6 \
    --run=1 \
    --outDir=$DEMO_OUT/rf_120kmh" | grep -E "===|HO_|Throughput: [0-9]|Packets|Throughput Mbps|Gap" | head -n 25

echo ""
echo "Random Forest Controller Run Completed."
echo ""

# Part 3: Comparative Summary
echo "================================================================================"
echo "Comparative Execution Summary (Held-Out Seed 6, 120 km/h):"
echo "================================================================================"
printf "%-10s %-12s %-12s %-12s %-12s %-14s %-16s\n" "Policy" "HO Attempts" "HO Success" "HO Failures" "Ping-Pongs" "Throughput" "PDR (%)"

# Parse A3 summary
if [[ -f "$DEMO_OUT/a3_120kmh/summary.csv" ]]; then
    A3_DATA=$(tail -n 1 "$DEMO_OUT/a3_120kmh/summary.csv")
    A3_ATT=$(echo "$A3_DATA" | cut -d',' -f6)
    A3_SUC=$(echo "$A3_DATA" | cut -d',' -f7)
    A3_FAI=$(echo "$A3_DATA" | cut -d',' -f8)
    A3_PP=$(echo "$A3_DATA" | cut -d',' -f9)
    A3_TX=$(echo "$A3_DATA" | cut -d',' -f10)
    A3_RX=$(echo "$A3_DATA" | cut -d',' -f11)
    A3_TPUT=$(echo "$A3_DATA" | cut -d',' -f13)
    A3_PDR=$(python3 -c "print(f'{$A3_RX/$A3_TX*100:.2f}%')")
    printf "%-10s %-12s %-12s %-12s %-12s %-14s %-16s\n" "A3" "$A3_ATT" "$A3_SUC" "$A3_FAI" "$A3_PP" "${A3_TPUT} Mbps" "$A3_PDR"
fi

# Parse RF summary
if [[ -f "$DEMO_OUT/rf_120kmh/summary.csv" ]]; then
    RF_DATA=$(tail -n 1 "$DEMO_OUT/rf_120kmh/summary.csv")
    RF_ATT=$(echo "$RF_DATA" | cut -d',' -f6)
    RF_SUC=$(echo "$RF_DATA" | cut -d',' -f7)
    RF_FAI=$(echo "$RF_DATA" | cut -d',' -f8)
    RF_PP=$(echo "$RF_DATA" | cut -d',' -f9)
    RF_TX=$(echo "$RF_DATA" | cut -d',' -f10)
    RF_RX=$(echo "$RF_DATA" | cut -d',' -f11)
    RF_TPUT=$(echo "$RF_DATA" | cut -d',' -f13)
    RF_PDR=$(python3 -c "print(f'{$RF_RX/$RF_TX*100:.2f}%')")
    printf "%-10s %-12s %-12s %-12s %-12s %-14s %-16s\n" "RF" "$RF_ATT" "$RF_SUC" "$RF_FAI" "$RF_PP" "${RF_TPUT} Mbps" "$RF_PDR"
fi

echo "================================================================================"
echo "Demonstration complete. Artifacts saved in: $DEMO_OUT"
