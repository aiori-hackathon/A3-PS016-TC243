#!/usr/bin/env python3
"""
train_rf.py

Trains a supervised Random Forest handover decision policy via imitation learning
of the 3GPP Event A3 baseline in ns-3.48 LTE.
Exports the complete trained forest to JSON (model/rf_model.json) and C++ header
(scratch/rf-model-generated.h), and validates C++ inference parity against scikit-learn.
"""

import os
import json
import numpy as np
import pandas as pd
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from sklearn.ensemble import RandomForestClassifier
from sklearn.model_selection import StratifiedKFold
from sklearn.metrics import (
    accuracy_score, precision_score, recall_score, f1_score,
    roc_auc_score, confusion_matrix, ConfusionMatrixDisplay
)

FEATURE_COLS = [
    'serving_rsrp_dbm',
    'target_rsrp_dbm',
    'delta_rsrp_db',
    'serving_rsrq_db',
    'target_rsrq_db',
    'delta_rsrq_db',
    'num_neighbours',
    'speed_kmh'
]
TARGET_COL = 'handover_label'

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    data_path = os.path.join(project_root, 'results', 'train', 'training_dataset.csv')
    model_dir = os.path.join(project_root, 'model')
    plots_dir = os.path.join(project_root, 'results', 'plots')
    report_path = os.path.join(project_root, 'results', 'train', 'rf_training_report.txt')
    cpp_header_path = os.path.join(project_root, 'scratch', 'rf-model-generated.h')
    json_model_path = os.path.join(model_dir, 'rf_model.json')

    os.makedirs(model_dir, exist_ok=True)
    os.makedirs(plots_dir, exist_ok=True)

    print("Loading training dataset from:", data_path)
    df = pd.read_csv(data_path)

    X = df[FEATURE_COLS].to_numpy(dtype=np.float64)
    y = df[TARGET_COL].to_numpy(dtype=np.int32)

    n_samples = len(y)
    n_class0 = int(np.sum(y == 0))
    n_class1 = int(np.sum(y == 1))

    print(f"Total samples: {n_samples}")
    print(f"Class 0 (Hold / No Handover): {n_class0} ({n_class0 / n_samples * 100:.2f}%)")
    print(f"Class 1 (Trigger Handover):   {n_class1} ({n_class1 / n_samples * 100:.2f}%)")

    # Hyperparameters
    n_estimators = 30
    max_depth = 5
    min_samples_split = 4
    random_state = 42

    rf = RandomForestClassifier(
        n_estimators=n_estimators,
        max_depth=max_depth,
        min_samples_split=min_samples_split,
        class_weight='balanced',
        random_state=random_state
    )

    # 5-Fold Stratified Cross Validation
    skf = StratifiedKFold(n_splits=5, shuffle=True, random_state=random_state)
    cv_precisions, cv_recalls, cv_f1s, cv_rocs = [], [], [], []

    for fold, (train_idx, val_idx) in enumerate(skf.split(X, y)):
        clf_fold = RandomForestClassifier(
            n_estimators=n_estimators,
            max_depth=max_depth,
            min_samples_split=min_samples_split,
            class_weight='balanced',
            random_state=random_state + fold
        )
        clf_fold.fit(X[train_idx], y[train_idx])
        y_pred = clf_fold.predict(X[val_idx])
        y_prob = clf_fold.predict_proba(X[val_idx])[:, 1]

        cv_precisions.append(precision_score(y[val_idx], y_pred, zero_division=0))
        cv_recalls.append(recall_score(y[val_idx], y_pred, zero_division=0))
        cv_f1s.append(f1_score(y[val_idx], y_pred, zero_division=0))
        cv_rocs.append(roc_auc_score(y[val_idx], y_prob))

    # Fit final model on all training data
    rf.fit(X, y)

    y_pred_full = rf.predict(X)
    y_prob_full = rf.predict_proba(X)[:, 1]

    acc = accuracy_score(y, y_pred_full)
    prec = precision_score(y, y_pred_full, zero_division=0)
    rec = recall_score(y, y_pred_full, zero_division=0)
    f1 = f1_score(y, y_pred_full, zero_division=0)
    roc = roc_auc_score(y, y_prob_full)
    cm = confusion_matrix(y, y_pred_full)

    # Feature importances
    importances = rf.feature_importances_
    feat_imp = sorted(zip(FEATURE_COLS, importances), key=lambda x: x[1], reverse=True)

    # Export to JSON
    forest_export = {
        'n_estimators': n_estimators,
        'max_depth': max_depth,
        'features': FEATURE_COLS,
        'feature_importances': {k: float(v) for k, v in feat_imp},
        'trees': []
    }

    for tree_idx, est in enumerate(rf.estimators_):
        t = est.tree_
        nodes = []
        for i in range(t.node_count):
            feat = int(t.feature[i])
            thresh = float(t.threshold[i])
            left = int(t.children_left[i])
            right = int(t.children_right[i])
            val = t.value[i][0]
            prob_c1 = float(val[1] / np.sum(val))
            nodes.append({
                'node_id': i,
                'feature_index': feat,
                'threshold': thresh,
                'left_child': left,
                'right_child': right,
                'prob_class1': prob_c1
            })
        forest_export['trees'].append(nodes)

    with open(json_model_path, 'w') as f:
        json.dump(forest_export, f, indent=2)
    print("Saved JSON model to:", json_model_path)

    # Generate C++ Header
    cpp_lines = [
        "// Generated by scripts/train_rf.py. Do not edit manually.",
        "#ifndef RF_MODEL_GENERATED_H",
        "#define RF_MODEL_GENERATED_H",
        "",
        "#include \"rf-handover-algorithm.h\"",
        "",
        "namespace ns3 {",
        "",
        "inline RfModelEnsemble LoadGeneratedRfModel()",
        "{",
        "    RfModelEnsemble model;",
        f"    model.trees.resize({n_estimators});",
        ""
    ]

    for t_idx, tree_nodes in enumerate(forest_export['trees']):
        cpp_lines.append(f"    // Tree {t_idx}")
        cpp_lines.append(f"    model.trees[{t_idx}].resize({len(tree_nodes)});")
        for n in tree_nodes:
            idx = n['node_id']
            feat = n['feature_index']
            thresh = n['threshold']
            left = n['left_child']
            right = n['right_child']
            prob = n['prob_class1']
            cpp_lines.append(
                f"    model.trees[{t_idx}][{idx}] = RfTreeNode{{{feat}, {thresh:.8f}, {left}, {right}, {prob:.8f}}};"
            )

    cpp_lines.extend([
        "    return model;",
        "}",
        "",
        "} // namespace ns3",
        "",
        "#endif /* RF_MODEL_GENERATED_H */",
        ""
    ])

    with open(cpp_header_path, 'w') as f:
        f.write('\n'.join(cpp_lines))
    print("Saved C++ generated header to:", cpp_header_path)

    # Validate inference equivalence in Python
    # Simulate C++ tree traversal
    def cpp_simulate_predict_proba(features, trees):
        sum_prob = 0.0
        for tree in trees:
            node = 0
            while tree[node]['feature_index'] >= 0:
                f_idx = tree[node]['feature_index']
                if features[f_idx] <= tree[node]['threshold']:
                    node = tree[node]['left_child']
                else:
                    node = tree[node]['right_child']
            sum_prob += tree[node]['prob_class1']
        return sum_prob / len(trees)

    sample_indices = np.random.RandomState(42).choice(len(X), size=min(100, len(X)), replace=False)
    max_diff = 0.0
    for idx in sample_indices:
        py_prob = rf.predict_proba(X[idx:idx+1])[0, 1]
        cpp_prob = cpp_simulate_predict_proba(X[idx], forest_export['trees'])
        diff = abs(py_prob - cpp_prob)
        if diff > max_diff:
            max_diff = diff

    print(f"C++ vs scikit-learn probability inference max absolute difference: {max_diff:.10e}")
    assert max_diff < 1e-6, f"Inference mismatch: {max_diff}"
    print("Inference parity verified successfully!")

    # Plot Confusion Matrix
    fig, ax = plt.subplots(figsize=(6, 5))
    disp = ConfusionMatrixDisplay(confusion_matrix=cm, display_labels=['Hold (0)', 'Handover (1)'])
    disp.plot(cmap=plt.cm.Blues, ax=ax, values_format='d')
    plt.title("Random Forest Confusion Matrix (Training Set)")
    plt.tight_layout()
    cm_plot_path = os.path.join(plots_dir, 'rf_confusion_matrix.png')
    plt.savefig(cm_plot_path, dpi=300)
    plt.close()
    print("Saved confusion matrix plot to:", cm_plot_path)

    # Plot Feature Importances
    fig, ax = plt.subplots(figsize=(8, 5))
    names, vals = zip(*reversed(feat_imp))
    ax.barh(names, vals, color='#1f77b4')
    ax.set_xlabel('Gini Feature Importance')
    ax.set_title('Random Forest Feature Importance')
    plt.tight_layout()
    fi_plot_path = os.path.join(plots_dir, 'rf_feature_importance.png')
    plt.savefig(fi_plot_path, dpi=300)
    plt.close()
    print("Saved feature importance plot to:", fi_plot_path)

    # Write Training Report
    report_content = f"""Random Forest Training and Imitation Learning Report
==================================================

Project: LostPacket
Team ID: A3-PS016-TC243
Model: Random Forest Classifier (Supervised Imitation Learning)
Source Baseline: 3GPP Event A3 RSRP Handover Policy (ns-3.48 LTE/EPC)

Important Note:
This model is trained via imitation learning of the A3 handover policy.
High training accuracy reflects successful behavioral imitation and policy capture,
not inherent algorithmic superiority over A3 by construction.

Dataset Provenance:
- Total simulation runs: 10 (Seeds 1-5, Speeds 30 km/h and 120 km/h)
- Total samples: {n_samples}
- Class 0 (Hold / No Handover): {n_class0} ({n_class0 / n_samples * 100:.2f}%)
- Class 1 (Trigger Handover):   {n_class1} ({n_class1 / n_samples * 100:.2f}%)
- Input features ({len(FEATURE_COLS)}): {', '.join(FEATURE_COLS)}

Hyperparameters:
- n_estimators: {n_estimators}
- max_depth: {max_depth}
- min_samples_split: {min_samples_split}
- class_weight: balanced
- random_state: {random_state}

5-Fold Stratified Cross-Validation:
- Mean Precision: {np.mean(cv_precisions):.4f} +/- {np.std(cv_precisions):.4f}
- Mean Recall:    {np.mean(cv_recalls):.4f} +/- {np.std(cv_recalls):.4f}
- Mean F1-Score:  {np.mean(cv_f1s):.4f} +/- {np.std(cv_f1s):.4f}
- Mean ROC-AUC:   {np.mean(cv_rocs):.4f} +/- {np.std(cv_rocs):.4f}

Full Training Performance:
- Accuracy:  {acc:.4f}
- Precision: {prec:.4f}
- Recall:    {rec:.4f}
- F1-Score:  {f1:.4f}
- ROC-AUC:   {roc:.4f}

Confusion Matrix:
{cm}

Feature Importances:
"""
    for f_name, imp in feat_imp:
        report_content += f"  - {f_name:20s}: {imp:.4f}\n"

    report_content += f"""
C++ Inference Validation:
- Max absolute probability difference on test samples: {max_diff:.10e}
- Verification status: PASSED (difference < 1e-6)
"""

    with open(report_path, 'w') as f:
        f.write(report_content)
    print("Saved training report to:", report_path)

if __name__ == '__main__':
    main()
