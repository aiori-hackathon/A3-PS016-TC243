#!/usr/bin/env python3
"""
generate_diagrams.py

Generates publication-quality architecture design and data flow diagrams
for the LostPacket high-mobility LTE handover evaluation framework.
"""

import os
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.patches as patches

def draw_architecture_diagram(out_path):
    fig, ax = plt.subplots(figsize=(10, 5.5), dpi=300)
    ax.set_xlim(0, 100)
    ax.set_ylim(0, 55)
    ax.axis('off')

    # Color palette
    c_ue = '#e2e8f0'
    c_enb = '#ebf8ff'
    c_epc = '#fefcbf'
    c_rf = '#feebc8'
    c_border = '#2b6cb0'

    # Title
    ax.text(50, 52, "LostPacket LTE Handover Simulation Architecture (ns-3.48)",
            ha='center', va='center', fontsize=12, fontweight='bold', color='#1a365d')

    # Remote Host and EPC Box
    epc_box = patches.FancyBboxPatch((4, 30), 22, 18, boxstyle="round,pad=0.5",
                                     ec="#b7791f", fc=c_epc, lw=1.5)
    ax.add_patch(epc_box)
    ax.text(15, 45, "Core Network (EPC)", ha='center', va='center', fontsize=9.5, fontweight='bold', color='#744210')
    ax.text(15, 40, "Remote UDP Host\n(1200 B / 10 ms = 0.96 Mbps)", ha='center', va='center', fontsize=8, color='#2d3748')
    ax.text(15, 34, "PGW / SGW / MME\n(PointToPointEpcHelper)", ha='center', va='center', fontsize=8, color='#4a5568')

    # 3 eNodeBs
    enbs = [
        ("eNodeB 1 (x = 0 m)\nServing Cell (Init)", 34),
        ("eNodeB 2 (x = 500 m)\nTarget 1 / Serving 2", 56),
        ("eNodeB 3 (x = 1000 m)\nTarget 2 / Final Cell", 78)
    ]

    for title, x_pos in enbs:
        enb_box = patches.FancyBboxPatch((x_pos - 9, 30), 18, 18, boxstyle="round,pad=0.5",
                                         ec=c_border, fc=c_enb, lw=1.5)
        ax.add_patch(enb_box)
        ax.text(x_pos, 45, title.split('\n')[0], ha='center', va='center', fontsize=8.5, fontweight='bold', color='#2b6cb0')
        ax.text(x_pos, 41, title.split('\n')[1], ha='center', va='center', fontsize=7.5, color='#4a5568')
        ax.text(x_pos, 35, "LteEnbRrc\nRrFfMacScheduler\nTxPower: 46 dBm", ha='center', va='center', fontsize=7.5, color='#2d3748')

    # X2 Links between eNodeBs
    ax.annotate("", xy=(45, 39), xytext=(47, 39),
                arrowprops=dict(arrowstyle="<->", color="#c53030", lw=2))
    ax.text(46, 41.5, "X2 Link", ha='center', va='center', fontsize=7.5, color="#c53030", fontweight='bold')

    ax.annotate("", xy=(67, 39), xytext=(69, 39),
                arrowprops=dict(arrowstyle="<->", color="#c53030", lw=2))
    ax.text(68, 41.5, "X2 Link", ha='center', va='center', fontsize=7.5, color="#c53030", fontweight='bold')

    # S1-U / S1-MME Link from PGW to eNB1
    ax.annotate("", xy=(26, 39), xytext=(25, 39),
                arrowprops=dict(arrowstyle="<->", color="#b7791f", lw=1.5))
    ax.text(25.5, 41.5, "S1 Link", ha='center', va='center', fontsize=7.5, color="#b7791f", fontweight='bold')

    # Mobile UE Box
    ue_box = patches.FancyBboxPatch((32, 10), 38, 12, boxstyle="round,pad=0.5",
                                    ec="#4a5568", fc=c_ue, lw=1.5)
    ax.add_patch(ue_box)
    ax.text(51, 19, "High-Mobility Vehicular UE (x: -100 m -> +1900 m)",
            ha='center', va='center', fontsize=9, fontweight='bold', color='#1a202c')
    ax.text(51, 15, "Speed: 30 km/h (8.33 m/s) or 120 km/h (33.33 m/s)\nRadio Model: Friis Propagation Loss | MultiModelSpectrumChannel",
            ha='center', va='center', fontsize=7.5, color='#2d3748')
    ax.text(51, 12, "LteUeRrc | PacketSink (Port 9000)", ha='center', va='center', fontsize=7.5, color='#4a5568')

    # Wireless link arrows from eNBs to UE
    ax.annotate("", xy=(34, 22), xytext=(34, 30),
                arrowprops=dict(arrowstyle="<->", color="#319795", lw=1.5, ls="--"))
    ax.annotate("", xy=(51, 22), xytext=(56, 30),
                arrowprops=dict(arrowstyle="<->", color="#319795", lw=1.5, ls="--"))
    ax.annotate("", xy=(68, 22), xytext=(78, 30),
                arrowprops=dict(arrowstyle="<->", color="#319795", lw=1.5, ls="--"))
    ax.text(51, 25, "LTE Uu Air Interface (EARFCN 100 DL / 19100 UL)",
            ha='center', va='center', fontsize=8, color="#319795", fontweight='bold')

    # RF Handover Controller Box
    ctrl_box = patches.FancyBboxPatch((72, 5), 26, 18, boxstyle="round,pad=0.5",
                                      ec="#dd6b20", fc=c_rf, lw=1.5)
    ax.add_patch(ctrl_box)
    ax.text(85, 20.5, "Handover Controller Layer", ha='center', va='center', fontsize=8.5, fontweight='bold', color='#9c4221')
    ax.text(85, 16.5, "- A3: A3RsrpHandoverAlgorithm\n  (Hyst = 3 dB, TTT = 256 ms)", ha='center', va='center', fontsize=7.5, color='#2d3748')
    ax.text(85, 11, "- RF: RfHandoverAlgorithm (Native C++)\n  (30 Trees, 8 Features, Guard 2.0s)", ha='center', va='center', fontsize=7.5, color='#9c4221', fontweight='bold')
    ax.text(85, 7, "Handover Management SAP", ha='center', va='center', fontsize=7.5, color='#4a5568')

    # Arrow from eNB to Controller
    ax.annotate("", xy=(85, 23), xytext=(85, 30),
                arrowprops=dict(arrowstyle="<->", color="#dd6b20", lw=1.5))
    ax.text(85, 26.5, "SAP Interface", ha='center', va='center', fontsize=7, color="#dd6b20", fontweight='bold')

    plt.tight_layout()
    plt.savefig(out_path, dpi=300, bbox_inches='tight')
    plt.close()
    print("Saved architecture diagram to:", out_path)

def draw_data_flow_diagram(out_path):
    fig, ax = plt.subplots(figsize=(10, 5.5), dpi=300)
    ax.set_xlim(0, 100)
    ax.set_ylim(0, 55)
    ax.axis('off')

    # Title
    ax.text(50, 52, "LostPacket Data Flow Diagram (DFD): RF Handover Decision Pipeline",
            ha='center', va='center', fontsize=12, fontweight='bold', color='#1a365d')

    boxes = [
        ("1. UE Measurement\nReporting", "UE sends RRC MeasResults\n(serving RSRP/RSRQ,\nneighbor list) every 240 ms", 12, 34, "#e2e8f0", "#4a5568"),
        ("2. Candidate Filter &\nFeature Extractor", "Extract 8 features:\n[RSRP_s, RSRP_t, dRSRP,\nRSRQ_s, RSRQ_t, dRSRQ, n, v]", 36, 34, "#ebf8ff", "#2b6cb0"),
        ("3. C++ Random Forest\nInference Engine", "Evaluate 30 decision trees\nCalculate posterior probability\nP(HO | features)", 62, 34, "#feebc8", "#dd6b20"),
        ("4. Decision Guard &\nThreshold Check", "Check P(HO) >= 0.50 and\ntime_since_last_trigger >= 2.0s\nPrevent command spam", 86, 34, "#fefcbf", "#b7791f")
    ]

    for title, desc, x, y, bg, ec in boxes:
        box = patches.FancyBboxPatch((x - 10, y - 9), 20, 18, boxstyle="round,pad=0.5",
                                     ec=ec, fc=bg, lw=1.5)
        ax.add_patch(box)
        ax.text(x, y + 5, title, ha='center', va='center', fontsize=8.5, fontweight='bold', color=ec)
        ax.text(x, y - 2, desc, ha='center', va='center', fontsize=7.2, color='#2d3748')

    # Flow arrows across boxes
    ax.annotate("", xy=(25, 34), xytext=(23, 34),
                arrowprops=dict(arrowstyle="->", color="#2b6cb0", lw=2))
    ax.annotate("", xy=(51, 34), xytext=(49, 34),
                arrowprops=dict(arrowstyle="->", color="#dd6b20", lw=2))
    ax.annotate("", xy=(75, 34), xytext=(73, 34),
                arrowprops=dict(arrowstyle="->", color="#b7791f", lw=2))

    # Output execution box
    box_exec = patches.FancyBboxPatch((28, 8), 44, 13, boxstyle="round,pad=0.5",
                                      ec="#276749", fc="#c6f6d5", lw=1.5)
    ax.add_patch(box_exec)
    ax.text(50, 16.5, "5. Handover Trigger Execution (X2 Interface)",
            ha='center', va='center', fontsize=9, fontweight='bold', color='#22543d')
    ax.text(50, 11.5, "m_handoverManagementSapUser->TriggerHandover(rnti, bestNeighbourCellId)\nInitiates X2 Handover Preparation -> Reconfiguration -> Path Switch -> Buffer Forwarding",
            ha='center', va='center', fontsize=7.5, color='#234e52')

    # Arrow from Decision Guard to Handover Trigger Execution
    ax.annotate("", xy=(70, 20), xytext=(86, 25),
                arrowprops=dict(arrowstyle="->", color="#22543d", lw=2, connectionstyle="angle,angleA=0,angleB=90,rad=5"))
    ax.text(80, 21.5, "Decision = 1 (Handover)", ha='center', va='center', fontsize=7.5, color="#22543d", fontweight='bold')

    # Suppressed / Hold loop
    ax.annotate("", xy=(86, 44), xytext=(86, 48),
                arrowprops=dict(arrowstyle="-", color="#718096", lw=1.5))
    ax.annotate("", xy=(12, 48), xytext=(86, 48),
                arrowprops=dict(arrowstyle="-", color="#718096", lw=1.5))
    ax.annotate("", xy=(12, 44), xytext=(12, 48),
                arrowprops=dict(arrowstyle="->", color="#718096", lw=1.5))
    ax.text(50, 50, "Decision = 0 (Hold) or Guard Suppressed: Await next measurement report",
            ha='center', va='center', fontsize=7.5, color="#718096", style='italic')

    plt.tight_layout()
    plt.savefig(out_path, dpi=300, bbox_inches='tight')
    plt.close()
    print("Saved data flow diagram to:", out_path)

if __name__ == '__main__':
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    plots_dir = os.path.join(project_root, 'results', 'plots')
    os.makedirs(plots_dir, exist_ok=True)

    draw_architecture_diagram(os.path.join(plots_dir, 'architecture_uml_diagram.png'))
    draw_data_flow_diagram(os.path.join(plots_dir, 'data_flow_diagram.png'))
