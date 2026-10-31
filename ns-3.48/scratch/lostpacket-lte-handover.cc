/**
 * lostpacket-lte-handover.cc
 *
 * LTE handover simulation — LostPacket project, Stage 1
 *
 * Scenario
 * --------
 * Three LTE eNBs placed linearly at x = 0, 500, 1000 m.
 * One UE starts at x = -100 m and moves in the +x direction at constant velocity.
 * Downlink UDP traffic flows from a remote host through the EPC to the UE.
 *
 * Radio / propagation model
 * -------------------------
 * ns3::FriisPropagationLossModel (ns-3.48 LTE module default, set explicitly below).
 * Spectrum channel: MultiModelSpectrumChannel (LTE module default).
 * eNB Tx power: 46 dBm.
 * Scheduler: Round-Robin (ns3::RrFfMacScheduler).
 *
 * Handover policies
 * -----------------
 *   a3   — A3 RSRP (3GPP Event A3, Hysteresis 3 dB, TTT 256 ms). PS-016 baseline.
 *   noop — No automatic handover (for diagnostics only).
 *
 * Measured KPIs
 * -------------
 *   - Handover attempts (eNB-RRC HandoverStart)
 *   - Handover successes (eNB-RRC HandoverEndOk)
 *   - Handover failures (4 eNB-RRC failure traces, counted without double-counting)
 *   - Ping-pong count (definition: HO back to a cell left within 5 s)
 *   - Handover-associated DL packet reception gap (last RX before HO start to
 *     first RX after HO success, in seconds — NOT claimed to be exact user-plane
 *     interruption duration, which would require PDCP-layer timestamping)
 *   - DL packets sent (UdpClient::m_sent counter, actual application-layer count)
 *   - DL packets received (counted individually via PacketSink Rx trace, NOT via
 *     total-bytes / fixed-size, to avoid size-assumption errors)
 *   - DL bytes received (PacketSink::GetTotalRx())
 *   - Per-second throughput (Mbps)
 *   - Mean throughput over simulation
 *
 * Output
 * ------
 *   stdout: human-readable event log
 *   <outDir>/per_second_throughput.csv   — per-second throughput
 *   <outDir>/handover_events.csv         — HO start / success / failure events
 *   <outDir>/rx_packets.csv              — per-packet RX timestamps
 *   <outDir>/ho_rx_gap.csv               — per-HO packet reception gap
 *   <outDir>/summary.csv                 — one-row aggregate KPIs
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/lte-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

#include "rf-handover-algorithm.h"
#include "rf-model-generated.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace ns3;

NS_OBJECT_ENSURE_REGISTERED(RfHandoverAlgorithm);

NS_LOG_COMPONENT_DEFINE("LostPacketLteHandover");

// ---------------------------------------------------------------------------
// Global simulation state
// ---------------------------------------------------------------------------

static uint32_t g_hoAttempts = 0;    ///< Handover attempts (eNB HandoverStart)
static uint32_t g_hoSuccesses = 0;   ///< Handover successes (eNB HandoverEndOk)
static uint32_t g_hoFailures = 0;    ///< Total handover failures (all causes, per-event)
static uint32_t g_pingPongCount = 0; ///< Ping-pong handovers

/// Ping-pong detection state per UE (keyed by IMSI)
struct HoRecord
{
    double time;         ///< Wall-time of handover (simulation seconds)
    uint16_t fromCellId; ///< Source cell
    uint16_t toCellId;   ///< Target cell
};
static std::vector<HoRecord> g_hoHistory; ///< Completed handover history for ping-pong detection

/// Handover packet-reception-gap tracking
struct HoGapRecord
{
    uint32_t hoIndex;     ///< Index of handover attempt (1-based)
    double hoStartTime;   ///< Sim time of eNB HandoverStart event
    double hoSuccessTime; ///< Sim time of eNB HandoverEndOk event
    double lastPreRxTime; ///< Sim time of last DL packet RX before hoStartTime
    double firstPostRxTime; ///< Sim time of first DL packet RX after hoSuccessTime
    bool successSeen;     ///< Whether HandoverEndOk was observed
    bool preRxValid;      ///< Whether a pre-HO RX was seen
    bool postRxValid;     ///< Whether a post-HO RX was seen
};
static std::vector<HoGapRecord> g_hoGaps; ///< One entry per HO attempt

/// Per-second throughput tracking
static uint64_t g_lastRxBytes = 0;

/// Per-packet RX counter (actual packet count, not bytes/size)
static uint32_t g_rxPacketCount = 0;

/// Per-packet TX counter (driven by UdpClient TxWithAddresses trace)
static uint32_t g_txPacketCount = 0;

/// Last DL packet RX time (for gap measurement)
static double g_lastRxTime = -1.0;

/// Output file streams (opened in main)
static std::ofstream g_throughputCsv;
static std::ofstream g_eventsCsv;
static std::ofstream g_rxPacketCsv;
static std::ofstream g_gapCsv;

/// Ping-pong window in seconds (documented definition)
static constexpr double PINGPONG_WINDOW_S = 5.0;

// ---------------------------------------------------------------------------
// Ping-pong detection
// ---------------------------------------------------------------------------

/**
 * Check whether the completed handover (fromCell -> toCell at time t) constitutes
 * a ping-pong.
 *
 * Definition: a ping-pong occurs when the UE hands over from cell A to cell B
 * and then back from cell B to the same cell A within PINGPONG_WINDOW_S seconds.
 *
 * We check the most recent completed handover in g_hoHistory: if it went to
 * fromCell (i.e. toCellId of the previous record equals the current fromCell)
 * within the window, this is a ping-pong.
 */
static void
CheckPingPong(double t, uint16_t fromCell, uint16_t toCell)
{
    // Walk backwards through history looking for a handover to 'fromCell'
    // within the window that originated from 'toCell'
    for (auto it = g_hoHistory.rbegin(); it != g_hoHistory.rend(); ++it)
    {
        if ((t - it->time) > PINGPONG_WINDOW_S)
        {
            break;
        }
        // A ping-pong: previous HO went to fromCell (now we are leaving it)
        // and that previous HO came from toCell (we are now going back to it)
        if (it->toCellId == fromCell && it->fromCellId == toCell)
        {
            ++g_pingPongCount;
            std::cout << t << "s PING-PONG detected: cell " << fromCell << " -> " << toCell
                      << " (returned to " << toCell << " within " << (t - it->time) << "s)"
                      << std::endl;
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// eNB-side handover trace callbacks
// (eNB traces carry: imsi, cellId, rnti, [targetCellId for Start])
// ---------------------------------------------------------------------------

/**
 * eNB HandoverStart: (imsi, sourceCellId, rnti, targetCellId)
 * Counts handover attempts. Creates a gap-tracking record.
 */
void
EnbNotifyHandoverStart(std::string context,
                       uint64_t imsi,
                       uint16_t cellId,
                       uint16_t rnti,
                       uint16_t targetCellId)
{
    double t = Simulator::Now().GetSeconds();
    ++g_hoAttempts;

    std::cout << t << "s HO_ATTEMPT #" << g_hoAttempts << ": IMSI=" << imsi << " From=" << cellId
              << " To=" << targetCellId << std::endl;

    g_eventsCsv << "HO_START," << t << "," << imsi << "," << cellId << "," << targetCellId << "\n";

    // Open a gap record
    HoGapRecord rec{};
    rec.hoIndex = g_hoAttempts;
    rec.hoStartTime = t;
    rec.hoSuccessTime = 0.0;
    rec.lastPreRxTime = g_lastRxTime; // last RX seen before this callback
    rec.firstPostRxTime = 0.0;
    rec.successSeen = false;
    rec.preRxValid = (g_lastRxTime >= 0.0);
    rec.postRxValid = false;
    g_hoGaps.push_back(rec);
}

/**
 * eNB HandoverEndOk: (imsi, cellId, rnti) — cellId is the NEW cell
 * Counts successes. Records HO completion time for gap measurement.
 */
void
EnbNotifyHandoverEndOk(std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti)
{
    double t = Simulator::Now().GetSeconds();
    ++g_hoSuccesses;

    std::cout << t << "s HO_SUCCESS #" << g_hoSuccesses << ": IMSI=" << imsi
              << " NewCell=" << cellId << std::endl;

    g_eventsCsv << "HO_SUCCESS," << t << "," << imsi << "," << cellId << ",\n";

    // Mark the most recent open gap record as succeeded
    for (auto it = g_hoGaps.rbegin(); it != g_hoGaps.rend(); ++it)
    {
        if (!it->successSeen)
        {
            it->successSeen = true;
            it->hoSuccessTime = t;
            break;
        }
    }
}

/**
 * HandoverFailure callback — shared by all four eNB failure trace sources.
 * Signature: (imsi, rnti, cellId) as per LteEnbRrc::HandoverFailureTracedCallback.
 *
 * Each trace source fires independently for a different failure cause.
 * We count each firing as one failure event. Because the four traces represent
 * different failure stages (not the same event seen four times), there is no
 * double-counting within a single failure. However, a single handover failure
 * may trigger multiple traces (e.g., Leaving AND Joining). We track the
 * per-attempt failure to avoid over-counting total failed HOs.
 */
void
EnbNotifyHandoverFailure(std::string context,
                         uint64_t imsi,
                         uint16_t rnti,
                         uint16_t cellId,
                         const std::string& cause)
{
    double t = Simulator::Now().GetSeconds();

    // Only increment g_hoFailures once per unique HO attempt that fails.
    // Strategy: mark the most recent open gap record that has not already been
    // marked as failed. Each additional failure trace on the same HO attempt
    // is logged but does not re-increment the counter.
    bool newFailure = false;
    for (auto it = g_hoGaps.rbegin(); it != g_hoGaps.rend(); ++it)
    {
        if (!it->successSeen)
        {
            if (!newFailure)
            {
                // First failure cause seen for this attempt
                ++g_hoFailures;
                newFailure = true;
                it->successSeen = true; // Mark closed (failed)
            }
            break;
        }
    }

    std::cout << t << "s HO_FAILURE: IMSI=" << imsi << " Cell=" << cellId << " Cause=" << cause
              << std::endl;

    g_eventsCsv << "HO_FAILURE_" << cause << "," << t << "," << imsi << "," << cellId << ",\n";
}

void
EnbNotifyHandoverFailureNoPreamble(std::string context,
                                   uint64_t imsi,
                                   uint16_t rnti,
                                   uint16_t cellId)
{
    EnbNotifyHandoverFailure(context, imsi, rnti, cellId, "NoPreamble");
}

void
EnbNotifyHandoverFailureMaxRach(std::string context,
                                uint64_t imsi,
                                uint16_t rnti,
                                uint16_t cellId)
{
    EnbNotifyHandoverFailure(context, imsi, rnti, cellId, "MaxRach");
}

void
EnbNotifyHandoverFailureLeaving(std::string context,
                                uint64_t imsi,
                                uint16_t rnti,
                                uint16_t cellId)
{
    EnbNotifyHandoverFailure(context, imsi, rnti, cellId, "Leaving");
}

void
EnbNotifyHandoverFailureJoining(std::string context,
                                uint64_t imsi,
                                uint16_t rnti,
                                uint16_t cellId)
{
    EnbNotifyHandoverFailure(context, imsi, rnti, cellId, "Joining");
}

// ---------------------------------------------------------------------------
// UE-side handover trace callbacks
// (UE traces carry: imsi, cellId, rnti)
// ---------------------------------------------------------------------------

void
UeNotifyConnectionEstablished(std::string context,
                               uint64_t imsi,
                               uint16_t cellId,
                               uint16_t rnti)
{
    std::cout << Simulator::Now().GetSeconds() << "s UE_CONNECTED: IMSI=" << imsi
              << " CellId=" << cellId << " RNTI=" << rnti << std::endl;
}

void
UeNotifyHandoverStart(std::string context,
                      uint64_t imsi,
                      uint16_t cellId,
                      uint16_t rnti,
                      uint16_t targetCellId)
{
    // UE-side start (informational only; we count attempts from eNB side)
    std::cout << Simulator::Now().GetSeconds() << "s UE_HO_START: IMSI=" << imsi
              << " From=" << cellId << " To=" << targetCellId << std::endl;
}

void
UeNotifyHandoverEndOk(std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti)
{
    double t = Simulator::Now().GetSeconds();
    std::cout << t << "s UE_HO_SUCCESS: IMSI=" << imsi << " NewCell=" << cellId << std::endl;

    // Ping-pong check: find source cell from the most recent gap record
    if (!g_hoGaps.empty())
    {
        // The eNB HandoverStart tells us source; UE HandoverEndOk tells us new cell.
        // We recorded the gap with fromCell in EnbNotifyHandoverStart — find it.
        // For ping-pong we use the eNB-side history.
        for (auto it = g_hoGaps.rbegin(); it != g_hoGaps.rend(); ++it)
        {
            if (it->successSeen && it->hoSuccessTime == t)
            {
                // We can't recover fromCellId from gap record here (it's not stored).
                // Ping-pong check is done in EnbNotifyHandoverEndOk which has cellId context.
                break;
            }
        }
    }
}

void
UeNotifyHandoverEndError(std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti)
{
    double t = Simulator::Now().GetSeconds();
    std::cout << t << "s UE_HO_ERROR: IMSI=" << imsi << " Cell=" << cellId << std::endl;
    g_eventsCsv << "UE_HO_ERROR," << t << "," << imsi << "," << cellId << ",\n";
}

// ---------------------------------------------------------------------------
// Packet TX callback — counts actual application-layer packets sent
// ---------------------------------------------------------------------------

/**
 * Called by UdpClient TxWithAddresses trace for every packet transmitted.
 * This gives the true application-level TX count, without timing-order
 * ambiguity from querying GetTotalTx() after Simulator::Stop().
 */
void
DlPacketSent(Ptr<const Packet> packet, const Address& from, const Address& to)
{
    ++g_txPacketCount;
}

// ---------------------------------------------------------------------------
// Packet RX callback — counts actual packets (not inferred from bytes)
// ---------------------------------------------------------------------------

/**
 * Called by PacketSink Rx trace for every received packet.
 * Updates per-packet RX count and last-RX time (for gap measurement).
 * Also closes the post-HO gap window for the most recent completed HO.
 */
void
DlPacketReceived(Ptr<const Packet> packet, const Address& from)
{
    double t = Simulator::Now().GetSeconds();

    ++g_rxPacketCount;
    g_lastRxTime = t;

    g_rxPacketCsv << t << "," << packet->GetSize() << "\n";

    // Close post-HO gap for the most recently succeeded HO that hasn't captured a post-RX yet
    for (auto it = g_hoGaps.rbegin(); it != g_hoGaps.rend(); ++it)
    {
        if (it->successSeen && !it->postRxValid)
        {
            it->firstPostRxTime = t;
            it->postRxValid = true;
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Ping-pong tracking via eNB HandoverEndOk
// (We need fromCell here; use the eNB trace path which includes context)
// ---------------------------------------------------------------------------

/// Track the last source cell per UE for ping-pong detection
static uint16_t g_lastSourceCell = 0;
static uint16_t g_lastTargetCell = 0;

/**
 * Second pass on eNB HandoverEndOk — used only for ping-pong detection
 * because we need both source and target cell IDs at completion time.
 */
void
EnbNotifyHandoverEndOkPingPong(std::string context,
                               uint64_t imsi,
                               uint16_t cellId,
                               uint16_t rnti)
{
    double t = Simulator::Now().GetSeconds();

    // Find the most recent start record's source cell from g_hoGaps
    // (we store hoIndex in the gap record, not the fromCell/toCell pair)
    // For ping-pong we need source->target, which was logged at HandoverStart time.
    // We use a pair of module-level variables set at HandoverStart.
    if (g_lastSourceCell > 0 && g_lastTargetCell > 0)
    {
        CheckPingPong(t, g_lastSourceCell, g_lastTargetCell);
        HoRecord rec{t, g_lastSourceCell, g_lastTargetCell};
        g_hoHistory.push_back(rec);
        g_lastSourceCell = 0;
        g_lastTargetCell = 0;
    }
}

/**
 * Sets the pending source/target for ping-pong detection.
 * Connected alongside EnbNotifyHandoverStart.
 */
void
EnbNotifyHandoverStartPingPong(std::string context,
                               uint64_t imsi,
                               uint16_t cellId,
                               uint16_t rnti,
                               uint16_t targetCellId)
{
    g_lastSourceCell = cellId;
    g_lastTargetCell = targetCellId;
}

// ---------------------------------------------------------------------------
// Throughput reporting (per second)
// ---------------------------------------------------------------------------

void
ReportThroughput(Ptr<PacketSink> sink)
{
    double t = Simulator::Now().GetSeconds();
    uint64_t currentRxBytes = sink->GetTotalRx();
    uint64_t newBytes = currentRxBytes - g_lastRxBytes;
    double throughputMbps = (newBytes * 8.0) / 1e6; // over 1 s window

    std::cout << t << "s Throughput: " << throughputMbps << " Mbps" << std::endl;
    g_throughputCsv << t << "," << throughputMbps << "\n";

    g_lastRxBytes = currentRxBytes;
    Simulator::Schedule(Seconds(1.0), &ReportThroughput, sink);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int
main(int argc, char* argv[])
{
    // -----------------------------------------------------------------------
    // CLI arguments
    // -----------------------------------------------------------------------
    double speedKmh = 120.0;
    double simTime = 0.0; // 0 = auto-select based on speed
    uint32_t seed = 1;
    uint32_t run = 1;
    std::string policy = "a3";
    std::string outDir = "results/raw";
    std::string trainingFile = "results/train/samples.csv";
    double rfThreshold = 0.5;

    CommandLine cmd(__FILE__);
    cmd.AddValue("speedKmh", "UE speed in km/h (30 or 120)", speedKmh);
    cmd.AddValue("simTime", "Simulation duration in seconds (0 = auto)", simTime);
    cmd.AddValue("seed", "RNG seed (ns3::RngSeedManager::SetSeed)", seed);
    cmd.AddValue("run", "RNG run number (ns3::RngSeedManager::SetRun)", run);
    cmd.AddValue("policy", "Handover policy: a3 | noop | train | rf", policy);
    cmd.AddValue("outDir", "Output directory for CSV files", outDir);
    cmd.AddValue("trainingFile", "CSV file path for training samples", trainingFile);
    cmd.AddValue("rfThreshold", "Decision threshold for RF policy", rfThreshold);
    cmd.Parse(argc, argv);

    // -----------------------------------------------------------------------
    // RNG seeding
    // -----------------------------------------------------------------------
    RngSeedManager::SetSeed(seed);
    RngSeedManager::SetRun(run);

    // -----------------------------------------------------------------------
    // Derived parameters
    // -----------------------------------------------------------------------
    double speedMps = speedKmh / 3.6;

    // Auto sim time: UE travels from x=-100 to x=1200 (200m past last eNB).
    // Use a margin of 1.5x the minimum required time to ensure all HOs complete.
    if (simTime <= 0.0)
    {
        double minTime = 1300.0 / speedMps; // distance / speed
        simTime = minTime * 1.5;
        // Round up to nearest 10 s
        simTime = std::ceil(simTime / 10.0) * 10.0;
    }

    std::cout << "=== LostPacket LTE Handover Simulation ===" << std::endl;
    std::cout << "Policy:    " << policy << std::endl;
    std::cout << "Speed:     " << speedKmh << " km/h (" << speedMps << " m/s)" << std::endl;
    std::cout << "SimTime:   " << simTime << " s" << std::endl;
    std::cout << "Seed:      " << seed << std::endl;
    std::cout << "Run:       " << run << std::endl;
    std::cout << "OutDir:    " << outDir << std::endl;

    // -----------------------------------------------------------------------
    // Open output CSV files
    // -----------------------------------------------------------------------
    std::filesystem::create_directories(outDir);

    g_throughputCsv.open(outDir + "/per_second_throughput.csv");
    g_throughputCsv << "time_s,throughput_mbps\n";

    g_eventsCsv.open(outDir + "/handover_events.csv");
    g_eventsCsv << "event_type,time_s,imsi,cell_id,target_cell_id\n";

    g_rxPacketCsv.open(outDir + "/rx_packets.csv");
    g_rxPacketCsv << "time_s,size_bytes\n";

    g_gapCsv.open(outDir + "/ho_rx_gap.csv");
    // Note: 'gap_s' is the DL packet reception gap, not exact user-plane interruption
    g_gapCsv << "ho_index,ho_start_s,ho_success_s,last_pre_rx_s,first_post_rx_s,gap_s,gap_valid\n";

    // -----------------------------------------------------------------------
    // LTE / EPC helpers
    // -----------------------------------------------------------------------
    Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
    Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper>();

    lteHelper->SetEpcHelper(epcHelper);
    lteHelper->SetSchedulerType("ns3::RrFfMacScheduler");

    // Explicitly set the propagation loss model (ns-3.48 LTE default is FriisPropagationLossModel)
    // This ensures the radio model is unambiguous and reproducible.
    lteHelper->SetAttribute("PathlossModel",
                            TypeIdValue(TypeId::LookupByName("ns3::FriisPropagationLossModel")));

    // -----------------------------------------------------------------------
    // Handover algorithm
    // -----------------------------------------------------------------------
    if (policy == "a3")
    {
        lteHelper->SetHandoverAlgorithmType("ns3::A3RsrpHandoverAlgorithm");
        lteHelper->SetHandoverAlgorithmAttribute("Hysteresis", DoubleValue(3.0));
        lteHelper->SetHandoverAlgorithmAttribute("TimeToTrigger", TimeValue(MilliSeconds(256)));
    }
    else if (policy == "noop")
    {
        lteHelper->SetHandoverAlgorithmType("ns3::NoOpHandoverAlgorithm");
    }
    else if (policy == "train")
    {
        lteHelper->SetHandoverAlgorithmType("ns3::RfHandoverAlgorithm");
        lteHelper->SetHandoverAlgorithmAttribute("Mode", StringValue("train"));
        lteHelper->SetHandoverAlgorithmAttribute("TrainingOutFile", StringValue(trainingFile));
        lteHelper->SetHandoverAlgorithmAttribute("SpeedKmh", DoubleValue(speedKmh));
        lteHelper->SetHandoverAlgorithmAttribute("Hysteresis", DoubleValue(3.0));
        lteHelper->SetHandoverAlgorithmAttribute("TimeToTrigger", TimeValue(MilliSeconds(256)));
    }
    else if (policy == "rf")
    {
        RfHandoverAlgorithm::SetStaticModel(LoadGeneratedRfModel());
        lteHelper->SetHandoverAlgorithmType("ns3::RfHandoverAlgorithm");
        lteHelper->SetHandoverAlgorithmAttribute("Mode", StringValue("rf"));
        lteHelper->SetHandoverAlgorithmAttribute("SpeedKmh", DoubleValue(speedKmh));
        lteHelper->SetHandoverAlgorithmAttribute("RfThreshold", DoubleValue(rfThreshold));
    }
    else
    {
        NS_FATAL_ERROR("Unknown policy: " << policy << ". Supported: a3, noop, train, rf.");
    }

    // -----------------------------------------------------------------------
    // Nodes
    // -----------------------------------------------------------------------
    NodeContainer enbNodes;
    NodeContainer ueNodes;
    enbNodes.Create(3);
    ueNodes.Create(1);

    // -----------------------------------------------------------------------
    // eNB positions: linear deployment
    // -----------------------------------------------------------------------
    Ptr<ListPositionAllocator> enbPositionAlloc = CreateObject<ListPositionAllocator>();
    enbPositionAlloc->Add(Vector(0.0, 0.0, 0.0));
    enbPositionAlloc->Add(Vector(500.0, 0.0, 0.0));
    enbPositionAlloc->Add(Vector(1000.0, 0.0, 0.0));

    MobilityHelper enbMobility;
    enbMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    enbMobility.SetPositionAllocator(enbPositionAlloc);
    enbMobility.Install(enbNodes);

    // -----------------------------------------------------------------------
    // UE mobility: constant velocity in +x direction
    // -----------------------------------------------------------------------
    MobilityHelper ueMobility;
    ueMobility.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    ueMobility.Install(ueNodes);

    Ptr<ConstantVelocityMobilityModel> ueMm =
        ueNodes.Get(0)->GetObject<ConstantVelocityMobilityModel>();
    ueMm->SetPosition(Vector(-100.0, 0.0, 0.0));
    ueMm->SetVelocity(Vector(speedMps, 0.0, 0.0));

    // -----------------------------------------------------------------------
    // Install LTE devices
    // -----------------------------------------------------------------------
    Config::SetDefault("ns3::LteEnbPhy::TxPower", DoubleValue(46.0));

    NetDeviceContainer enbLteDevs = lteHelper->InstallEnbDevice(enbNodes);
    NetDeviceContainer ueLteDevs = lteHelper->InstallUeDevice(ueNodes);

    // -----------------------------------------------------------------------
    // EPC remote host
    // -----------------------------------------------------------------------
    Ptr<Node> pgw = epcHelper->GetPgwNode();

    NodeContainer remoteHostContainer;
    remoteHostContainer.Create(1);
    Ptr<Node> remoteHost = remoteHostContainer.Get(0);

    InternetStackHelper internet;
    internet.Install(remoteHostContainer);

    PointToPointHelper p2ph;
    p2ph.SetDeviceAttribute("DataRate", DataRateValue(DataRate("100Gb/s")));
    p2ph.SetChannelAttribute("Delay", TimeValue(MilliSeconds(10)));
    NetDeviceContainer internetDevices = p2ph.Install(pgw, remoteHost);

    Ipv4AddressHelper ipv4h;
    ipv4h.SetBase("1.0.0.0", "255.0.0.0");
    Ipv4InterfaceContainer internetIpIfaces = ipv4h.Assign(internetDevices);
    // remoteHostAddr is not needed directly; the UdpClient uses ueIpIfaces instead.

    // Routing
    Ipv4StaticRoutingHelper ipv4RoutingHelper;
    Ptr<Ipv4StaticRouting> remoteHostStaticRouting =
        ipv4RoutingHelper.GetStaticRouting(remoteHost->GetObject<Ipv4>());
    remoteHostStaticRouting->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

    // UE IP stack
    internet.Install(ueNodes);
    Ipv4InterfaceContainer ueIpIfaces = epcHelper->AssignUeIpv4Address(ueLteDevs);
    Ptr<Ipv4StaticRouting> ueStaticRouting =
        ipv4RoutingHelper.GetStaticRouting(ueNodes.Get(0)->GetObject<Ipv4>());
    ueStaticRouting->SetDefaultRoute(epcHelper->GetUeDefaultGatewayAddress(), 1);

    // -----------------------------------------------------------------------
    // Downlink UDP traffic: Remote Host -> UE
    // -----------------------------------------------------------------------
    uint16_t dlPort = 9000;

    PacketSinkHelper dlSinkHelper("ns3::UdpSocketFactory",
                                  InetSocketAddress(Ipv4Address::GetAny(), dlPort));
    ApplicationContainer dlSinkApps = dlSinkHelper.Install(ueNodes.Get(0));
    Ptr<PacketSink> dlSink = DynamicCast<PacketSink>(dlSinkApps.Get(0));

    // Connect Rx trace for per-packet counting and gap measurement
    dlSink->TraceConnectWithoutContext("Rx", MakeCallback(&DlPacketReceived));

    dlSinkApps.Start(Seconds(0.1));
    dlSinkApps.Stop(Seconds(simTime));

    UdpClientHelper dlClient(ueIpIfaces.GetAddress(0), dlPort);
    dlClient.SetAttribute("Interval", TimeValue(MilliSeconds(10)));
    dlClient.SetAttribute("PacketSize", UintegerValue(1200));
    dlClient.SetAttribute("MaxPackets", UintegerValue(0)); // unlimited

    ApplicationContainer dlClientApps = dlClient.Install(remoteHost);
    dlClientApps.Start(Seconds(0.1));
    dlClientApps.Stop(Seconds(simTime));

    // Get the UdpClient pointer and connect TX trace for accurate packet counting.
    // We use TxWithAddresses trace rather than GetTotalTx()/packetSize to avoid
    // timing-order ambiguity when Simulator::Stop fires at the same simulated time
    // as the client's StopApplication event.
    Ptr<UdpClient> dlClientPtr = DynamicCast<UdpClient>(dlClientApps.Get(0));
    dlClientPtr->TraceConnectWithoutContext("TxWithAddresses", MakeCallback(&DlPacketSent));

    // -----------------------------------------------------------------------
    // Attach UE and add X2 interface
    // -----------------------------------------------------------------------
    lteHelper->Attach(ueLteDevs.Get(0), enbLteDevs.Get(0));
    lteHelper->AddX2Interface(enbNodes);

    // -----------------------------------------------------------------------
    // Trace connections
    // -----------------------------------------------------------------------

    // UE-RRC traces
    Config::Connect("/NodeList/*/DeviceList/*/LteUeRrc/ConnectionEstablished",
                    MakeCallback(&UeNotifyConnectionEstablished));
    Config::Connect("/NodeList/*/DeviceList/*/LteUeRrc/HandoverStart",
                    MakeCallback(&UeNotifyHandoverStart));
    Config::Connect("/NodeList/*/DeviceList/*/LteUeRrc/HandoverEndOk",
                    MakeCallback(&UeNotifyHandoverEndOk));
    Config::Connect("/NodeList/*/DeviceList/*/LteUeRrc/HandoverEndError",
                    MakeCallback(&UeNotifyHandoverEndError));

    // eNB-RRC HandoverStart and HandoverEndOk (for counters and ping-pong detection)
    Config::Connect("/NodeList/*/DeviceList/*/LteEnbRrc/HandoverStart",
                    MakeCallback(&EnbNotifyHandoverStart));
    Config::Connect("/NodeList/*/DeviceList/*/LteEnbRrc/HandoverStart",
                    MakeCallback(&EnbNotifyHandoverStartPingPong));
    Config::Connect("/NodeList/*/DeviceList/*/LteEnbRrc/HandoverEndOk",
                    MakeCallback(&EnbNotifyHandoverEndOk));
    Config::Connect("/NodeList/*/DeviceList/*/LteEnbRrc/HandoverEndOk",
                    MakeCallback(&EnbNotifyHandoverEndOkPingPong));

    // eNB-RRC failure traces (4 distinct causes)
    Config::Connect("/NodeList/*/DeviceList/*/LteEnbRrc/HandoverFailureNoPreamble",
                    MakeCallback(&EnbNotifyHandoverFailureNoPreamble));
    Config::Connect("/NodeList/*/DeviceList/*/LteEnbRrc/HandoverFailureMaxRach",
                    MakeCallback(&EnbNotifyHandoverFailureMaxRach));
    Config::Connect("/NodeList/*/DeviceList/*/LteEnbRrc/HandoverFailureLeaving",
                    MakeCallback(&EnbNotifyHandoverFailureLeaving));
    Config::Connect("/NodeList/*/DeviceList/*/LteEnbRrc/HandoverFailureJoining",
                    MakeCallback(&EnbNotifyHandoverFailureJoining));

    // -----------------------------------------------------------------------
    // Simulation run
    // -----------------------------------------------------------------------
    Simulator::Stop(Seconds(simTime));

    // Begin periodic throughput reporting at t=1.1s (after traffic starts at 0.1s)
    Simulator::Schedule(Seconds(1.1), &ReportThroughput, dlSink);

    Simulator::Run();

    // -----------------------------------------------------------------------
    // Post-simulation: write gap CSV and summary
    // -----------------------------------------------------------------------

    // Flush gap records
    for (const auto& g : g_hoGaps)
    {
        double gap = -1.0;
        bool gapValid = false;
        if (g.preRxValid && g.postRxValid)
        {
            gap = g.firstPostRxTime - g.lastPreRxTime;
            gapValid = true;
        }
        g_gapCsv << g.hoIndex << "," << g.hoStartTime << ","
                 << (g.successSeen ? g.hoSuccessTime : -1.0) << ","
                 << (g.preRxValid ? g.lastPreRxTime : -1.0) << ","
                 << (g.postRxValid ? g.firstPostRxTime : -1.0) << "," << gap << ","
                 << (gapValid ? "1" : "0") << "\n";
    }

    // TX packet count: use the TxWithAddresses trace counter (g_txPacketCount),
    // which is incremented in DlPacketSent for every packet fired by UdpClient.
    // This avoids the timing-ambiguity issue with GetTotalTx() at sim end.
    uint32_t txPackets = g_txPacketCount;

    uint64_t rxBytes = dlSink->GetTotalRx();
    double meanThroughputMbps = (rxBytes * 8.0) / simTime / 1e6;

    uint32_t hoFailuresTotal = g_hoFailures; // unique failed HO attempts

    // Compute mean gap over valid measurements
    double totalGap = 0.0;
    uint32_t validGaps = 0;
    for (const auto& g : g_hoGaps)
    {
        if (g.preRxValid && g.postRxValid)
        {
            totalGap += (g.firstPostRxTime - g.lastPreRxTime);
            ++validGaps;
        }
    }
    double meanGapS = validGaps > 0 ? totalGap / validGaps : -1.0;

    std::cout << "\n=== SIMULATION COMPLETE ===" << std::endl;
    std::cout << "Policy:               " << policy << std::endl;
    std::cout << "Speed (km/h):         " << speedKmh << std::endl;
    std::cout << "Seed/Run:             " << seed << "/" << run << std::endl;
    std::cout << "SimTime (s):          " << simTime << std::endl;
    std::cout << "HO Attempts:          " << g_hoAttempts << std::endl;
    std::cout << "HO Successes:         " << g_hoSuccesses << std::endl;
    std::cout << "HO Failures (unique): " << hoFailuresTotal << std::endl;
    std::cout << "Ping-Pongs:           " << g_pingPongCount << std::endl;
    std::cout << "TX Packets (traced):  " << txPackets << std::endl;
    std::cout << "RX Packets (actual):  " << g_rxPacketCount << std::endl;
    std::cout << "RX Bytes:             " << rxBytes << std::endl;
    std::cout << "Mean Throughput Mbps: " << meanThroughputMbps << std::endl;
    std::cout << "Mean HO RX Gap (s):   " << (validGaps > 0 ? std::to_string(meanGapS) : "N/A")
              << " (n=" << validGaps << ")" << std::endl;
    std::cout << "  NOTE: RX gap is the DL packet reception gap, not exact user-plane" << std::endl;
    std::cout << "  interruption duration. PDCP-level measurement not implemented." << std::endl;

    // Write summary CSV
    std::ofstream summaryCsv(outDir + "/summary.csv");
    summaryCsv << "policy,speed_kmh,seed,run,sim_time_s,"
               << "ho_attempts,ho_successes,ho_failures,ping_pong,"
               << "tx_packets,rx_packets,rx_bytes,"
               << "mean_throughput_mbps,mean_ho_rx_gap_s,valid_gap_measurements\n";
    summaryCsv << policy << "," << speedKmh << "," << seed << "," << run << "," << simTime << ","
               << g_hoAttempts << "," << g_hoSuccesses << "," << hoFailuresTotal << ","
               << g_pingPongCount << "," << txPackets << "," << g_rxPacketCount << "," << rxBytes
               << "," << meanThroughputMbps << "," << meanGapS << "," << validGaps << "\n";
    summaryCsv.close();

    // -----------------------------------------------------------------------
    // Close files and destroy
    // -----------------------------------------------------------------------
    g_throughputCsv.close();
    g_eventsCsv.close();
    g_rxPacketCsv.close();
    g_gapCsv.close();

    Simulator::Destroy();
    return 0;
}
