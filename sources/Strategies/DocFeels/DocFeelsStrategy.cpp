/*******************************************************************************
 *   Created: 2017/08/20 14:18:12
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 ******************************************************************************/

#include "Prec.hpp"
#include "DocFeelsStrategy.hpp"
#include "Core/TradingLog.hpp"

using namespace trdk;
using namespace trdk::Lib;
using namespace trdk::Lib::TimeMeasurement;
using namespace trdk::Strategies::DocFeels;

namespace pt = boost::posix_time;
namespace df = trdk::Strategies::DocFeels;

namespace {
class Generator : private boost::noncopyable {
 public:
  explicit Generator(size_t rangeFrom, size_t rangeTo)
      :
#ifndef DEV_VER
        m_seed(static_cast<uint32_t>(pt::microsec_clock::universal_time()
                                         .time_of_day()
                                         .total_nanoseconds())),
#else
        m_seed(boost::mt19937::default_seed),
#endif
        m_random(m_seed),
        m_range(rangeFrom, rangeTo),
        m_generate(m_random, m_range) {
  }

 public:
  size_t Generate() const { return m_generate(); }

 private:
  const uint32_t m_seed;
  boost::mt19937 m_random;
  boost::uniform_int<size_t> m_range;
  mutable boost::variate_generator<boost::mt19937, boost::uniform_int<size_t>>
      m_generate;
};
}

df::Strategy::Strategy(Context &context,
                       const std::string &instanceName,
                       const IniSectionRef &conf,
                       const boost::shared_ptr<CtsTrend> &trend)
    : Base(context,
           "{afbc3244-cc9c-4aa0-bc94-223f9232f043}",
           "DocFeels",
           instanceName,
           conf),
      m_trend(trend ? trend : boost::make_shared<CtsTrend>(conf)),
      m_positionController(*this, *m_trend),
      m_security(nullptr),
      m_barService(nullptr),
      m_groupReportPeriod(pt::hours(
          conf.ReadTypedKey<long>("groups_report_period_days") * 24)) {
  const auto numberOfCts1 = conf.ReadTypedKey<size_t>("number_of_cts1");
  const auto numberOfRtfsPerGroup =
      conf.ReadTypedKey<size_t>("number_of_rtfs_per_group");
  const auto numberOfRtfGroups =
      conf.ReadTypedKey<size_t>("number_of_rtf_groups");
  for (size_t i = 0; i < numberOfCts1; ++i) {
    boost::format ctsInstanceName("CTS-1.%1%");
    ctsInstanceName % (i + 1);
    m_cts1.emplace_back(std::make_unique<CumulativeReturnFilterService>(
        context, ctsInstanceName.str(), conf));
  }
  if (!m_cts1.empty()) {
    m_groupsReport = OpenDataLog("csv", "_GROUPS");
    m_groupsReport << "Date"           // 1
                   << ",Losses.Total"  // 2
                   << ",Wins.Total"    // 3
                   << ",% Win.Total";  // 4
    const Generator generator(0, m_cts1.size() - 1);
    for (size_t groupIndex = 0; groupIndex < numberOfRtfGroups; ++groupIndex) {
      m_groupsReport << ",Losses." << (groupIndex + 1)  // 5
                     << ",Wins." << (groupIndex + 1)    // 6
                     << ",% Win." << (groupIndex + 1);  // 7
      std::vector<size_t> group;
      group.reserve(numberOfRtfsPerGroup);
      for (size_t rtfInGroupIndex = 0; rtfInGroupIndex < numberOfRtfsPerGroup;
           ++rtfInGroupIndex) {
        group.emplace_back(generator.Generate());
      }
      m_groups.emplace_back(Group{std::move(group)});
    }
    m_groupsReport << std::endl;
  }
}

df::Strategy::~Strategy() {
  try {
    FlushGroupReport(true);
  } catch (...) {
    AssertFailNoException();
  }
}

void df::Strategy::OnSecurityStart(Security &security,
                                   Security::Request &request) {
  Base::OnSecurityStart(security, request);
  if (m_security) {
    throw Exception(
        "Strategy can not work with more than one trading security");
  }
  m_security = &security;
  GetLog().Info("Using \"%1%\"...", *m_security);
}

void df::Strategy::OnServiceStart(const Service &service) {
  const auto *barService = dynamic_cast<const Services::BarService *>(&service);
  if (barService) {
    if (m_barService) {
      throw Exception("Strategy uses one bar-service, but configured more");
    }
    m_barService = barService;
  }
  GetLog().Info("Using \"%1%\" as %2%-service...",
                barService ? "bar" : "control",  // 1
                service);                        // 2
}

void df::Strategy::OnServiceDataUpdate(const Service &service,
                                       const Milestones &delayMeasurement) {
  if (!m_barService || m_barService != &service) {
    throw Exception("One or more of required services are not set");
  }
  CheckSignal(delayMeasurement);
}

void df::Strategy::OnPositionUpdate(Position &position) {
  m_positionController.OnPositionUpdate(position);
}

void df::Strategy::OnPostionsCloseRequest() {
  m_positionController.OnPostionsCloseRequest();
}

void df::Strategy::CheckSignal(const Milestones &delayMeasurement) {
  Assert(m_security);
  Assert(m_barService);
  Assert(!m_barService->IsEmpty());

  size_t numberOfResults = 0;
  for (auto &cts1 : m_cts1) {
    if (cts1->RaiseServiceDataUpdateEvent(*m_barService, delayMeasurement)) {
      ++numberOfResults;
    }
  }
  if (!numberOfResults) {
    return;
  }
  AssertEq(m_cts1.size(), numberOfResults);

  intmax_t confluence = 0;
  if (numberOfResults) {
    for (auto &group : m_groups) {
      intmax_t groupConfluence = 0;
      for (const auto &index : group.rtfs) {
        const auto &rtf = m_cts1[index]->GetLastPoint().bestTNewValues.nprtf;
        groupConfluence += rtf;
      }
      ++(groupConfluence <= 0 ? group.numberOfLosses : group.numberOfWins);
      confluence += groupConfluence;
    }
  }
  m_lastGroupTime = m_barService->GetLastBar().time;
  FlushGroupReport(false);

  const bool isTrendChanged = m_trend->Update(confluence);
  GetTradingLog().Write(
      "trend\t%1%\t%2%\tconfluence=%3%", [&](TradingRecord &record) {
        record % (isTrendChanged ? "CHANGED" : "not-changed")  // 1
            % (m_trend->IsRising()
                   ? "rising"
                   : m_trend->IsFalling() ? "falling" : "     ")  // 2
            % confluence;                                         // 3
      });
  if (isTrendChanged) {
    m_positionController.OnSignal(*m_security, delayMeasurement);
  }
}

void df::Strategy::FlushGroupReport(bool isForced) {
  if (m_lastGroupTime.is_not_a_date_time()) {
    return;
  }
  if (!isForced) {
    if (m_nextGroupReportTime.is_not_a_date_time()) {
      m_nextGroupReportTime = m_lastGroupTime + m_groupReportPeriod;
      return;
    }
    if (m_lastGroupTime < m_nextGroupReportTime) {
      return;
    }
    AssertEq(m_nextGroupReportTime, m_lastGroupTime);
  }

  std::vector<std::pair<size_t, size_t>> data;
  data.reserve(m_groups.size());
  auto total = std::make_pair<size_t, size_t>(0, 0);
  for (auto &group : m_groups) {
    total.first += group.numberOfLosses;
    total.second += group.numberOfWins;
    data.emplace_back(group.numberOfLosses, group.numberOfWins);
    group.ResetStat();
  }
  m_nextGroupReportTime = m_lastGroupTime + m_groupReportPeriod;

  m_groupsReport << m_lastGroupTime.date()  // 1
                 << ',' << total.first      // 2
                 << ',' << total.second     // 3
                 << ',' << static_cast<int>((static_cast<double>(total.second) /
                                             (total.second + total.first)) *
                                            100);  // 4
  for (const auto &group : data) {
    m_groupsReport << ',' << group.first   // 5
                   << ',' << group.second  // 6
                   << ','
                   << static_cast<int>((static_cast<double>(group.second) /
                                        (group.second + group.first)) *
                                       100);  // 7
  }
  m_groupsReport << std::endl;
}
