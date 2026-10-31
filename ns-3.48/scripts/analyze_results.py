#!/usr/bin/env python3
"""
analyze_results.py

Processes held-out evaluation results across seeds 6-10 for A3 and Random Forest policies
at 30 km/h and 120 km/h. Computes statistical aggregates (mean, std), generates comparison
tables, and produces high-resolution visualization plots.
"""

import os
import pandas as pd
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    eval_csv = os.path.join(project_root, 'results', 'eval', 'evaluation_summary.csv')
    plots_dir = os.path.join(project_root, 'results', 'plots')
    out_table_csv = os.path.join(project_root, 'results', 'eval', 'aggregate_comparison_table.csv')
    out_table_md = os.path.join(project_root, 'results', 'eval', 'aggregate_comparison_table.md')

    os.makedirs(plots_dir, exist_ok=True)

    print("Loading evaluation data from:", eval_csv)
    df = pd.read_csv(eval_csv)

    # Derived metrics
    df['packet_delivery_ratio_pct'] = (df['rx_packets'] / df['tx_packets']) * 100.0
    df['packet_loss'] = df['tx_packets'] - df['rx_packets']

    # Group by Policy and Speed
    grouped = df.groupby(['policy', 'speed_kmh'])

    metrics = [
        'ho_attempts',
        'ho_successes',
        'ho_failures',
        'ping_pong',
        'packet_delivery_ratio_pct',
        'packet_loss',
        'mean_throughput_mbps',
        'mean_ho_rx_gap_s'
    ]

    agg_means = grouped[metrics].mean()
    agg_stds = grouped[metrics].std()

    # Build structured summary table
    summary_rows = []
    for (pol, spd), row_mean in agg_means.iterrows():
        row_std = agg_stds.loc[(pol, spd)]
        summary_rows.append({
            'Policy': pol.upper(),
            'Speed (km/h)': spd,
            'Runs': len(df[(df['policy'] == pol) & (df['speed_kmh'] == spd)]),
            'HO Attempts': f"{row_mean['ho_attempts']:.2f} +/- {row_std['ho_attempts']:.2f}",
            'HO Successes': f"{row_mean['ho_successes']:.2f} +/- {row_std['ho_successes']:.2f}",
            'HO Failures': f"{row_mean['ho_failures']:.2f} +/- {row_std['ho_failures']:.2f}",
            'Ping-Pong': f"{row_mean['ping_pong']:.2f} +/- {row_std['ping_pong']:.2f}",
            'PDR (%)': f"{row_mean['packet_delivery_ratio_pct']:.3f} +/- {row_std['packet_delivery_ratio_pct']:.3f}",
            'Packet Loss': f"{row_mean['packet_loss']:.1f} +/- {row_std['packet_loss']:.1f}",
            'Throughput (Mbps)': f"{row_mean['mean_throughput_mbps']:.4f} +/- {row_std['mean_throughput_mbps']:.4f}",
            'Mean DL Gap (s)': f"{row_mean['mean_ho_rx_gap_s']:.4f} +/- {row_std['mean_ho_rx_gap_s']:.4f}"
        })

    summary_df = pd.DataFrame(summary_rows)
    summary_df.to_csv(out_table_csv, index=False)
    print("Saved aggregate comparison CSV to:", out_table_csv)

    # Save Markdown Table (custom simple formatter to avoid tabulate dependency)
    headers = list(summary_df.columns)
    header_line = "| " + " | ".join(headers) + " |"
    separator_line = "| " + " | ".join(["---"] * len(headers)) + " |"
    row_lines = []
    for _, r in summary_df.iterrows():
        row_lines.append("| " + " | ".join(str(r[c]) for c in headers) + " |")

    with open(out_table_md, 'w') as f:
        f.write("# Aggregate Evaluation Summary (Held-Out Seeds 6-10)\n\n")
        f.write(header_line + "\n")
        f.write(separator_line + "\n")
        f.write("\n".join(row_lines) + "\n")
        f.write("\n\nNote: Mean DL Gap denotes the handover-associated DL packet reception gap.\n")
    print("Saved Markdown table to:", out_table_md)

    # 1. Plot Throughput Comparison Bar Chart
    plt.figure(figsize=(7, 5))
    speeds = [30, 120]
    bar_width = 0.35
    x = np.arange(len(speeds))

    a3_tputs = [agg_means.loc[('a3', spd), 'mean_throughput_mbps'] for spd in speeds]
    a3_tput_stds = [agg_stds.loc[('a3', spd), 'mean_throughput_mbps'] for spd in speeds]

    rf_tputs = [agg_means.loc[('rf', spd), 'mean_throughput_mbps'] for spd in speeds]
    rf_tput_stds = [agg_stds.loc[('rf', spd), 'mean_throughput_mbps'] for spd in speeds]

    plt.bar(x - bar_width/2, a3_tputs, yerr=a3_tput_stds, width=bar_width, label='A3 Baseline', capsize=5, color='#1f77b4')
    plt.bar(x + bar_width/2, rf_tputs, yerr=rf_tput_stds, width=bar_width, label='Random Forest', capsize=5, color='#ff7f0e')

    plt.xlabel('Mobility Speed (km/h)')
    plt.ylabel('Mean Throughput (Mbps)')
    plt.title('Throughput Comparison across Held-Out Seeds (6-10)')
    plt.xticks(x, [f'{spd} km/h' for spd in speeds])
    plt.ylim(0.90, 0.98)
    plt.legend()
    plt.grid(axis='y', linestyle='--', alpha=0.6)
    plt.tight_layout()
    plt.savefig(os.path.join(plots_dir, 'kpi_throughput_comparison.png'), dpi=300)
    plt.close()

    # 2. Plot Packet Reception Gap Comparison Bar Chart
    plt.figure(figsize=(7, 5))
    a3_gaps = [agg_means.loc[('a3', spd), 'mean_ho_rx_gap_s'] * 1000.0 for spd in speeds]
    a3_gap_stds = [agg_stds.loc[('a3', spd), 'mean_ho_rx_gap_s'] * 1000.0 for spd in speeds]

    rf_gaps = [agg_means.loc[('rf', spd), 'mean_ho_rx_gap_s'] * 1000.0 for spd in speeds]
    rf_gap_stds = [agg_stds.loc[('rf', spd), 'mean_ho_rx_gap_s'] * 1000.0 for spd in speeds]

    plt.bar(x - bar_width/2, a3_gaps, yerr=a3_gap_stds, width=bar_width, label='A3 Baseline', capsize=5, color='#1f77b4')
    plt.bar(x + bar_width/2, rf_gaps, yerr=rf_gap_stds, width=bar_width, label='Random Forest', capsize=5, color='#ff7f0e')

    plt.xlabel('Mobility Speed (km/h)')
    plt.ylabel('Mean Handover DL Packet Gap (ms)')
    plt.title('Handover Packet Reception Gap across Held-Out Seeds')
    plt.xticks(x, [f'{spd} km/h' for spd in speeds])
    plt.legend()
    plt.grid(axis='y', linestyle='--', alpha=0.6)
    plt.tight_layout()
    plt.savefig(os.path.join(plots_dir, 'kpi_rx_gap_comparison.png'), dpi=300)
    plt.close()

    # 3. Plot Packet Delivery Ratio Comparison
    plt.figure(figsize=(7, 5))
    a3_pdrs = [agg_means.loc[('a3', spd), 'packet_delivery_ratio_pct'] for spd in speeds]
    a3_pdr_stds = [agg_stds.loc[('a3', spd), 'packet_delivery_ratio_pct'] for spd in speeds]

    rf_pdrs = [agg_means.loc[('rf', spd), 'packet_delivery_ratio_pct'] for spd in speeds]
    rf_pdr_stds = [agg_stds.loc[('rf', spd), 'packet_delivery_ratio_pct'] for spd in speeds]

    plt.bar(x - bar_width/2, a3_pdrs, yerr=a3_pdr_stds, width=bar_width, label='A3 Baseline', capsize=5, color='#1f77b4')
    plt.bar(x + bar_width/2, rf_pdrs, yerr=rf_pdr_stds, width=bar_width, label='Random Forest', capsize=5, color='#ff7f0e')

    plt.xlabel('Mobility Speed (km/h)')
    plt.ylabel('Packet Delivery Ratio (%)')
    plt.title('Packet Delivery Ratio across Held-Out Seeds')
    plt.xticks(x, [f'{spd} km/h' for spd in speeds])
    plt.ylim(99.0, 100.1)
    plt.legend()
    plt.grid(axis='y', linestyle='--', alpha=0.6)
    plt.tight_layout()
    plt.savefig(os.path.join(plots_dir, 'kpi_pdr_comparison.png'), dpi=300)
    plt.close()

    # 4. Plot Time-Series Throughput for Seed 6 (120 km/h)
    a3_ts_path = os.path.join(project_root, 'results', 'eval', 'a3', '120kmh', 'seed6', 'per_second_throughput.csv')
    rf_ts_path = os.path.join(project_root, 'results', 'eval', 'rf', '120kmh', 'seed6', 'per_second_throughput.csv')

    if os.path.exists(a3_ts_path) and os.path.exists(rf_ts_path):
        a3_ts = pd.read_csv(a3_ts_path)
        rf_ts = pd.read_csv(rf_ts_path)

        plt.figure(figsize=(9, 4.5))
        plt.plot(a3_ts['time_s'], a3_ts['throughput_mbps'], label='A3 Baseline (Seed 6)', color='#1f77b4', linewidth=1.5)
        plt.plot(rf_ts['time_s'], rf_ts['throughput_mbps'], label='Random Forest (Seed 6)', color='#ff7f0e', linestyle='--', linewidth=1.5)

        # Mark handovers
        plt.axvline(x=12.5, color='gray', linestyle=':', label='Handover 1 (Cell 1->2)')
        plt.axvline(x=27.5, color='gray', linestyle=':', label='Handover 2 (Cell 2->3)')

        plt.xlabel('Simulation Time (s)')
        plt.ylabel('Instantaneous Throughput (Mbps)')
        plt.title('Time-Series Throughput Dynamics at 120 km/h')
        plt.legend(loc='lower left')
        plt.grid(True, linestyle='--', alpha=0.6)
        plt.tight_layout()
        plt.savefig(os.path.join(plots_dir, 'timeseries_throughput_120kmh.png'), dpi=300)
        plt.close()

    print("All plots generated successfully in:", plots_dir)

if __name__ == '__main__':
    main()
