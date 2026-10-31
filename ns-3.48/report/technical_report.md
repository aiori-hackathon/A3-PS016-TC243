# Technical Report: Simulation-Based Handover Decision Evaluation for High-Mobility LTE Users

**Project Title**: LostPacket  
**Team ID**: A3-PS016-TC243  
**Problem Statement**: PS-016 — Simulation-Based Handover Decision Evaluation for High-Mobility Users  
**Category**: 6G & Future Networks  
**Simulator**: ns-3.48 (Discrete-Event Network Simulator, LTE/EPC Module Only)  
**Lead Contributor / Author**: arjya (`/home/arjya`)  
**Date**: October 31, 2026  

---

## Abstract

High-mobility vehicular communication corridors present severe operational challenges for cellular handovers, including late triggering, radio link failure, packet loss, and ping-pong oscillations. This study presents LostPacket, a fully reproducible, standards-compliant simulation and machine learning evaluation framework developed for ns-3.48 LTE/EPC. We deploy a three-cell linear LTE network corridor traversed by a high-velocity User Equipment (UE) operating at 30 km/h (low mobility) and 120 km/h (high mobility). We benchmark the classical 3GPP Event A3 Reference Signal Received Power (RSRP) heuristic against an autonomous supervised Random Forest controller implemented in native C++ and integrated directly into the ns-3 LTE Handover Management Service Access Point (SAP). To ensure scientific integrity, strict seed isolation was enforced: 1,685 genuine measurement samples were collected across seeds 1–5 for imitation learning, while a 20-run evaluation matrix was conducted on held-out seeds 6–10. Bit-level inference parity between Python scikit-learn and the native C++ engine was verified ($0.0000000000 \times 10^0$ error). Across all 20 held-out evaluation runs, both policies achieved 100% handover success (zero failures), zero ping-pong events, sustained downlink application throughput above 0.953 Mbps (99.46%–99.95% packet delivery ratio), and limited the handover-associated downlink packet reception gap to 31–60 ms. The Random Forest controller successfully imitated the Event A3 policy with 100% precision and recall on unseen seeds, proving that learned decision policies can be deterministically embedded within discrete-event network simulation kernels.

---

## 1. Problem Statement

In cellular radio access networks, handovers transfer an ongoing data session between base stations (eNodeBs) to maintain connectivity as users move through space. High-velocity transit (such as highway vehicular mobility at 120 km/h) creates acute challenges:
- **Steep Path Loss Gradients**: The User Equipment (UE) traverses cell boundaries rapidly, experiencing sudden signal decay.
- **Doppler Shift and Multipath Distortion**: Channel conditions fluctuate rapidly, compressing the available decision window.
- **Too-Late Handover Dilemma**: Conservative triggering thresholds or prolonged Time-to-Trigger (TTT) values delay execution until signal quality drops below the receiver sensitivity threshold, causing radio link failure (RLF), call drop, and severe user-plane blackout.
- **Too-Early / Ping-Pong Oscillation**: Overly aggressive triggering thresholds cause premature handovers and rapid bouncing ($A \to B \to A$) between adjacent base stations, flooding the core network with control-plane signaling and inducing repetitive user-plane interruptions.

Addressing this trade-off requires evaluating handover decision algorithms under controlled, reproducible conditions using discrete-event network simulation.

---

## 2. Objectives

The primary engineering and scientific objectives of this investigation are:
1. Construct an explicit, standards-compliant three-cell LTE simulation environment using ns-3.48 without 5G abstractions.
2. Model vehicular mobility across low-mobility ($30\text{ km/h}$) and high-mobility ($120\text{ km/h}$) conditions.
3. Establish a 3GPP Event A3 baseline configured with standard hysteresis and Time-to-Trigger parameters.
4. Instrument comprehensive packet-level and control-plane metrics: handover attempts, successes, failures across all four eNodeB failure causes, ping-pong events, throughput, packet delivery ratio, and packet reception gaps.
5. Collect genuine simulation data without synthetic labels to train a supervised Random Forest classifier via imitation learning.
6. Integrate the trained Random Forest directly into the ns-3 C++ Handover Management SAP so that machine learning inferences actively govern simulation execution.
7. Conduct a statistically sound, held-out evaluation across multiple random seeds (seeds 6–10) to quantify policy trade-offs and verify operational stability.

---

## 3. System Architecture

The LostPacket system architecture couples the ns-3.48 discrete-event network simulator with a native C++ machine learning decision engine. The network consists of:
- **Evolved Packet Core (EPC)**: Instantiated via `ns3::PointToPointEpcHelper`, modeling the Packet Data Network Gateway (PGW), Serving Gateway (SGW), and Mobility Management Entity (MME).
- **Remote Internet Host**: Connected to the PGW via a Point-to-Point link (100 Gbps bandwidth, 10 ms propagation delay).
- **Three LTE Macro eNodeBs**: Deployed in a linear corridor with inter-eNodeB X2 communication interfaces.
- **Vehicular User Equipment (UE)**: Mobile node attached to the EPC and running a downlink UDP sink application.
- **Handover Management SAP**: Interface connecting the eNodeB Radio Resource Control (RRC) entity to the handover algorithm.

![Figure 1: LostPacket LTE Handover Simulation Architecture](results/plots/architecture_uml_diagram.png)

---

## 4. LTE Simulation Configuration

### 4.1 Physical Radio and Spectrum Configuration
All physical layer and propagation parameters are explicitly configured to ensure reproducibility:
- **Spectrum Channel**: `MultiModelSpectrumChannel` providing independent channel realizations.
- **Propagation Model**: Explicit `ns3::FriisPropagationLossModel` with free-space path loss attenuation.
- **eNodeB Transmission Power**: $46.0\text{ dBm}$ ($40\text{ W}$ per carrier).
- **Antenna Model**: `IsotropicAntennaModel` installed on both eNodeB and UE nodes.
- **Carrier Frequency**: Downlink EARFCN 100 ($2120\text{ MHz}$); Uplink EARFCN 19100 ($1930\text{ MHz}$).
- **Bandwidth**: 25 Resource Blocks ($5\text{ MHz}$ bandwidth).
- **MAC Scheduler**: Round-Robin scheduling (`ns3::RrFfMacScheduler`).

### 4.2 Network Topology, Trajectory, and Observation Windows
The network topology comprises three linear eNodeBs:
- eNodeB 1: $x = 0\text{ m}, y = 0\text{ m}, z = 0\text{ m}$
- eNodeB 2: $x = 500\text{ m}, y = 0\text{ m}, z = 0\text{ m}$
- eNodeB 3: $x = 1000\text{ m}, y = 0\text{ m}, z = 0\text{ m}$

The UE starts at coordinate $x = -100\text{ m}, y = 0\text{ m}, z = 0\text{ m}$ and travels in the positive $x$-direction at constant velocity:
- **Speed Class 1 (Low Mobility)**: $30\text{ km/h} \approx 8.333\text{ m/s}$.
- **Speed Class 2 (High Mobility)**: $120\text{ km/h} \approx 33.333\text{ m/s}$.

#### Trajectory and Observation Window Clarification
To ensure scientifically defensible observation windows:
- The active multi-cell service corridor extends from $x = -100\text{ m}$ to $x = 1200\text{ m}$ ($1300\text{ m}$ distance; $200\text{ m}$ past eNodeB 3). The transit time across this corridor is $156.0\text{ s}$ at $30\text{ km/h}$ and $39.0\text{ s}$ at $120\text{ km/h}$.
- To prevent premature termination immediately following the second handover, an extended observation margin of $1.5\times$ was applied, yielding total simulation durations of $240.0\text{ s}$ ($30\text{ km/h}$) and $60.0\text{ s}$ ($120\text{ km/h}$).
- Under constant velocity, the UE covers an identical total distance of $2000.0\text{ m}$ (from $x = -100\text{ m}$ to $x = +1900\text{ m}$) in both scenarios.
- The terminal $700\text{ m}$ segment ($x = 1200\text{ m}$ to $x = 1900\text{ m}$) provides an extended post-handover observation window of $84.0\text{ s}$ at $30\text{ km/h}$ and $21.0\text{ s}$ at $120\text{ km/h}$ (well beyond the $5.0\text{ s}$ ping-pong detection window). This confirms that no delayed ping-pong triggers or unhandled link failures occur as the UE enters the fringe of Cell 3.

### 4.3 Traffic Model
Continuous downlink UDP traffic is generated by the remote host directed to port 9000 on the UE:
- Packet size: $1200\text{ bytes}$.
- Packet interval: $10\text{ ms}$ ($100\text{ packets/second}$).
- Offered load: $0.96\text{ Mbps}$ continuous downlink.
- Application start time: $t = 0.1\text{ s}$; Stop time: $t = \text{simTime}$.
- Total transmitted packets: $23,990\text{ packets}$ for $240\text{ s}$; $5,990\text{ packets}$ for $60\text{ s}$.

---

## 5. Baseline Handover Algorithm (3GPP Event A3)

The baseline policy implements the 3GPP Event A3 heuristic via `ns3::A3RsrpHandoverAlgorithm`. Event A3 triggers when a neighboring cell's Reference Signal Received Power ($RSRP_{target}$) exceeds the serving cell's power ($RSRP_{serving}$) by an offset margin:

$$RSRP_{target} > RSRP_{serving} + Hysteresis \quad \text{for duration } TimeToTrigger$$

Parameters configured according to 3GPP specifications:
- **Hysteresis**: $3.0\text{ dB}$ (prevents spurious triggering due to minor channel variations).
- **TimeToTrigger (TTT)**: $256\text{ ms}$ (requires sustained signal superiority before execution).

---

## 6. Supervised Machine Learning Methodology

### 6.1 Imitation Learning Framework
Rather than introducing unverified reinforcement learning or synthetic rule-sets, the Random Forest model is formulated as an **imitation learning agent**. It observes real-time UE measurement reports and learns to replicate the operational boundary of the 3GPP Event A3 algorithm. High classification accuracy confirms behavioral imitation fidelity, establishing that a machine learning policy can accurately capture and execute standard cellular handovers.

### 6.2 Feature Space Definition
For each incoming UE measurement report, an 8-dimensional feature vector is extracted:
1. `serving_rsrp_dbm`: RSRP of the current serving cell (dBm).
2. `target_rsrp_dbm`: RSRP of the candidate neighbor cell (dBm).
3. `delta_rsrp_db`: Relative signal strength difference ($RSRP_{target} - RSRP_{serving}$).
4. `serving_rsrq_db`: Reference Signal Received Quality of serving cell (dB).
5. `target_rsrq_db`: RSRQ of candidate neighbor cell (dB).
6. `delta_rsrq_db`: Relative signal quality difference ($RSRQ_{target} - RSRQ_{serving}$).
7. `num_neighbours`: Number of visible neighbor cells reported.
8. `speed_kmh`: Velocity of the UE ($30$ or $120\text{ km/h}$).

### 6.3 Dataset Provenance and Class Balance
Training data was collected across 10 simulation runs using independent random seeds 1, 2, 3, 4, and 5 at both $30\text{ km/h}$ and $120\text{ km/h}$. A total of 1,685 measurement samples were recorded:
- **Class 0 (Hold / No Handover)**: 1,665 samples ($98.81\%$).
- **Class 1 (Trigger Handover)**: 20 samples ($1.19\%$).

This acute class imbalance reflects real-world cellular environments, where handovers are brief transition events separated by long periods of steady tracking. To prevent majority-class bias, balanced class weighting was applied during training ($w_c = N / (2 \cdot N_c)$).

### 6.4 Model Architecture and Feature Importance
The Random Forest ensemble was trained with the following hyperparameter specification:
- Number of estimators ($M$): 30 decision trees.
- Maximum tree depth: 5.
- Minimum samples per split: 4.
- Splitting criterion: Gini impurity.
- Class weight: `balanced`.
- Random state seed: 42.

Feature importance analysis confirms that differential metrics govern the decision:
1. `delta_rsrq_db`: 0.2975
2. `serving_rsrq_db`: 0.2336
3. `delta_rsrp_db`: 0.2224
4. `target_rsrq_db`: 0.1382
5. `target_rsrp_dbm`: 0.0628
6. `serving_rsrp_dbm`: 0.0362
7. `speed_kmh`: 0.0093
8. `num_neighbours`: 0.0000

![Figure 2: Random Forest Feature Importance Distribution](results/plots/rf_feature_importance.png)

---

## 7. Training and Evaluation Methodology

### 7.1 Autocorrelation and Seed-Level Separation
In cellular simulation, successive measurement reports received every $240\text{ ms}$ from a moving vehicle exhibit high autocorrelation. Shuffling samples across time within the same simulation run induces temporal data leakage. 

To ensure complete statistical independence:
- **Training Set**: Collected strictly from Seeds 1, 2, 3, 4, 5 (1,685 samples).
- **Held-Out Evaluation Set**: Collected independently from Seeds 6, 7, 8, 9, 10 (1,685 samples). No samples from seeds 6–10 were accessible during model fitting.

### 7.2 Training Cross-Validation
Across 5-fold stratified cross-validation on seeds 1–5, the model achieved:
- Precision: $1.0000 \pm 0.0000$
- Recall: $1.0000 \pm 0.0000$
- F1-Score: $1.0000 \pm 0.0000$
- ROC-AUC: $1.0000 \pm 0.0000$

![Figure 3: Training Confusion Matrix (Seeds 1 to 5 Cross-Validation)](results/plots/rf_confusion_matrix.png)

### 7.3 Held-Out Test Dataset Evaluation (Seeds 6 to 10)
When evaluated offline on the 1,685 samples collected from held-out seeds 6–10 (containing 1,665 Hold and 20 Handover ground-truth events):
- Test Accuracy: $1.0000$
- Test Precision: $1.0000$
- Test Recall: $1.0000$
- Test F1-Score: $1.0000$
- Confusion Matrix: `[[1665, 0], [0, 20]]`

![Figure 4: Held-Out Test Confusion Matrix (Seeds 6 to 10)](results/plots/rf_heldout_confusion_matrix.png)

This demonstrates complete behavioral fidelity in learning the A3 triggering boundary across unseen simulation seeds.

---

## 8. Model Integration and Controller Architecture

### 8.1 Native C++ Tree Traversal Engine
To avoid external inter-process communication (IPC) overhead, socket latency, or Python GIL contention during discrete-event execution, the trained forest was serialized into portable JSON (`model/rf_model.json`) and compiled directly into a C++ header (`scratch/rf-model-generated.h`).

The native C++ inference engine traverses 30 trees in memory. Exact probability parity between Python scikit-learn and the C++ engine was verified across all samples:
$$\max |P_{\text{sklearn}}(y=1|X) - P_{\text{C++}}(y=1|X)| = 0.0000000000 \times 10^0$$

### 8.2 Data Flow and Pending Handover Guard
The `RfHandoverAlgorithm` class subclasses `ns3::LteHandoverAlgorithm`. When a measurement report arrives via `DoReportUeMeas`, the controller:
1. Filters candidate neighbor cells and extracts the 8-dimensional feature vector.
2. Checks whether a handover was initiated for the same UE within a $2.0\text{ s}$ guard interval. If active, the report is suppressed to eliminate command duplication.
3. Computes the ensemble probability $P(y=1|X)$. If $P \ge 0.50$, it dispatches `m_handoverManagementSapUser->TriggerHandover(rnti, targetCellId)`.

![Figure 5: Data Flow Diagram of Random Forest Decision Pipeline](results/plots/data_flow_diagram.png)

---

## 9. Experimental Setup and Multi-Seed Matrix

The experimental matrix comprises 20 independent simulation runs conducted on held-out seeds 6 through 10:
- **Policies**: 3GPP Event A3 Baseline vs. Native Random Forest Controller.
- **Mobility Speeds**: $30\text{ km/h}$ ($240\text{ s}$ duration) and $120\text{ km/h}$ ($60\text{ s}$ duration).
- **Seeds**: 6, 7, 8, 9, 10.
- **Traffic**: Uniform downlink UDP stream ($1200\text{ B}$ / $10\text{ ms}$ = $0.96\text{ Mbps}$).

---

## 10. Metrics and Mathematical Definitions

1. **Handover Attempts ($N_{att}$)**: Number of handover executions initiated by the controller via the Handover Management SAP.
2. **Handover Successes ($N_{succ}$)**: Successfully completed handovers verified by the target eNodeB RRC callback `HandoverEndOk`.
3. **Handover Failures ($N_{fail}$)**: Deduplicated control-plane failures tracked via eNodeB callbacks (`HandoverFailureNoPreamble`, `HandoverFailureMaxRach`, `HandoverFailureLeaving`, `HandoverFailureJoining`) and UE callback `HandoverEndError`.
4. **Ping-Pong Events ($N_{pp}$)**: Defined as a handover sequence from Cell A to Cell B followed by a return handover to Cell A ($A \to B \to A$) within a observation window of $\Delta t \le 5.0\text{ s}$.
5. **Throughput ($Th$)**: Average downlink application throughput calculated from received packets:
   $$Th = \frac{N_{rx\_bytes} \cdot 8}{t_{sim} \cdot 10^6} \quad [\text{Mbps}]$$
6. **Packet Delivery Ratio (PDR)**: Ratio of received packets to transmitted packets traced directly at the application boundary:
   $$\text{PDR} = \frac{N_{rx\_packets}}{N_{tx\_packets}} \times 100\%$$
7. **Handover-Associated DL Packet Reception Gap**: Time interval elapsed between the last downlink packet received before handover initiation and the first packet received after target cell attachment:
   $$\Delta t_{gap} = t_{first\_post\_rx} - t_{last\_pre\_rx} \quad [\text{seconds}]$$
   *Note: This metric is explicitly defined as an application-layer operational proxy rather than a physical-layer hardware interruption measurement.*

---

## 11. Results and Visualizations

Across all 20 held-out evaluation runs, every experiment completed successfully with valid per-run CSV traces.

### 11.1 Handover Control-Plane Performance Summary

| Policy | Speed (km/h) | HO Attempts | HO Successes | HO Failures | Ping-Pong Events | Mean DL Reception Gap (s) |
|---|---|---|---|---|---|---|
| **A3** | 30 | 2.00 +/- 0.00 | 2.00 +/- 0.00 | 0.00 +/- 0.00 | 0.00 +/- 0.00 | 0.0340 +/- 0.0338 |
| **A3** | 120 | 2.00 +/- 0.00 | 2.00 +/- 0.00 | 0.00 +/- 0.00 | 0.00 +/- 0.00 | 0.0600 +/- 0.0156 |
| **RF** | 30 | 2.00 +/- 0.00 | 2.00 +/- 0.00 | 0.00 +/- 0.00 | 0.00 +/- 0.00 | 0.0310 +/- 0.0305 |
| **RF** | 120 | 2.00 +/- 0.00 | 2.00 +/- 0.00 | 0.00 +/- 0.00 | 0.00 +/- 0.00 | 0.0580 +/- 0.0175 |

### 11.2 Downlink User-Plane Data Delivery Summary

| Policy | Speed (km/h) | TX Packets | RX Packets | Packet Loss | PDR (%) | Throughput (Mbps) |
|---|---|---|---|---|---|---|
| **A3** | 30 | 23990 +/- 0 | 23975.2 +/- 5.8 | 14.8 +/- 5.8 | 99.938 +/- 0.024 | 0.9590 +/- 0.0002 |
| **A3** | 120 | 5990 +/- 0 | 5966.8 +/- 7.3 | 23.2 +/- 7.3 | 99.613 +/- 0.122 | 0.9547 +/- 0.0012 |
| **RF** | 30 | 23990 +/- 0 | 23976.8 +/- 5.5 | 13.2 +/- 5.5 | 99.945 +/- 0.023 | 0.9591 +/- 0.0002 |
| **RF** | 120 | 5990 +/- 0 | 5957.4 +/- 4.9 | 32.6 +/- 4.9 | 99.456 +/- 0.082 | 0.9532 +/- 0.0008 |

![Figure 6: Downlink Application Throughput Comparison](results/plots/kpi_throughput_comparison.png)

![Figure 7: Packet Delivery Ratio Comparison](results/plots/kpi_pdr_comparison.png)

![Figure 8: Handover-Associated DL Packet Reception Gap Comparison](results/plots/kpi_rx_gap_comparison.png)

![Figure 9: Time-Series Throughput Dynamics at 120 km/h](results/plots/timeseries_throughput_120kmh.png)

---

## 12. Discussion

1. **Handover Reliability**:
   Both policies achieved 100% handover success across all seeds and speeds. In every run, exactly two handovers occurred ($1 \to 2$ and $2 \to 3$), matching the corridor geometry. Zero handover failures were recorded.
2. **Ping-Pong Suppression**:
   Zero ping-pong events occurred under the 5.0-second window. The combination of directional velocity, Friis path loss progression, and the 2.0-second pending handover guard interval prevented oscillations.
3. **Throughput and Packet Delivery**:
   Downlink application throughput was sustained near the theoretical 0.96 Mbps maximum ($0.959\text{ Mbps}$ at $30\text{ km/h}$; $0.953$–$0.954\text{ Mbps}$ at $120\text{ km/h}$). Packet delivery ratios exceeded $99.45\%$ under high-speed conditions.
4. **Packet Reception Gap Dynamics**:
   The Random Forest controller exhibited slightly shorter mean packet reception gaps ($31.0\text{ ms}$ vs $34.0\text{ ms}$ at $30\text{ km/h}$; $58.0\text{ ms}$ vs $60.0\text{ ms}$ at $120\text{ km/h}$). The Random Forest evaluates incoming measurement reports every $240\text{ ms}$ and triggers execution immediately once posterior probability exceeds threshold, whereas Event A3 waits for its continuous TimeToTrigger timer.

---

## 13. Technical Limitations

1. **Application-Layer Reception Gap Proxy**:
   The measured packet reception gap represents the inter-arrival latency at the UDP application layer. It serves as an operational proxy rather than a physical-layer user-plane interruption duration, as it incorporates scheduling latency and buffer draining delays.
2. **Imitation Learning Scope**:
   The Random Forest model was trained on ground-truth decisions generated by the 3GPP Event A3 baseline. Consequently, high accuracy reflects behavioral imitation rather than unconstrained policy superiority.
3. **Propagation Environment**:
   The simulation uses the standard free-space Friis propagation loss model. Urban canyon multipath fading, shadow fading, and building blockages were not simulated.

---

## 14. Conclusion

This investigation delivered an end-to-end, reproducible cellular handover evaluation framework in ns-3.48 for Problem Statement PS-016. By implementing a native C++ Random Forest controller subclassing `ns3::LteHandoverAlgorithm` with bit-level parity to scikit-learn, the framework demonstrated that machine learning policies can reliably govern discrete-event handover decisions. Benchmarking across 20 independent simulation runs on strictly held-out seeds confirmed that the learned policy faithfully captures the stability and reliability of the 3GPP Event A3 standard, sustaining $99.46\%$–$99.95\%$ packet delivery ratios with zero failures and zero ping-pongs up to $120\text{ km/h}$.

---

## 15. Reproducibility Instructions

All experiments and deliverables can be reproduced from the project directory:

```bash
# 1. Build ns-3.48 simulator
cd /home/arjya/projects/lostpacket/ns-3.48
./ns3 build

# 2. Collect genuine training data across seeds 1-5
./scripts/collect_training_data.sh

# 3. Train Random Forest model, verify inference parity, and export headers
python3 scripts/train_rf.py

# 4. Generate architectural and data flow diagrams
python3 scripts/generate_diagrams.py

# 5. Collect held-out evaluation dataset across seeds 6-10
./scripts/collect_heldout_data.sh

# 6. Evaluate Random Forest model offline on held-out seeds
python3 scripts/eval_rf_heldout.py

# 7. Execute 20-run held-out evaluation matrix (seeds 6-10)
./scripts/run_evaluation.sh

# 8. Compute aggregate statistics and generate KPI figures
python3 scripts/analyze_results.py

# 9. Recompile PDF Technical Report
python3 scripts/generate_pdf_report.py

# 10. Run live demonstration from root
cd /home/arjya/projects/lostpacket
./demo.sh
```

---

## References

1. 3GPP TS 36.331: *Evolved Universal Terrestrial Radio Access (E-UTRA); Radio Resource Control (RRC); Protocol specification*, Release 16, 2020.
2. 3GPP TS 36.133: *E-UTRA; Requirements for support of radio resource management*, Release 16, 2020.
3. ns-3 Consortium: *ns-3 Model Library — LTE Module Documentation*, Release 3.48, 2024.
4. L. Breiman: *Random Forests*, Machine Learning, Vol. 45, No. 1, pp. 5–32, 2001.
5. F. Pedregosa et al.: *Scikit-learn: Machine Learning in Python*, Journal of Machine Learning Research, Vol. 12, pp. 2825–2830, 2011.
