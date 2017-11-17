/*******************************************************************************
 *   Created: 2017/10/22 17:57:59
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 ******************************************************************************/

#include "Prec.hpp"
#include "Strategy.hpp"
#include "OperationContext.hpp"
#include "PositionController.hpp"
#include "Report.hpp"

using namespace trdk;
using namespace trdk::Lib;
using namespace trdk::Lib::TimeMeasurement;
using namespace trdk::Strategies::ArbitrageAdvisor;

namespace pt = boost::posix_time;
namespace aa = trdk::Strategies::ArbitrageAdvisor;
namespace sig = boost::signals2;

////////////////////////////////////////////////////////////////////////////////

class aa::Strategy::Implementation : private boost::noncopyable {
 public:
  aa::Strategy &m_self;
  PositionController m_controller;
  sig::signal<void(const Advice &)> m_adviceSignal;

  Double m_minPriceDifferenceRatioToAdvice;
  boost::optional<TradingSettings> m_tradingSettings;

  boost::unordered_map<Symbol, std::vector<Advice::SecuritySignal>> m_symbols;

  boost::unordered_set<const Security *> m_errors;

  explicit Implementation(aa::Strategy &self)
      : m_self(self),
        m_controller(m_self),
        m_minPriceDifferenceRatioToAdvice(0) {}

  void CheckSignal(Security &updatedSecurity,
                   std::vector<Advice::SecuritySignal> &allSecurities,
                   const Milestones &delayMeasurement) {
    typedef std::pair<Price, Advice::SecuritySignal *> PriceItem;

    std::vector<PriceItem> bids;
    std::vector<PriceItem> asks;

    bids.reserve(allSecurities.size());
    asks.reserve(allSecurities.size());

    Price updatedSecurityBidPrice = std::numeric_limits<double>::quiet_NaN();
    Price updatedSecurityAskPrice = std::numeric_limits<double>::quiet_NaN();
    for (auto &security : allSecurities) {
      security.isBestBid = security.isBestAsk = false;
      bids.emplace_back(security.security->GetBidPriceValue(), &security);
      asks.emplace_back(security.security->GetAskPriceValue(), &security);
      if (security.security == &updatedSecurity) {
        updatedSecurityBidPrice = bids.back().first;
        updatedSecurityAskPrice = asks.back().first;
      }
      if (bids.back().first.IsNan()) {
        bids.pop_back();
      }
      if (asks.back().first.IsNan()) {
        asks.pop_back();
      }
    }

    std::sort(bids.begin(), bids.end(),
              [](const PriceItem &lhs, const PriceItem &rhs) {
                return lhs.first > rhs.first;
              });
    std::sort(asks.begin(), asks.end(),
              [](const PriceItem &lhs, const PriceItem &rhs) {
                return lhs.first < rhs.first;
              });

    Price spread;
    Double spreadRatio;
    if (!bids.empty() && !asks.empty()) {
      auto &bestBid = bids.front();
      auto &bestAsk = asks.front();
      spread = bestBid.first - bestAsk.first;
      spreadRatio = 100 / (bestAsk.first / spread);
      spreadRatio = RoundByPrecision(spreadRatio, 100);
      spreadRatio /= 100;
      auto &bestSell = *bestBid.second->security;
      auto &bestBuy = *bestAsk.second->security;
      if (m_tradingSettings &&
          spreadRatio >= m_tradingSettings->minPriceDifferenceRatio) {
        Trade(bestSell, bestBuy,
              std::min(m_tradingSettings->maxQty,
                       std::min(bestSell.GetBidQty(), bestBuy.GetAskQty())),
              bestSell.GetBidPrice(), bestBuy.GetAskPrice(), spreadRatio,
              delayMeasurement);
      } else {
        StopTrading(bestSell, bestBuy, spreadRatio);
      }
    } else {
      spread = spreadRatio = std::numeric_limits<double>::quiet_NaN();
    }

    for (auto it = bids.begin();
         it != bids.cend() && it->first == bids.front().first; ++it) {
      Assert(!it->second->isBestBid);
      it->second->isBestBid = true;
    }
    for (auto it = asks.begin();
         it != asks.cend() && it->first == asks.front().first; ++it) {
      Assert(!it->second->isBestAsk);
      it->second->isBestAsk = true;
    }

    m_adviceSignal(
        Advice{&updatedSecurity,
               updatedSecurity.GetLastMarketDataTime(),
               {updatedSecurityBidPrice, updatedSecurity.GetBidQtyValue()},
               {updatedSecurityAskPrice, updatedSecurity.GetAskQtyValue()},
               spread,
               spreadRatio,
               spreadRatio >= m_minPriceDifferenceRatioToAdvice,
               allSecurities});
  }

  void RecheckSignal() {
    for (auto &symbol : m_symbols) {
      for (const Advice::SecuritySignal &security : symbol.second) {
        CheckSignal(*security.security, symbol.second, Milestones());
      }
    }
  }

  bool CheckActualPositions(const Security &sellTarget,
                            const Security &buyTarget,
                            const Double &spreadRatio) {
    size_t numberOfPositionsWithTheSameTargget = 0;
    for (auto &position : m_self.GetPositions()) {
      if (position.IsCompleted()) {
        continue;
      }

      const auto &operation =
          *boost::polymorphic_downcast<const OperationContext *>(
              &position.GetOperationContext());
      if (operation.IsSame(sellTarget, buyTarget)) {
        AssertGe(2, numberOfPositionsWithTheSameTargget);
        ++numberOfPositionsWithTheSameTargget;
        continue;
      }

      m_self.GetTradingLog().Write(
          "{'signal': {'signalExpired': {'sell': {'exchange': '%1%', 'bid': "
          "%2$.8f, 'ask': %3$.8f}, 'buy': {'exchange': '%4%', 'bid': "
          "%5$.8f, 'ask': %6$.8f}}, 'spread': %7$.3f}}",
          [&](TradingRecord &record) {
            record % boost::cref(operation.GetTradingSystem(m_self, sellTarget)
                                     .GetInstanceName())  // 1
                % sellTarget.GetBidPriceValue()           // 2
                % sellTarget.GetAskPriceValue()           // 3
                % boost::cref(operation.GetTradingSystem(m_self, buyTarget)
                                  .GetInstanceName())  // 4
                % buyTarget.GetBidPriceValue()         // 5
                % buyTarget.GetAskPriceValue()         // 6
                % spreadRatio;                         // 7
          });
      m_controller.ClosePosition(position, CLOSE_REASON_OPEN_FAILED);
    }
    AssertGe(2, numberOfPositionsWithTheSameTargget);
    return numberOfPositionsWithTheSameTargget > 0;
  }

  void Trade(Security &sellTarget,
             Security &buyTarget,
             const Qty &maxQty,
             const Price &sellPrice,
             const Price &buyPrice,
             const Double &spreadRatio,
             const Milestones &delayMeasurement) {
    if (CheckActualPositions(sellTarget, buyTarget, spreadRatio)) {
      return;
    }
    if (m_errors.count(&sellTarget) || m_errors.count(&buyTarget)) {
      m_self.GetTradingLog().Write(
          "{'signal': {'ignored': {'reason': 'black list', 'sell': "
          "{'exchange': '%1%', 'bid': %2$.8f, 'ask': %3$.8f}, 'buy': "
          "{'exchange': '%4%', 'bid': %5$.8f, 'ask': %6$.8f}}, 'spread': "
          "%7$.3f}}",
          [&](TradingRecord &record) {
            record % boost::cref(sellTarget.GetSource().GetInstanceName())  // 1
                % sellTarget.GetBidPriceValue()                             // 2
                % sellTarget.GetAskPriceValue()                             // 3
                % boost::cref(buyTarget.GetSource().GetInstanceName())      // 4
                % buyTarget.GetBidPriceValue()                              // 5
                % buyTarget.GetAskPriceValue()                              // 6
                % spreadRatio;                                              // 7
          });
      return;
    }
    if (!sellTarget.IsOnline() || !buyTarget.IsOnline()) {
      m_self.GetTradingLog().Write(
          "{'signal': {'ignored': {'reason': 'offline', 'sell': {'exchange': "
          "'%1%', 'bid': %2$.8f, 'ask': %3$.8f}, 'buy': {'exchange': '%4%', "
          "'bid': %5$.8f, 'ask': %6$.8f}}, 'spread': %7$.3f}}",
          [&](TradingRecord &record) {
            record % boost::cref(sellTarget.GetSource().GetInstanceName())  // 1
                % sellTarget.GetBidPriceValue()                             // 2
                % sellTarget.GetAskPriceValue()                             // 3
                % boost::cref(buyTarget.GetSource().GetInstanceName())      // 4
                % buyTarget.GetBidPriceValue()                              // 5
                % buyTarget.GetAskPriceValue()                              // 6
                % spreadRatio;                                              // 7
          });
      return;
    }

    const auto operation = boost::make_shared<OperationContext>(
        sellTarget, buyTarget, maxQty, sellPrice, buyPrice);

    const auto &sellTradingSystemName =
        operation->GetTradingSystem(m_self, sellTarget).GetInstanceName();
    const auto &buyTradingSystemName =
        operation->GetTradingSystem(m_self, buyTarget).GetInstanceName();

    m_self.GetTradingLog().Write(
        "{'signal': {'new': {'sell': {'exchange': '%1%', 'bid': %2$.8f, 'ask': "
        "%3$.8f, 'price': %8$.8f}, 'buy': {'exchange': '%4%', 'bid': %5$.8f, "
        "'ask': %6$.8f}}, 'spread': %7$.3f, 'price': %9$.8f}}",
        [&](TradingRecord &record) {
          record % boost::cref(sellTradingSystemName)  // 1
              % sellTarget.GetBidPriceValue()          // 2
              % sellTarget.GetAskPriceValue()          // 3
              % boost::cref(buyTradingSystemName)      // 4
              % buyTarget.GetBidPriceValue()           // 5
              % buyTarget.GetAskPriceValue()           // 6
              % spreadRatio                            // 7
              % sellPrice                              // 8
              % buyPrice;                              // 9
        });

    const auto &legTargets =
        boost::icontains(buyTradingSystemName, "ccex") ||
                boost::icontains(buyTradingSystemName, "novaexchange")
            ? std::make_pair(&buyTarget, &sellTarget)
            : std::make_pair(&sellTarget, &buyTarget);

    Position *firstLegPosition = nullptr;
    try {
      firstLegPosition = &m_controller.OpenPosition(
          operation, *legTargets.first, delayMeasurement);
      m_controller.OpenPosition(operation, *legTargets.second,
                                delayMeasurement);
    } catch (const std::exception &ex) {
      m_self.GetLog().Error("Failed to start trading: \"%1%\".", ex.what());

      if (firstLegPosition) {
        Verify(m_errors.emplace(legTargets.second).second);
        m_controller.ClosePosition(*firstLegPosition, CLOSE_REASON_OPEN_FAILED);
        operation->GetReportData().Add(OperationReportData::PositionReport{
            {},
            !firstLegPosition->IsLong(),
            firstLegPosition->GetOpenStartTime(),
            pt::not_a_date_time,
            firstLegPosition->IsLong() ? sellTarget.GetBidPrice()
                                       : buyTarget.GetAskPrice(),
            std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::quiet_NaN(),
            firstLegPosition->IsLong()
                ? &operation->GetTradingSystem(m_self, sellTarget)
                : &operation->GetTradingSystem(m_self, buyTarget),
            CLOSE_REASON_OPEN_FAILED});
      }

      const auto &disabledTarget =
          *(!firstLegPosition ? legTargets.first : legTargets.second);
      Verify(m_errors.emplace(legTargets.first).second);
      m_self.GetLog().Warn(
          "\"%1%\" security (%2% leg) is disabled by position opening error. "
          "%3% leg is \"%4%\"",
          disabledTarget, !firstLegPosition ? "first" : "second",
          !firstLegPosition ? "Second" : "First",
          &disabledTarget == legTargets.first ? *legTargets.second
                                              : *legTargets.first);
    }
  }

  void StopTrading(const Security &bestBid,
                   const Security &bestAsk,
                   const Double &spreadRatio) {
    if (m_self.GetPositions().IsEmpty()) {
      return;
    }

    m_self.GetTradingLog().Write(
        "{'signal': {'stop': {'sell': {'exchange': '%1%', 'bid': %2$.8f, "
        "'ask': %3$.8f}, 'buy': {'exchange': '%4%', 'bid': %5$.8f, 'ask': "
        "%6$.8f}}, 'spread': %7$.3f}}",
        [&](TradingRecord &record) {
          record % boost::cref(bestBid.GetSource().GetInstanceName())  // 1
              % bestBid.GetBidPriceValue()                             // 2
              % bestAsk.GetAskPriceValue()                             // 3
              % boost::cref(bestAsk.GetSource().GetInstanceName())     // 4
              % bestAsk.GetBidPriceValue()                             // 5
              % bestAsk.GetAskPriceValue()                             // 6
              % spreadRatio;                                           // 7
        });

    for (auto &position : m_self.GetPositions()) {
      m_controller.ClosePosition(position, CLOSE_REASON_OPEN_FAILED);
    }
  }
};

aa::Strategy::Strategy(Context &context,
                       const std::string &instanceName,
                       const IniSectionRef &conf)
    : Base(context,
           "{39FBFFDA-10D7-462D-BA82-0D8BA9CA7A09}",
           "ArbitrageAdvisor",
           instanceName,
           conf),
      m_pimpl(boost::make_unique<Implementation>(*this)) {}

aa::Strategy::~Strategy() = default;

sig::scoped_connection aa::Strategy::SubscribeToAdvice(
    const boost::function<void(const Advice &)> &slot) {
  const auto &result = m_pimpl->m_adviceSignal.connect(slot);
  m_pimpl->RecheckSignal();
  return result;
}

void aa::Strategy::SetupAdvising(const Double &minPriceDifferenceRatio) const {
  GetTradingLog().Write(
      "{'setup': {'advising': {'ratio': '%1$.8f->%2$.8f'}}}",
      [this, &minPriceDifferenceRatio](TradingRecord &record) {
        record % m_pimpl->m_minPriceDifferenceRatioToAdvice  // 1
            % minPriceDifferenceRatio;                       // 2
      });
  if (m_pimpl->m_minPriceDifferenceRatioToAdvice == minPriceDifferenceRatio) {
    return;
  }
  m_pimpl->m_minPriceDifferenceRatioToAdvice = minPriceDifferenceRatio;
  m_pimpl->RecheckSignal();
}

void aa::Strategy::OnSecurityStart(Security &security, Security::Request &) {
  Assert(std::find_if(m_pimpl->m_symbols[security.GetSymbol()].cbegin(),
                      m_pimpl->m_symbols[security.GetSymbol()].cend(),
                      [&security](const Advice::SecuritySignal &stored) {
                        return stored.security == &security ||
                               stored.isBestBid || stored.isBestAsk;
                      }) == m_pimpl->m_symbols[security.GetSymbol()].cend());
  m_pimpl->m_symbols[security.GetSymbol()].emplace_back(
      Advice::SecuritySignal{&security});
}

void aa::Strategy::ActivateAutoTrading(TradingSettings &&settings) {
  bool shouldRechecked = true;
  if (m_pimpl->m_tradingSettings) {
    GetTradingLog().Write(
        "{'setup': {'trading': {'ratio': '%1$.8f->%2$.8f', 'maxQty': "
        "'%3$.8f->%4$.8f}}}",
        [this, &settings](TradingRecord &record) {
          record % m_pimpl->m_tradingSettings->minPriceDifferenceRatio  // 1
              % settings.minPriceDifferenceRatio                        // 2
              % m_pimpl->m_tradingSettings->maxQty                      // 3
              % settings.maxQty;                                        // 4
        });
    shouldRechecked = m_pimpl->m_tradingSettings->minPriceDifferenceRatio !=
                      settings.minPriceDifferenceRatio;
  } else {
    GetTradingLog().Write(
        "{'setup': {'trading': {'ratio': 'null->%1$.8f', 'maxQty': "
        "'null->%2$.8f'}}}",
        [this, &settings](TradingRecord &record) {
          record % settings.minPriceDifferenceRatio  // 1
              % settings.maxQty;                     // 2
        });
  }
  m_pimpl->m_tradingSettings = std::move(settings);
  m_pimpl->RecheckSignal();
}

const boost::optional<aa::Strategy::TradingSettings>
    &aa::Strategy::GetAutoTradingSettings() const {
  return m_pimpl->m_tradingSettings;
}
void aa::Strategy::DeactivateAutoTrading() {
  if (m_pimpl->m_tradingSettings) {
    GetTradingLog().Write(
        "{'setup': {'trading': {'ratio': '%1$.8f->null', 'maxQty': "
        "'%2$.8f->null'}}}",
        [this](TradingRecord &record) {
          record % m_pimpl->m_tradingSettings->minPriceDifferenceRatio  // 1
              % m_pimpl->m_tradingSettings->maxQty;                     // 2
        });
  } else {
    GetTradingLog().Write(
        "{'setup': {'trading': {'ratio': 'null->null', 'maxQty': "
        "'null->null'}}}");
  }
  m_pimpl->m_tradingSettings = boost::none;
  m_pimpl->m_errors.clear();
}

void aa::Strategy::OnLevel1Update(Security &security,
                                  const Milestones &delayMeasurement) {
  if (m_pimpl->m_adviceSignal.num_slots() == 0 && !m_pimpl->m_tradingSettings) {
    return;
  }
  AssertLt(0, m_pimpl->m_adviceSignal.num_slots());
  try {
    m_pimpl->CheckSignal(security, m_pimpl->m_symbols[security.GetSymbol()],
                         delayMeasurement);
  } catch (const Exception &ex) {
    GetLog().Error("Failed to check signal: \"%1%\".", ex.what());
  }
}

void aa::Strategy::OnPositionUpdate(Position &position) {
  m_pimpl->m_controller.OnPositionUpdate(position);
}

void aa::Strategy::OnPostionsCloseRequest() {
  m_pimpl->m_controller.OnPostionsCloseRequest();
}
////////////////////////////////////////////////////////////////////////////////

boost::shared_ptr<trdk::Strategy> CreateStrategy(
    Context &context,
    const std::string &instanceName,
    const IniSectionRef &conf) {
  return boost::make_shared<aa::Strategy>(context, instanceName, conf);
}

////////////////////////////////////////////////////////////////////////////////
