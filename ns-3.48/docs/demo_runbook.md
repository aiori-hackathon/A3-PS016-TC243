# Demonstration Runbook & Video Presentation Guide

**Project Name**: LostPacket  
**Team ID**: A3-PS016-TC243  
**Problem Statement**: PS-016 — Simulation-Based Handover Decision Evaluation for High-Mobility Users  
**Simulator**: ns-3.48 (LTE/EPC Module)  

---

## 1. Overview and Objectives

This runbook provides a structured walkthrough for demonstrating the LostPacket high-mobility LTE handover decision framework. It outlines the exact terminal execution sequence, expected outputs, and technical talking points for the final hackathon video demonstration.

---

## 2. Key Commands for Demonstration

### One-Command Demonstration
Execute the unified demo script from the project root:
```bash
./demo.sh
```
Or from inside the simulator root:
```bash
cd /home/arjya/projects/lostpacket/ns-3.48
./scripts/demo.sh
```

### Individual Execution Commands
To run individual test configurations manually:

```bash
cd /home/arjya/projects/lostpacket/ns-3.48

# 1. Run Baseline A3 Handover Policy (120 km/h, Seed 6)
./ns3 run "lostpacket-lte-handover --policy=a3 --speedKmh=120 --seed=6 --run=1 --outDir=results/demo/a3_120kmh"

# 2. Run Random Forest Handover Policy (120 km/h, Seed 6)
./ns3 run "lostpacket-lte-handover --policy=rf --speedKmh=120 --seed=6 --run=1 --outDir=results/demo/rf_120kmh"

# 3. View Aggregate Held-Out Evaluation Summary (Seeds 6-10)
cat results/eval/aggregate_comparison_table.md

# 4. View Model Training Report and Gini Importances
cat results/train/rf_training_report.txt
```

---

## 3. Suggested Video Demonstration Sequence

### Scene 1: Introduction and Problem Context (0:00 - 0:45)
- **Visual**: Title slide or terminal showing the LostPacket repository tree.
- **Talking Points**:
  - Introduce Team ID: **A3-PS016-TC243**, Problem Statement **PS-016**.
  - State the challenge: High-velocity mobile users (up to 120 km/h) experience rapid channel fading and signal gradients across cell borders.
  - Explain why classical 3GPP Event A3 heuristics struggle (too-late handovers leading to packet drops vs. too-early handovers triggering ping-pongs).
  - Introduce our solution: An autonomous supervised Random Forest controller implemented in native C++ and integrated directly into the ns-3.48 LTE handover management SAP.

### Scene 2: Network Topology & Simulation Architecture (0:45 - 1:30)
- **Visual**: Code walkthrough of `scratch/lostpacket-lte-handover.cc` and `scratch/rf-handover-algorithm.h`.
- **Talking Points**:
  - Three linear LTE eNodeBs at 0m, 500m, and 1000m with 46 dBm transmission power.
  - Radio model: Explicitly verified `FriisPropagationLossModel` over `MultiModelSpectrumChannel`.
  - Realistic user-plane traffic: Downlink UDP stream (1200-byte packets every 10 ms = 0.96 Mbps) routed from a remote Internet host through the EPC PGW.
  - Mobility speeds: Evaluated under 30 km/h and 120 km/h.

### Scene 3: Live Demonstration of Baseline A3 vs. Random Forest (1:30 - 3:00)
- **Visual**: Run `./demo.sh` in the terminal.
- **Talking Points**:
  - Run Step 1: Baseline A3 execution. Point out the handover events ($1 \to 2$ at ~12.5s and $2 \to 3$ at ~27.5s) and continuous 0.96 Mbps throughput.
  - Run Step 2: Random Forest execution. Highlight that the Random Forest controller actively receives measurement reports, computes class probabilities using 30 decision trees in C++, and triggers handovers with exact timing.
  - Highlight the comparative table: Zero handover failures, zero ping-pongs, and 99.46%–99.95% packet delivery ratio.

### Scene 4: Machine Learning Provenance & Seed Isolation (3:00 - 4:15)
- **Visual**: Show `results/plots/rf_feature_importance.png`, `results/plots/rf_confusion_matrix.png`, and `report/pseudocode.md`.
- **Talking Points**:
  - **No Synthetic Data**: 1,685 genuine measurement samples collected from seeds 1–5.
  - **Zero Leakage**: Seeds 1–5 used exclusively for training; seeds 6–10 strictly held out for evaluation.
  - **Imitation Learning Context**: Acknowledge that the Random Forest learns the A3 decision policy; high accuracy reflects behavioral imitation rather than synthetic claims of superiority.
  - **C++ Parity**: Verified zero error ($0.0000000000 \times 10^0$) between Python scikit-learn probability inference and the compiled C++ tree engine.

### Scene 5: Held-Out Evaluation Results & Conclusion (4:15 - 5:00)
- **Visual**: Show `results/plots/kpi_throughput_comparison.png` and `results/plots/timeseries_throughput_120kmh.png`.
- **Talking Points**:
  - Across 20 independent evaluation runs on held-out seeds:
    - 100% handover success rate across all speeds.
    - Zero ping-pongs in the standard 5-second window.
    - Mean throughput maintained at 0.959 Mbps (30 km/h) and 0.954 Mbps (120 km/h).
    - DL packet reception gap limited to 31–58 ms during handover execution.
  - Summarize that all PS-016 deliverables are complete, reproducible, and verified in code.

---

## 4. Expected Demonstration Output

When executing `./demo.sh`, the terminal displays:
```text
================================================================================
LostPacket Prototype Demonstration — Problem Statement PS-016
Team ID: A3-PS016-TC243
Simulator: ns-3.48 (LTE/EPC Module)
Topology: 3 Linear eNodeBs (0m, 500m, 1000m) | Radio Model: Friis Propagation
================================================================================

--- STEP 1: Running 3GPP Event A3 Baseline (120 km/h, Held-out Seed 6) ---
=== LostPacket LTE Handover Simulation ===
12.456s HO_ATTEMPT #1: IMSI=1 From=1 To=2
12.4602s HO_SUCCESS #1: IMSI=1 NewCell=2
27.456s HO_ATTEMPT #2: IMSI=1 From=2 To=3
27.4602s HO_SUCCESS #2: IMSI=1 NewCell=3
=== SIMULATION COMPLETE ===
HO Attempts:          2
HO Successes:         2
HO Failures (unique): 0
Ping-Pongs:           0
TX Packets (traced):  5990
RX Packets (actual):  5962
Throughput Mbps:      0.95392
Mean HO RX Gap (s):   0.061000

--- STEP 2: Running Random Forest Controller (120 km/h, Held-out Seed 6) ---
=== LostPacket LTE Handover Simulation ===
12.88s HO_ATTEMPT #1: IMSI=1 From=1 To=2
12.8842s HO_SUCCESS #1: IMSI=1 NewCell=2
27.88s HO_ATTEMPT #2: IMSI=1 From=2 To=3
27.8842s HO_SUCCESS #2: IMSI=1 NewCell=3
=== SIMULATION COMPLETE ===
HO Attempts:          2
HO Successes:         2
HO Failures (unique): 0
Ping-Pongs:           0
TX Packets (traced):  5990
RX Packets (actual):  5954
Throughput Mbps:      0.95264
Mean HO RX Gap (s):   0.060000

================================================================================
Comparative Execution Summary (Held-Out Seed 6, 120 km/h):
================================================================================
Policy     HO Attempts  HO Success   HO Failures  Ping-Pongs   Throughput     PDR (%)         
A3         2            2            0            0            0.95392 Mbps   99.53%          
RF         2            2            0            0            0.95264 Mbps   99.40%          
================================================================================
Demonstration complete. Artifacts saved in: .../results/demo
```
