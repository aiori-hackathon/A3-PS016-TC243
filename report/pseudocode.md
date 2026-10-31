# Pseudocode: Random Forest LTE Handover Controller

Project: LostPacket
Team ID: A3-PS016-TC243
Problem Statement: PS-016 — Simulation-Based Handover Decision Evaluation for High-Mobility Users

This document specifies the exact algorithmic procedures governing measurement report processing, feature extraction, Random Forest ensemble inference, candidate cell selection, pending-handover state management, and command execution inside the ns-3.48 LTE discrete-event simulation environment.

---

## 1. Measurement Collection and Report Handling

```text
Algorithm 1: Measurement Collection and Handler Entry
---------------------------------------------------------------------------------------------------
Input : UE identity (RNTI rnti), Measurement results structure (MeasResults measResults)
Output: Handover trigger decision or no-operation (hold)

1: Procedure DoReportUeMeas(rnti, measResults):
2:     If measResults.measId not in m_measIds Then
3:         Return                                    // Ignore unrecognized measurement IDs
4:     End If
5:
6:     If Not measResults.haveMeasResultNeighCells Or measResults.measResultListEutra is empty Then
7:         Return                                    // No visible neighbor cells reported
8:     End If
9:
10:    // Extract serving cell radio channel state (PCell)
11:    serving_rsrp_dbm <- RsrpRange2Dbm(measResults.measResultPCell.rsrpResult)
12:    serving_rsrq_db  <- RsrqRange2Db(measResults.measResultPCell.rsrqResult)
13:
14:    // Proceed to candidate cell selection and feature construction
15:    (bestNeighbourCellId, features) <- ExtractFeaturesAndSelectCandidate(measResults, serving_rsrp_dbm, serving_rsrq_db)
16:
17:    If bestNeighbourCellId == 0 Then
18:        Return                                    // No valid neighbor candidate identified
19:    End If
20:
21:    // Proceed to pending state guard and policy execution
22:    EvaluateHandoverDecision(rnti, bestNeighbourCellId, features)
23: End Procedure
```

---

## 2. Candidate Selection and Feature Extraction

```text
Algorithm 2: Candidate Cell Selection and Feature Extraction
---------------------------------------------------------------------------------------------------
Input : measResults, serving_rsrp_dbm, serving_rsrq_db
Output: bestNeighbourCellId, feature vector X in R^8

1: Procedure ExtractFeaturesAndSelectCandidate(measResults, serving_rsrp_dbm, serving_rsrq_db):
2:     bestNeighbourCellId  <- 0
3:     bestNeighbourRsrpDbm <- -infinity
4:     bestNeighbourRsrqDb  <- -infinity
5:
6:     For Each n In measResults.measResultListEutra Do
7:         If n.haveRsrpResult Then
8:             nRsrpDbm <- RsrpRange2Dbm(n.rsrpResult)
9:             If nRsrpDbm > bestNeighbourRsrpDbm And IsValidNeighbour(n.physCellId) Then
10:                bestNeighbourCellId  <- n.physCellId
11:                bestNeighbourRsrpDbm <- nRsrpDbm
12:                If n.haveRsrqResult Then
13:                    bestNeighbourRsrqDb <- RsrqRange2Db(n.rsrqResult)
14:                Else
15:                    bestNeighbourRsrqDb <- serving_rsrq_db
16:                End If
17:            End If
18:        End If
19:    End For
20:
21:    If bestNeighbourCellId == 0 Then
22:        Return (0, empty_vector)
23:    End If
24:
25:    // Compute differential and contextual features
26:    delta_rsrp_db  <- bestNeighbourRsrpDbm - serving_rsrp_dbm
27:    delta_rsrq_db  <- bestNeighbourRsrqDb - serving_rsrq_db
28:    num_neighbours <- length(measResults.measResultListEutra)
29:
30:    // Construct unified 8-dimensional feature vector
31:    features <- [
32:        serving_rsrp_dbm,      // Feature 0
33:        bestNeighbourRsrpDbm,  // Feature 1
34:        delta_rsrp_db,         // Feature 2
35:        serving_rsrq_db,       // Feature 3
36:        bestNeighbourRsrqDb,   // Feature 4
37:        delta_rsrq_db,         // Feature 5
38:        num_neighbours,        // Feature 6
39:        m_speedKmh             // Feature 7
40:    ]
41:
42:    Return (bestNeighbourCellId, features)
43: End Procedure
```

---

## 3. Random Forest Inference and Decision Guard

```text
Algorithm 3: Random Forest Evaluation and Safe Handover Execution
---------------------------------------------------------------------------------------------------
Input : UE identity rnti, Candidate cell bestNeighbourCellId, Feature vector features
Output: Execution of X2 handover preparation or suppression

1: Procedure EvaluateHandoverDecision(rnti, bestNeighbourCellId, features):
2:     now <- Simulator::Now()
3:
4:     // Safe pending handover state check
5:     If rnti In m_pendingHandoverTime Then
6:         timeSinceLastTrigger <- now - m_pendingHandoverTime[rnti]
7:         If timeSinceLastTrigger < Seconds(2.0) Then
8:             Return                                // Suppress conflicting / repeated commands
9:         End If
10:    End If
11:
12:    If m_mode == "rf" Then
13:        // Evaluate ensemble of M decision trees
14:        prob_handover <- EvaluateForestProbability(features, s_rfModel)
15:
16:        If prob_handover >= m_rfThreshold Then    // Default threshold = 0.50
17:            // Update pending handover record
18:            m_pendingHandoverTime[rnti] <- now
19:
20:            // Dispatch handover command to eNodeB RRC via Handover Management SAP
21:            m_handoverManagementSapUser->TriggerHandover(rnti, bestNeighbourCellId)
22:        End If
23:
24:    Else If m_mode == "train" Then
25:        // Ground-truth A3 evaluation for dataset generation
26:        label <- EvaluateA3GroundTruth(rnti, bestNeighbourCellId, features[2], now)
27:        LogTrainingSample(now, rnti, features, bestNeighbourCellId, label)
28:
29:        If label == 1 Then
30:            m_pendingHandoverTime[rnti] <- now
31:            m_handoverManagementSapUser->TriggerHandover(rnti, bestNeighbourCellId)
32:        End If
33:    End If
34: End Procedure
```

---

## 4. Random Forest Tree Traversal Engine

```text
Algorithm 4: Ensemble Tree Traversal Engine (C++ Parity Implementation)
---------------------------------------------------------------------------------------------------
Input : Feature vector features, Forest ensemble model (M trees)
Output: Predicted probability of handover class P(y = 1 | features)

1: Function EvaluateForestProbability(features, model):
2:     If model.trees is empty Then
3:         Return 0.0
4:     End If
5:
6:     sum_prob <- 0.0
7:
8:     For Each tree In model.trees Do
9:         node_idx <- 0                             // Start at tree root node
10:        While tree[node_idx].featureIndex >= 0 Do // While current node is an internal split node
11:            feat_idx  <- tree[node_idx].featureIndex
12:            threshold <- tree[node_idx].threshold
13:            val       <- features[feat_idx]
14:
15:            If val <= threshold Then
16:                node_idx <- tree[node_idx].leftChild
17:            Else
18:                node_idx <- tree[node_idx].rightChild
19:            End If
20:        End While
21:
22:        // Reached leaf node: accumulate class 1 posterior probability
23:        sum_prob <- sum_prob + tree[node_idx].probClass1
24:    End For
25:
26:    prob_handover <- sum_prob / length(model.trees)
27:    Return prob_handover
28: End Function
```
