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
#include "Operation.hpp"
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

namespace {

typedef std::pair<Price, AdviceSecuritySignal *> PriceItem;

std::pair<Price, Double> CaclSpread(const Price &bid, const Price &ask) {
  const Price spread = bid - ask;
  Double spreadRatio = 100 / (ask / spread);
  spreadRatio = RoundByPrecision(spreadRatio, 100);
  spreadRatio /= 100;
  return {spread, spreadRatio};
}
std::pair<Price, Double> CaclSpread(const PriceItem &bestBid,
                                    const PriceItem &bestAsk) {
  return CaclSpread(bestBid.first, bestAsk.first);
}
}

////////////////////////////////////////////////////////////////////////////////

class aa::Strategy::Implementation : private boost::noncopyable {
 public:
  aa::Strategy &m_self;
  const Double m_operationStopSpreadRatio;
  const boost::optional<Double> m_lowestSpreadRatio;

  std::unique_ptr<PositionController> m_controller;

  sig::signal<void(const Advice &)> m_adviceSignal;

  Double m_minPriceDifferenceRatioToAdvice;
  boost::optional<TradingSettings> m_tradingSettings;

  boost::unordered_map<Symbol, std::vector<AdviceSecuritySignal>> m_symbols;

  boost::unordered_set<const Security *> m_errors;
  const Security *m_lastError;

  explicit Implementation(aa::Strategy &self, const IniSectionRef &conf)
      : m_self(self),
        m_operationStopSpreadRatio(
            conf.ReadTypedKey<Double>("operation_stop_spread_percentage") /
            100),
        m_controller(
            !conf.ReadBoolKey("restore_balances", false)
                ? boost::make_unique<PositionController>(m_self)
                : boost::make_unique<PositionAndBalanceController>(m_self)),
        m_minPriceDifferenceRatioToAdvice(0),
        m_lastError(nullptr) {
    {
      const char *const key = "lowest_spread_percentage";
      if (conf.IsKeyExist(key)) {
        const_cast<boost::optional<Double> &>(m_lowestSpreadRatio) =
            conf.ReadTypedKey<Double>(key) / 100;
        m_self.GetLog().Info("Lowest spread: %1%%%.",
                             *m_lowestSpreadRatio * 100);
      } else {
        m_self.GetLog().Info("Lowest spread: not set.");
      }
    }
    m_self.GetLog().Info("Operation stop spread: %1%%%.",
                         m_operationStopSpreadRatio * 100);
    if (dynamic_cast<const PositionAndBalanceController *>(&*m_controller)) {
      m_self.GetLog().Info("Enabled balances restoration.");
    }
  }

  void CheckSignal(Security &updatedSecurity,
                   std::vector<AdviceSecuritySignal> &allSecurities,
                   const Milestones &delayMeasurement) {
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
      const auto &bestBid = bids.front();
      const auto &bestAsk = asks.front();
      boost::tie(spread, spreadRatio) = CaclSpread(bestBid, bestAsk);
      auto &bestSell = *bestBid.second->security;
      auto &bestBuy = *bestAsk.second->security;
      if (spreadRatio <= m_operationStopSpreadRatio) {
        StopTrading(bestSell, bestBuy, spreadRatio);
      } else if (m_tradingSettings &&
                 spreadRatio >= m_tradingSettings->minPriceDifferenceRatio) {
        Trade(bids, asks, spreadRatio, delayMeasurement);
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
      for (const auto &security : symbol.second) {
        CheckSignal(*security.security, symbol.second, Milestones());
      }
    }
  }

  bool CheckActualPositions(const Security &sellTarget,
                            const Security &buyTarget) {
    size_t numberOfPositionsWithTheSameTarget = 0;
    for (auto &position : m_self.GetPositions()) {
      if (!IsBusinessPosition(position) || position.IsCompleted()) {
        continue;
      }

      const auto &operation = position.GetTypedOperation<aa::Operation>();
      if (operation.IsSame(sellTarget, buyTarget)) {
        AssertGe(2, numberOfPositionsWithTheSameTarget);
        ++numberOfPositionsWithTheSameTarget;
        continue;
      }

      /*m_self.GetTradingLog().Write(
          "{'signal': {'signalExpired': {'sell': {'exchange': '%1%', 'bid': "
          "{'price': %2$.8f, 'qty': %9$.8f}, 'ask': {'price': %3$.8f, 'qty': "
          "%10$.8f}}, 'buy': {'exchange': '%4%', 'bid': {'price': %5$.8f, "
          "'qty': %11$.8f}, 'ask': {'price': %6$.8f, 'qty': %12$.8f}}}, "
          "'spread': %7$.3f, 'bestSpreadMax': %8$.3f}}",
          [&](TradingRecord &record) {
            record % boost::cref(operation.GetTradingSystem(m_self, sellTarget)
                                     .GetInstanceName())  // 1
                % sellTarget.GetBidPriceValue()           // 2
                % sellTarget.GetAskPriceValue()           // 3
                % boost::cref(operation.GetTradingSystem(m_self, buyTarget)
                                  .GetInstanceName())  // 4
                % buyTarget.GetBidPriceValue()         // 5
                % buyTarget.GetAskPriceValue()         // 6
                % (spreadRatio * 100);                 // 7
            if (m_bestSpreadRatio) {
              record % (*m_bestSpreadRatio * 100);  // 8
            } else {
              record % "null";  // 8
            }
            record % sellTarget.GetBidQtyValue()  // 9
                % sellTarget.GetAskQtyValue()     // 10
                % buyTarget.GetBidQtyValue()      // 11
                % buyTarget.GetAskQtyValue();     // 12
          });
      m_controller->ClosePosition(position, CLOSE_REASON_OPEN_FAILED);*/
    }
    AssertGe(2, numberOfPositionsWithTheSameTarget);
    return numberOfPositionsWithTheSameTarget > 0;
  }

  void Trade(const std::vector<PriceItem> &bids,
             const std::vector<PriceItem> &asks,
             const Double &bestSpreadRatio,
             const Milestones &delayMeasurement) {
    Assert(!bids.empty());
    Assert(!asks.empty());
    auto &sellTarget = *bids.front().second->security;
    auto &buyTarget = *asks.front().second->security;

    if (CheckActualPositions(sellTarget, buyTarget)) {
      return;
    }

    Price sellPrice = sellTarget.GetBidPrice();
    Price buyPrice = buyTarget.GetAskPrice();
    Double spreadRatio = bestSpreadRatio;
    {
      const auto pip = 1.0 / 10000000;
      const auto &lowestSpreadRatio =
          m_lowestSpreadRatio ? *m_lowestSpreadRatio
                              : m_tradingSettings->minPriceDifferenceRatio;
      for (;;) {
        const auto nextSellPrice = sellPrice - pip;
        const auto nextBuyPrice = buyPrice + pip;
        const auto &nextSpread = CaclSpread(nextSellPrice, nextBuyPrice);
        AssertGe(spreadRatio, nextSpread.second);
        if (nextSpread.second <= lowestSpreadRatio) {
          break;
        }
        spreadRatio = nextSpread.second;
        sellPrice = nextSellPrice;
        buyPrice = nextBuyPrice;
      }
    }

    if (m_lastError == &sellTarget || m_lastError == &buyTarget) {
      ReportIgnored("last error", sellTarget, buyTarget, spreadRatio,
                    bestSpreadRatio);
      return;
    }

    const auto qtyPrecisionPower = 1000000;

    auto qty = RoundDownByPrecision(
        std::min(m_tradingSettings->maxQty,
                 std::min(sellTarget.GetBidQty(), buyTarget.GetAskQty())),
        qtyPrecisionPower);

    {
      auto balance =
          GetOrderQtyAllowedByBalance(sellTarget, buyTarget, buyPrice);
      if (qty > balance.first) {
        Assert(balance.second);
        balance.first = RoundDownByPrecision(balance.first, qtyPrecisionPower);
        AssertGt(qty, balance.first);
        m_self.GetTradingLog().Write(
            "{'pre-trade': {'balance reduces qty': {'prev': %1$.8f, 'new': "
            "%2$.8f, 'security': '%3%'}}",
            [&](TradingRecord &record) {
              record % qty            // 1
                  % balance.first     // 2
                  % *balance.second;  // 3
            });
        if (balance.first == 0) {
          ReportIgnored("empty balance", sellTarget, buyTarget, spreadRatio,
                        bestSpreadRatio);
          return;
        }
        qty = balance.first;
      }
    }

    {
      const auto &error =
          CheckOrder(sellTarget, qty, sellPrice, ORDER_SIDE_SELL);
      if (error) {
        ReportOrderCheck(sellTarget, qty, sellPrice, ORDER_SIDE_SELL, *error,
                         sellTarget, buyTarget, spreadRatio, bestSpreadRatio);
        return;
      }
    }
    {
      const auto &error = CheckOrder(buyTarget, qty, buyPrice, ORDER_SIDE_BUY);
      if (error) {
        ReportOrderCheck(buyTarget, qty, buyPrice, ORDER_SIDE_BUY, *error,
                         sellTarget, buyTarget, spreadRatio, bestSpreadRatio);
        return;
      }
    }

    const auto sellTargetBlackListIt = m_errors.find(&sellTarget);
    const bool isSellTargetInBlackList =
        sellTargetBlackListIt != m_errors.cend();
    const auto buyTargetBlackListIt = m_errors.find(&buyTarget);
    const bool isBuyTargetInBlackList = buyTargetBlackListIt != m_errors.cend();
    if (isSellTargetInBlackList && isBuyTargetInBlackList) {
      ReportIgnored("blacklist", sellTarget, buyTarget, spreadRatio,
                    bestSpreadRatio);
      return;
    }
    if (!sellTarget.IsOnline() || !buyTarget.IsOnline()) {
      ReportIgnored("offline", sellTarget, buyTarget, spreadRatio,
                    bestSpreadRatio);
      return;
    }

    ReportSignal("trade", "start", sellTarget, buyTarget, spreadRatio,
                 bestSpreadRatio);

    const auto operation = boost::make_shared<Operation>(
        sellTarget, buyTarget, qty, sellPrice, buyPrice);

    if (isSellTargetInBlackList || isBuyTargetInBlackList) {
      if (!OpenPositionSync(sellTarget, buyTarget, operation,
                            isSellTargetInBlackList, isBuyTargetInBlackList,
                            delayMeasurement)) {
        return;
      }
    } else if (!OpenPositionAsync(sellTarget, buyTarget, operation,
                                  delayMeasurement)) {
      return;
    }

    if (isBuyTargetInBlackList) {
      m_errors.erase(buyTargetBlackListIt);
    }
    if (isSellTargetInBlackList) {
      m_errors.erase(sellTargetBlackListIt);
    }
    m_lastError = nullptr;
  }

  void StopTrading(const Security &bestBid,
                   const Security &bestAsk,
                   const Double &spreadRatio) {
    m_lastError = nullptr;

    bool isReported = false;

    for (auto &position : m_self.GetPositions()) {
      if (position.GetCloseReason() != CLOSE_REASON_NONE ||
          !IsBusinessPosition(position)) {
        // In the current implementation "not business position" will be closed
        // immediately after creation.
        AssertNe(CLOSE_REASON_NONE, position.GetCloseReason());
        continue;
      }
      if (!isReported) {
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
                  % (spreadRatio * 100);                                   // 7
            });
        isReported = true;
      }

      m_controller->ClosePosition(position, CLOSE_REASON_OPEN_FAILED);
    }
  }

  std::pair<Qty, const Security *> GetOrderQtyAllowedByBalance(
      const Security &sell, const Security &buy, const Price &buyPrice) {
    const auto &sellBalance =
        m_self.GetTradingSystem(sell.GetSource().GetIndex())
            .GetBalances()
            .FindAvailableToTrade(sell.GetSymbol().GetBaseSymbol());
    const auto &buyTradingSystem =
        m_self.GetTradingSystem(buy.GetSource().GetIndex());
    const auto &buyBalance =
        buyTradingSystem.GetBalances().FindAvailableToTrade(
            buy.GetSymbol().GetQuoteSymbol());

    const auto calcByBuyBalance = [&]() -> Qty {
      Assert(buyBalance);
      const auto result = *buyBalance / buyPrice;
      return result - buyTradingSystem.CalcCommission(result, buy);
    };

    if (sellBalance && buyBalance) {
      const auto &resultByBayBalance = calcByBuyBalance();
      if (*sellBalance <= resultByBayBalance) {
        return {*sellBalance, &sell};
      } else {
        return {resultByBayBalance, &buy};
      }
    } else if (sellBalance) {
      Assert(!buyBalance);
      return {*sellBalance, &sell};
    } else if (buyBalance) {
      Assert(!sellBalance);
      return {calcByBuyBalance(), &buy};
    } else {
      Assert(!sellBalance);
      Assert(!buyBalance);
      return {std::numeric_limits<Qty>::max(), nullptr};
    }
  }

  boost::optional<TradingSystem::OrderCheckError> CheckOrder(
      const Security &security,
      const Qty &qty,
      const Price &price,
      const OrderSide &side) {
    return m_self.GetTradingSystem(security.GetSource().GetIndex())
        .CheckOrder(security, security.GetSymbol().GetCurrency(), qty, price,
                    side);
  }

  void ReportSignal(const char *type,
                    const char *reason,
                    const Security &sellTarget,
                    const Security &buyTarget,
                    const Double &spreadRatio,
                    const Double &bestSpreadRatio,
                    const std::string &additional = std::string()) const {
    m_self.GetTradingLog().Write(
        "{'signal': {'%14%': {'reason': '%9%', 'sell': {'security': '%1%', "
        "'bid': {'price': %2$.8f, 'qty': %10$.8f}, 'ask': {'price': %3$.8f, "
        "'qty': %11$.8f}}, 'buy': {'security': '%4%', 'bid': {'price': %5$.8f, "
        "'qty': %12$.8f}, 'ask': {'price': %6$.8f, 'qty': %13$.8f}}}, "
        "'spread': {'used': %7$.3f, 'signal': %8$.3f}%15%}}",
        [&](TradingRecord &record) {
          record % sellTarget                  // 1
              % sellTarget.GetBidPriceValue()  // 2
              % sellTarget.GetAskPriceValue()  // 3
              % buyTarget                      // 4
              % buyTarget.GetBidPriceValue()   // 5
              % buyTarget.GetAskPriceValue()   // 6
              % (spreadRatio * 100)            // 7
              % (bestSpreadRatio * 100)        // 8
              % reason                         // 9
              % sellTarget.GetBidQtyValue()    // 10
              % sellTarget.GetAskQtyValue()    // 11
              % buyTarget.GetBidQtyValue()     // 12
              % buyTarget.GetAskQtyValue()     // 13
              % type                           // 14
              % additional;                    // 15
        });
  }

  void ReportIgnored(const char *reason,
                     const Security &sellTarget,
                     const Security &buyTarget,
                     const Double &spreadRatio,
                     const Double &bestSpreadRatio,
                     const std::string &additional = std::string()) const {
    ReportSignal("ignored", reason, sellTarget, buyTarget, spreadRatio,
                 bestSpreadRatio, additional);
  }

  void ReportOrderCheck(const Security &security,
                        const Qty &qty,
                        const Price &price,
                        const OrderSide &side,
                        const TradingSystem::OrderCheckError &error,
                        const Security &sellTarget,
                        const Security &buyTarget,
                        const Double &spreadRatio,
                        const Double &bestSpreadRatio) const {
    boost::format log(
        ", 'restriction': {'order': {'security': '%1%', 'side': '%2%', 'qty': "
        "%3$.8f, 'price': %4$.8f, 'vol': %5$.8f}, 'error': {'qty': %6$.8f, "
        "'price': %7$.8f, 'vol': %8$.8f}}");
    log % security        // 1
        % side            // 2
        % qty             // 3
        % price           // 4
        % (qty * price);  // 5
    if (error.qty) {
      log % *error.qty;  // 6
    } else {
      log % "null";  // 6
    }
    if (error.price) {
      log % *error.price;  // 7
    } else {
      log % "null";  // 7
    }
    if (error.volume) {
      log % *error.volume;  // 8
    } else {
      log % "null";  // 8
    }
    ReportSignal("ignored", "order check", sellTarget, buyTarget, spreadRatio,
                 bestSpreadRatio, log.str());
  }

  bool OpenPositionSync(Security &sellTarget,
                        Security &buyTarget,
                        const boost::shared_ptr<Operation> &operation,
                        bool isSellTargetInBlackList,
                        bool isBuyTargetInBlackList,
                        const Milestones &delayMeasurement) {
    Assert(isBuyTargetInBlackList || isSellTargetInBlackList);
    UseUnused(isSellTargetInBlackList);

    const auto &legTargets = isBuyTargetInBlackList
                                 ? std::make_pair(&buyTarget, &sellTarget)
                                 : std::make_pair(&sellTarget, &buyTarget);

    Position *firstLeg = nullptr;
    try {
      firstLeg = &m_controller->OpenPosition(operation, 1, *legTargets.first,
                                             delayMeasurement);
      m_controller->OpenPosition(operation, 2, *legTargets.second,
                                 delayMeasurement);

      return true;

    } catch (const std::exception &ex) {
      m_self.GetLog().Error("Failed to start trading (sync): \"%1%\".",
                            ex.what());

      if (firstLeg) {
        CloseLegPositionByOperationStartError(*firstLeg, *legTargets.second,
                                              *operation);
      }

      m_lastError = !firstLeg ? legTargets.first : legTargets.second;
      m_errors.emplace(m_lastError);
      m_self.GetLog().Warn(
          "\"%1%\" (%2% leg) added to the blacklist by position opening error. "
          "%3% leg is \"%4%\"",
          *m_lastError,                    // 1
          !firstLeg ? "first" : "second",  // 2
          !firstLeg ? "Second" : "First",  // 3
          m_lastError == legTargets.first ? *legTargets.second
                                          : *legTargets.first);  // 4
    }

    return false;
  }

  bool OpenPositionAsync(Security &firstLegTarget,
                         Security &secondLegTarget,
                         const boost::shared_ptr<Operation> &operation,
                         const Milestones &delayMeasurement) {
    const boost::function<Position *(int64_t, Security &)> &openPosition = [&](
        int64_t leg, Security &target) -> Position * {
      try {
        return &m_controller->OpenPosition(operation, leg, target,
                                           delayMeasurement);
      } catch (const std::exception ex) {
        return nullptr;
      } catch (...) {
        AssertFailNoException();
        throw;
      }
    };

    auto firstLeg =
        boost::async(boost::bind(openPosition, 1, boost::ref(firstLegTarget)));
    auto secondLeg =
        boost::async(boost::bind(openPosition, 2, boost::ref(secondLegTarget)));
    if (firstLeg.get() && secondLeg.get()) {
      return true;
    }

    m_self.GetLog().Error("Failed to start trading (async).");

    m_lastError = nullptr;
    if (!firstLeg.get()) {
      Verify(m_errors.emplace(&firstLegTarget).second);
    }
    if (!secondLeg.get()) {
      Verify(m_errors.emplace(&secondLegTarget).second);
    }

    if (!firstLeg.get() && !secondLeg.get()) {
      m_self.GetLog().Warn(
          "\"%1%\" and \"%2%\" added to the blacklist by position opening "
          "error.",
          firstLegTarget,    // 1
          secondLegTarget);  // 2
      return false;
    }

    if (firstLeg.get()) {
      Assert(!secondLeg.get());
      m_lastError = &secondLegTarget;
      CloseLegPositionByOperationStartError(*firstLeg.get(), secondLegTarget,
                                            *operation);
    } else {
      Assert(secondLeg.get());
      m_lastError = &firstLegTarget;
      CloseLegPositionByOperationStartError(*secondLeg.get(), firstLegTarget,
                                            *operation);
    }
    m_self.GetLog().Warn(
        "\"%1%\" (%2% leg) added to the blacklist by position opening error. "
        "%3% leg is \"%4%\"",
        *m_lastError,                                         // 1
        firstLeg.get() ? "second" : "first",                  // 2
        secondLeg.get() ? "Second" : "First",                 // 3
        secondLeg.get() ? secondLegTarget : firstLegTarget);  // 4

    return false;
  }

  void CloseLegPositionByOperationStartError(
      Position &openedPosition,
      const Security &failedPositionTarget,
      Operation &operation) {
    m_controller->ClosePosition(openedPosition, CLOSE_REASON_OPEN_FAILED);
    operation.GetReportData().Add(BusinessOperationReportData::PositionReport{
        operation.GetId(), openedPosition.GetSubOperationId() == 1 ? 2 : 1,
        !openedPosition.IsLong(), pt::not_a_date_time, pt::not_a_date_time,
        operation.GetOpenOrderPolicy().GetOpenOrderPrice(
            !openedPosition.IsLong()),
        std::numeric_limits<double>::quiet_NaN(), 0,
        &operation.GetTradingSystem(m_self, failedPositionTarget),
        CLOSE_REASON_OPEN_FAILED,
        operation.GetTradingSystem(m_self, openedPosition.GetSecurity())
            .CalcCommission(openedPosition.GetOpenedVolume(),
                            openedPosition.GetSecurity())});
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
      m_pimpl(boost::make_unique<Implementation>(*this, conf)) {}

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
                      [&security](const AdviceSecuritySignal &stored) {
                        return stored.security == &security ||
                               stored.isBestBid || stored.isBestAsk;
                      }) == m_pimpl->m_symbols[security.GetSymbol()].cend());
  m_pimpl->m_symbols[security.GetSymbol()].emplace_back(
      AdviceSecuritySignal{&security});
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
  m_pimpl->m_lastError = nullptr;
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
  m_pimpl->m_controller->OnPositionUpdate(position);
}

void aa::Strategy::OnPostionsCloseRequest() {
  m_pimpl->m_controller->OnPostionsCloseRequest();
}
////////////////////////////////////////////////////////////////////////////////

boost::shared_ptr<trdk::Strategy> CreateStrategy(
    Context &context,
    const std::string &instanceName,
    const IniSectionRef &conf) {
  return boost::make_shared<aa::Strategy>(context, instanceName, conf);
}

////////////////////////////////////////////////////////////////////////////////
