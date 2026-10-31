#ifndef RF_HANDOVER_ALGORITHM_H
#define RF_HANDOVER_ALGORITHM_H

#include "ns3/lte-handover-algorithm.h"
#include "ns3/lte-handover-management-sap.h"
#include "ns3/lte-rrc-sap.h"
#include "ns3/lte-common.h"
#include "ns3/nstime.h"
#include "ns3/string.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"
#include "ns3/log.h"
#include "ns3/simulator.h"

#include <vector>
#include <map>
#include <string>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <iostream>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("RfHandoverAlgorithm");

/**
 * Node definition for a single decision tree in the Random Forest.
 */
struct RfTreeNode
{
    int featureIndex;      ///< Feature index (-1 for leaf node)
    double threshold;      ///< Split threshold
    int leftChild;         ///< Index of left child node
    int rightChild;        ///< Index of right child node
    double probClass1;     ///< Class 1 (handover) probability if leaf node
};

/**
 * In-memory representation of the complete Random Forest ensemble.
 */
struct RfModelEnsemble
{
    std::vector<std::vector<RfTreeNode>> trees;

    bool IsLoaded() const
    {
        return !trees.empty();
    }

    /**
     * Evaluate the ensemble on a feature vector and return predicted class 1 probability.
     */
    double PredictProba(const std::vector<double>& features) const
    {
        if (trees.empty())
        {
            return 0.0;
        }

        double sumProb = 0.0;
        for (const auto& tree : trees)
        {
            if (tree.empty())
            {
                continue;
            }
            int node = 0;
            while (node >= 0 && node < static_cast<int>(tree.size()) && tree[node].featureIndex >= 0)
            {
                int fIdx = tree[node].featureIndex;
                double val = (fIdx >= 0 && fIdx < static_cast<int>(features.size())) ? features[fIdx] : 0.0;
                if (val <= tree[node].threshold)
                {
                    node = tree[node].leftChild;
                }
                else
                {
                    node = tree[node].rightChild;
                }
            }
            if (node >= 0 && node < static_cast<int>(tree.size()))
            {
                sumProb += tree[node].probClass1;
            }
        }
        return sumProb / static_cast<double>(trees.size());
    }

    int Predict(const std::vector<double>& features, double threshold = 0.5) const
    {
        return PredictProba(features) >= threshold ? 1 : 0;
    }
};

/**
 * Handover algorithm supporting data collection and Random Forest inference.
 */
class RfHandoverAlgorithm : public LteHandoverAlgorithm
{
public:
    RfHandoverAlgorithm()
        : m_handoverManagementSapUser(nullptr),
          m_mode("train"),
          m_trainingDataFile("results/train/samples.csv"),
          m_hysteresisDb(3.0),
          m_timeToTrigger(MilliSeconds(256)),
          m_rfThreshold(0.5),
          m_speedKmh(120.0)
    {
        m_handoverManagementSapProvider =
            new MemberLteHandoverManagementSapProvider<RfHandoverAlgorithm>(this);
    }

    ~RfHandoverAlgorithm() override
    {
    }

    static TypeId GetTypeId()
    {
        static TypeId tid =
            TypeId("ns3::RfHandoverAlgorithm")
                .SetParent<LteHandoverAlgorithm>()
                .SetGroupName("Lte")
                .AddConstructor<RfHandoverAlgorithm>()
                .AddAttribute("Mode",
                              "Algorithm operating mode: train | rf",
                              StringValue("train"),
                              MakeStringAccessor(&RfHandoverAlgorithm::m_mode),
                              MakeStringChecker())
                .AddAttribute("TrainingOutFile",
                              "Path to CSV output file for training samples",
                              StringValue("results/train/samples.csv"),
                              MakeStringAccessor(&RfHandoverAlgorithm::m_trainingDataFile),
                              MakeStringChecker())
                .AddAttribute("Hysteresis",
                              "A3 handover margin in dB for training label generation",
                              DoubleValue(3.0),
                              MakeDoubleAccessor(&RfHandoverAlgorithm::m_hysteresisDb),
                              MakeDoubleChecker<double>(0.0, 15.0))
                .AddAttribute("TimeToTrigger",
                              "A3 Time to trigger for training label generation",
                              TimeValue(MilliSeconds(256)),
                              MakeTimeAccessor(&RfHandoverAlgorithm::m_timeToTrigger),
                              MakeTimeChecker())
                .AddAttribute("RfThreshold",
                              "Probability decision threshold for Random Forest handover",
                              DoubleValue(0.5),
                              MakeDoubleAccessor(&RfHandoverAlgorithm::m_rfThreshold),
                              MakeDoubleChecker<double>(0.0, 1.0))
                .AddAttribute("SpeedKmh",
                              "UE speed in km/h used as a feature",
                              DoubleValue(120.0),
                              MakeDoubleAccessor(&RfHandoverAlgorithm::m_speedKmh),
                              MakeDoubleChecker<double>(0.0, 500.0));
        return tid;
    }

    void SetLteHandoverManagementSapUser(LteHandoverManagementSapUser* s) override
    {
        m_handoverManagementSapUser = s;
    }

    LteHandoverManagementSapProvider* GetLteHandoverManagementSapProvider() override
    {
        return m_handoverManagementSapProvider;
    }

    friend class MemberLteHandoverManagementSapProvider<RfHandoverAlgorithm>;

    static void SetStaticModel(const RfModelEnsemble& model)
    {
        s_rfModel = model;
    }

    static const RfModelEnsemble& GetStaticModel()
    {
        return s_rfModel;
    }

protected:
    void DoInitialize() override
    {
        // Configure measurement reports to stream frequently around the cell boundary
        LteRrcSap::ReportConfigEutra reportConfig;
        reportConfig.eventId = LteRrcSap::ReportConfigEutra::EVENT_A3;
        // a3Offset = -10 (-5.0 dB) causes reports to trigger as soon as neighbour is within 5 dB of serving
        reportConfig.a3Offset = -10;
        reportConfig.hysteresis = 0;
        reportConfig.timeToTrigger = 0;
        reportConfig.reportOnLeave = false;
        reportConfig.triggerQuantity = LteRrcSap::ReportConfigEutra::RSRP;
        reportConfig.reportInterval = LteRrcSap::ReportConfigEutra::MS240; // 240 ms interval
        m_measIds = m_handoverManagementSapUser->AddUeMeasReportConfigForHandover(reportConfig);

        LteHandoverAlgorithm::DoInitialize();
    }

    void DoDispose() override
    {
        delete m_handoverManagementSapProvider;
        LteHandoverAlgorithm::DoDispose();
    }

    void DoReportUeMeas(uint16_t rnti, LteRrcSap::MeasResults measResults) override
    {
        if (std::find(begin(m_measIds), end(m_measIds), measResults.measId) == std::end(m_measIds))
        {
            return;
        }

        if (!measResults.haveMeasResultNeighCells || measResults.measResultListEutra.empty())
        {
            return;
        }

        double servingRsrpDbm = EutranMeasurementMapping::RsrpRange2Dbm(measResults.measResultPCell.rsrpResult);
        double servingRsrqDb = EutranMeasurementMapping::RsrqRange2Db(measResults.measResultPCell.rsrqResult);

        uint16_t bestNeighbourCellId = 0;
        double bestNeighbourRsrpDbm = -999.0;
        double bestNeighbourRsrqDb = -999.0;

        for (const auto& n : measResults.measResultListEutra)
        {
            if (n.haveRsrpResult)
            {
                double nRsrpDbm = EutranMeasurementMapping::RsrpRange2Dbm(n.rsrpResult);
                if (nRsrpDbm > bestNeighbourRsrpDbm && IsValidNeighbour(n.physCellId))
                {
                    bestNeighbourCellId = n.physCellId;
                    bestNeighbourRsrpDbm = nRsrpDbm;
                    if (n.haveRsrqResult)
                    {
                        bestNeighbourRsrqDb = EutranMeasurementMapping::RsrqRange2Db(n.rsrqResult);
                    }
                    else
                    {
                        bestNeighbourRsrqDb = servingRsrqDb;
                    }
                }
            }
        }

        if (bestNeighbourCellId == 0)
        {
            return;
        }

        double deltaRsrpDb = bestNeighbourRsrpDbm - servingRsrpDbm;
        double deltaRsrqDb = bestNeighbourRsrqDb - servingRsrqDb;
        double numNeighbours = static_cast<double>(measResults.measResultListEutra.size());

        // Feature vector: [serving_rsrp, target_rsrp, delta_rsrp, serving_rsrq, target_rsrq, delta_rsrq, num_neigh, speed]
        std::vector<double> features = {
            servingRsrpDbm,
            bestNeighbourRsrpDbm,
            deltaRsrpDb,
            servingRsrqDb,
            bestNeighbourRsrqDb,
            deltaRsrqDb,
            numNeighbours,
            m_speedKmh
        };

        Time now = Simulator::Now();

        // Check pending handover: avoid re-triggering if one was initiated recently (< 2.0 s)
        auto pendingIt = m_pendingHandoverTime.find(rnti);
        if (pendingIt != m_pendingHandoverTime.end())
        {
            if ((now - pendingIt->second) < Seconds(2.0))
            {
                return;
            }
        }

        if (m_mode == "train")
        {
            // Ground-truth A3 labeling
            int label = 0;
            if (deltaRsrpDb >= m_hysteresisDb)
            {
                auto& cellMap = m_firstConditionTime[rnti];
                if (cellMap.find(bestNeighbourCellId) == cellMap.end() || cellMap[bestNeighbourCellId].IsZero())
                {
                    cellMap[bestNeighbourCellId] = now;
                }
                else if ((now - cellMap[bestNeighbourCellId]) >= m_timeToTrigger)
                {
                    label = 1;
                }
            }
            else
            {
                m_firstConditionTime[rnti][bestNeighbourCellId] = Seconds(0);
            }

            // Append sample to dataset file
            std::ofstream f(m_trainingDataFile, std::ios::app);
            if (f.is_open())
            {
                f << now.GetSeconds() << ","
                  << rnti << ","
                  << m_speedKmh << ","
                  << servingRsrpDbm << ","
                  << bestNeighbourRsrpDbm << ","
                  << deltaRsrpDb << ","
                  << servingRsrqDb << ","
                  << bestNeighbourRsrqDb << ","
                  << deltaRsrqDb << ","
                  << numNeighbours << ","
                  << bestNeighbourCellId << ","
                  << label << "\n";
            }

            // In training mode, execute A3 handover when label == 1
            if (label == 1)
            {
                m_pendingHandoverTime[rnti] = now;
                m_firstConditionTime[rnti][bestNeighbourCellId] = Seconds(0);
                m_handoverManagementSapUser->TriggerHandover(rnti, bestNeighbourCellId);
            }
        }
        else if (m_mode == "rf")
        {
            // Random Forest inference
            double probHo = s_rfModel.PredictProba(features);
            int decision = (probHo >= m_rfThreshold) ? 1 : 0;

            if (decision == 1)
            {
                m_pendingHandoverTime[rnti] = now;
                m_handoverManagementSapUser->TriggerHandover(rnti, bestNeighbourCellId);
            }
        }
    }

private:
    bool IsValidNeighbour(uint16_t cellId)
    {
        return cellId > 0;
    }

    std::vector<uint8_t> m_measIds;
    LteHandoverManagementSapUser* m_handoverManagementSapUser;
    LteHandoverManagementSapProvider* m_handoverManagementSapProvider;

    std::string m_mode;
    std::string m_trainingDataFile;
    double m_hysteresisDb;
    Time m_timeToTrigger;
    double m_rfThreshold;
    double m_speedKmh;

    std::map<uint16_t, std::map<uint16_t, Time>> m_firstConditionTime;
    std::map<uint16_t, Time> m_pendingHandoverTime;

    static RfModelEnsemble s_rfModel;
};

// Definition of static model ensemble
inline RfModelEnsemble RfHandoverAlgorithm::s_rfModel;

} // namespace ns3

#endif /* RF_HANDOVER_ALGORITHM_H */
