/**************************************************************************
 *   Created: 2015/03/29 04:44:08
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#include "Prec.hpp"
#include "RiskControl.hpp"
#include "EventsLog.hpp"
#include "Security.hpp"
#include "Trade.hpp"
#include "TradingLog.hpp"

using namespace trdk;
using namespace Lib;
namespace pt = boost::posix_time;
namespace ptr = boost::property_tree;

////////////////////////////////////////////////////////////////////////////////

namespace {

const char *const logPrefix = "RiskControl";

template <Lib::Concurrency::Profile profile>
struct ConcurrencyPolicyT {
  static_assert(profile == Lib::Concurrency::PROFILE_RELAX,
                "Wrong concurrency profile");
  typedef boost::mutex Mutex;
  typedef Mutex::scoped_lock Lock;
};
template <>
struct ConcurrencyPolicyT<Lib::Concurrency::PROFILE_HFT> {
  typedef Lib::Concurrency::SpinMutex Mutex;
  typedef Mutex::ScopedLock Lock;
};
}  // namespace

////////////////////////////////////////////////////////////////////////////////

class RiskControlSymbolContext::Position : private boost::noncopyable {
 public:
  const Lib::Currency currency;

  const Volume shortLimit;
  const Volume longLimit;

  Volume position;

 public:
  explicit Position(const Lib::Currency &currency,
                    const Volume &shortLimit,
                    const Volume &longLimit)
      : currency(currency),
        shortLimit(shortLimit),
        longLimit(longLimit),
        position(0) {}
};

struct RiskControlSymbolContext::Side : private boost::noncopyable {
  int8_t direction;
  const char *name;

  explicit Side(int8_t direction)
      : direction(direction), name(direction < 1 ? "short" : "long") {
    AssertNe(0, direction);
  }
};

struct RiskControlSymbolContext::Scope : private boost::noncopyable {
  Side shortSide;
  Side longSide;

  boost::shared_ptr<Position> baseCurrencyPosition;
  boost::shared_ptr<Position> quoteCurrencyPosition;

  Scope() : shortSide(-1), longSide(1) {}
};

////////////////////////////////////////////////////////////////////////////////

RiskControlScope::RiskControlScope(const TradingMode &tradingMode)
    : m_tradingMode(tradingMode) {}

RiskControlScope::~RiskControlScope() {}

const TradingMode &RiskControlScope::GetTradingMode() const {
  return m_tradingMode;
}

EmptyRiskControlScope::EmptyRiskControlScope(const TradingMode &tradingMode,
                                             const std::string &name)
    : RiskControlScope(tradingMode), m_name(name) {}
EmptyRiskControlScope::~EmptyRiskControlScope() {}
const std::string &EmptyRiskControlScope::GetName() const { return m_name; }
void EmptyRiskControlScope::CheckNewBuyOrder(const RiskControlOperationId &,
                                             Security &,
                                             const Currency &,
                                             const Qty &,
                                             const Price &) {}
void EmptyRiskControlScope::CheckNewSellOrder(const RiskControlOperationId &,
                                              Security &,
                                              const Currency &,
                                              const Qty &,
                                              const Price &) {}
void EmptyRiskControlScope::ConfirmBuyOrder(const RiskControlOperationId &,
                                            const OrderStatus &,
                                            Security &,
                                            const Currency &,
                                            const Price & /*orderPrice*/,
                                            const Qty & /*remainingQty*/,
                                            const Trade *) {}
void EmptyRiskControlScope::ConfirmSellOrder(const RiskControlOperationId &,
                                             const OrderStatus &,
                                             Security &,
                                             const Currency &,
                                             const Price & /*orderPrice*/,
                                             const Qty & /*remainingQty*/,
                                             const Trade *) {}
void EmptyRiskControlScope::CheckTotalPnl(const Volume &) const {}
void EmptyRiskControlScope::CheckTotalWinRatio(
    size_t /*totalWinRatio*/, size_t /*operationsCount*/) const {}
void EmptyRiskControlScope::OnSettingsUpdate(const ptr::ptree &) {}

class StandardRiskControlScope : public RiskControlScope {
 protected:
  typedef RiskControl::WrongSettingsException WrongSettingsException;
  typedef RiskControl::NumberOfOrdersLimitException
      NumberOfOrdersLimitException;
  typedef RiskControl::WrongOrderParameterException
      WrongOrderParameterException;
  typedef RiskControl::NotEnoughFundsException NotEnoughFundsException;
  typedef RiskControl::PnlIsOutOfRangeException PnlIsOutOfRangeException;
  typedef RiskControl::WinRatioIsOutOfRangeException
      WinRatioIsOutOfRangeException;

  struct Settings {
   public:
    pt::time_duration ordersFloodControlPeriod;
    std::pair<Volume, Volume> pnl;

    size_t winRatioFirstOperationsToSkip;
    uint16_t winRatioMinValue;

   public:
    explicit Settings(const pt::time_duration &ordersFloodControlPeriod,
                      const Volume &minPnl,
                      const Volume &maxPnl,
                      size_t winRatioFirstOperationsToSkip,
                      uint16_t winRatioMinValue)
        : ordersFloodControlPeriod(ordersFloodControlPeriod),
          pnl(std::make_pair(-minPnl, maxPnl)),
          winRatioFirstOperationsToSkip(winRatioFirstOperationsToSkip),
          winRatioMinValue(winRatioMinValue) {}
  };

 protected:
  typedef ConcurrencyPolicyT<TRDK_CONCURRENCY_PROFILE> ConcurrencyPolicy;
  typedef ConcurrencyPolicy::Mutex Mutex;
  typedef ConcurrencyPolicy::Lock Lock;

 private:
  typedef boost::circular_buffer<pt::ptime> FloodControlBuffer;

 public:
  explicit StandardRiskControlScope(Context &context,
                                    const std::string &name,
                                    size_t index,
                                    const TradingMode &tradingMode,
                                    const Settings &settings,
                                    size_t maxOrdersNumber)
      : RiskControlScope(tradingMode),
        m_context(context),
        m_name(ConvertToString(GetTradingMode()) + "." + name),
        m_index(index),
        m_log(logPrefix, m_context.GetLog()),
        m_tradingLog(logPrefix, m_context.GetTradingLog()),
        m_settings(settings),
        m_orderTimePoints(maxOrdersNumber) {
    m_log.Info(
        "Orders flood control for scope \"%1%\":"
        " not more than %2% orders per %3%.",
        m_name, m_orderTimePoints.capacity(),
        m_settings.ordersFloodControlPeriod);
    m_log.Info("Max profit for scope \"%1%\": %2$f; max loss: %3$f.", m_name,
               m_settings.pnl.second, fabs(m_settings.pnl.first));
    m_log.Info(
        "Min win-ratio for scope \"%1%\": %2%%%"
        " (skip first %3% operations).",
        m_name, m_settings.winRatioMinValue,
        m_settings.winRatioFirstOperationsToSkip);

    if (m_orderTimePoints.capacity() <= 0 ||
        m_settings.ordersFloodControlPeriod.total_nanoseconds() <= 0) {
      throw WrongSettingsException("Wrong Order Flood Control settings");
    }

    if (m_settings.pnl.first == 0 || m_settings.pnl.second == 0 ||
        m_settings.pnl.first > .1 || m_settings.pnl.second > .1) {
      throw WrongSettingsException("Wrong P&L available range set");
    }

    if (m_settings.winRatioMinValue < 0 || m_settings.winRatioMinValue > 100) {
      throw WrongSettingsException("Wrong Min win-ratio set");
    }
  }

  virtual ~StandardRiskControlScope() {}

 public:
  virtual const std::string &GetName() const { return m_name; }

 public:
  virtual void CheckNewBuyOrder(const RiskControlOperationId &operationId,
                                Security &security,
                                const Currency &currency,
                                const Qty &qty,
                                const Price &price) {
    CheckNewOrder(operationId, security, currency, qty, price,
                  security.GetRiskControlContext(GetTradingMode())
                      .GetScope(m_index)
                      .longSide);
  }

  virtual void CheckNewSellOrder(const RiskControlOperationId &operationId,
                                 Security &security,
                                 const Currency &currency,
                                 const Qty &qty,
                                 const Price &price) {
    CheckNewOrder(operationId, security, currency, qty, price,
                  security.GetRiskControlContext(GetTradingMode())
                      .GetScope(m_index)
                      .shortSide);
  }

  virtual void ConfirmBuyOrder(const RiskControlOperationId &operationId,
                               const OrderStatus &status,
                               Security &security,
                               const Lib::Currency &currency,
                               const Price &orderPrice,
                               const Qty &remainingQty,
                               const Trade *trade) {
    ConfirmOrder(operationId, status, security, currency, orderPrice,
                 remainingQty, trade,
                 security.GetRiskControlContext(GetTradingMode())
                     .GetScope(m_index)
                     .longSide);
  }

  virtual void ConfirmSellOrder(const RiskControlOperationId &operationId,
                                const OrderStatus &status,
                                Security &security,
                                const Currency &currency,
                                const Price &orderPrice,
                                const Qty &remainingQty,
                                const Trade *trade) {
    ConfirmOrder(operationId, status, security, currency, orderPrice,
                 remainingQty, trade,
                 security.GetRiskControlContext(GetTradingMode())
                     .GetScope(m_index)
                     .shortSide);
  }

 public:
  virtual void CheckTotalPnl(const Volume &pnl) const {
    if (pnl < 0) {
      if (pnl < m_settings.pnl.first) {
        m_tradingLog.Write(
            "Total loss is out of allowed PnL range for scope \"%1%\":"
            " %2$f, but can't be more than %3$f.",
            [&](TradingRecord &record) {
              record % GetName() % fabs(pnl) % fabs(m_settings.pnl.first);
            });
        throw PnlIsOutOfRangeException(
            "Total loss is out of allowed PnL range");
      }

    } else if (pnl > m_settings.pnl.second) {
      m_tradingLog.Write(
          "Total profit is out of allowed PnL range for scope \"%1%\":"
          " %2$f, but can't be more than %3$f.",
          [&](TradingRecord &record) {
            record % GetName() % pnl % m_settings.pnl.second;
          });
      throw PnlIsOutOfRangeException(
          "Total profit is out of allowed PnL range");
    }
  }

  virtual void CheckTotalWinRatio(size_t totalWinRatio,
                                  size_t operationsCount) const {
    AssertGe(100, totalWinRatio);
    if (operationsCount >= m_settings.winRatioFirstOperationsToSkip &&
        totalWinRatio < m_settings.winRatioMinValue) {
      m_tradingLog.Write(
          "Total win-ratio is too small for scope \"%1%\":"
          " %2%%%, but can't be less than %3%%%.",
          [&](TradingRecord &record) {
            record % GetName() % totalWinRatio % m_settings.winRatioMinValue;
          });
      throw WinRatioIsOutOfRangeException("Total win-ratio is too small");
    }
  }

 protected:
  ModuleTradingLog &GetTradingLog() const { return m_tradingLog; }

  void SetSettings(const Settings &newSettings, size_t maxOrdersNumber) {
    m_settings = newSettings;
    m_orderTimePoints.set_capacity(maxOrdersNumber);
  }

  virtual void OnSettingsUpdate(const ptr::ptree &) {
    m_log.Warn("Failed to update current positions rest!");
  }

 private:
  void CheckNewOrder(const RiskControlOperationId &operationId,
                     Security &security,
                     const Currency &currency,
                     const Qty &qty,
                     const Price &price,
                     RiskControlSymbolContext::Side &side) {
    CheckOrdersFloodLevel();
    BlockFunds(operationId, security, currency, qty, price, side);
  }

  void ConfirmOrder(const RiskControlOperationId &operationId,
                    const OrderStatus &status,
                    Security &security,
                    const Currency &currency,
                    const Price &orderPrice,
                    const Qty &remainingQty,
                    const Trade *trade,
                    RiskControlSymbolContext::Side &side) {
    ConfirmUsedFunds(operationId, status, security, currency, orderPrice,
                     remainingQty, trade, side);
  }

  void CheckOrdersFloodLevel() {
    const auto &now = m_context.GetCurrentTime();
    const auto &oldestTime = now - m_settings.ordersFloodControlPeriod;

    const Lock lock(m_mutex);

    while (!m_orderTimePoints.empty() &&
           oldestTime > m_orderTimePoints.front()) {
      m_orderTimePoints.pop_front();
    }

    if (m_orderTimePoints.size() >= m_orderTimePoints.capacity()) {
      AssertLt(0, m_orderTimePoints.capacity());
      AssertEq(m_orderTimePoints.capacity(), m_orderTimePoints.size());
      m_tradingLog.Write(
          "Number of orders for period limit is reached for scope \"%1%\""
          ": %2% orders over the past  %3% (%4% -> %5%)"
          ", but allowed not more than %6%.",
          [&](TradingRecord &record) {
            record % GetName() % (m_orderTimePoints.size() + 1) %
                m_settings.ordersFloodControlPeriod %
                m_orderTimePoints.front() % m_orderTimePoints.back() %
                m_orderTimePoints.capacity();
          });
      throw NumberOfOrdersLimitException(
          "Number of orders for period limit is reached");
    } else if (m_orderTimePoints.size() + 1 >= m_orderTimePoints.capacity() &&
               !m_orderTimePoints.empty()) {
      m_tradingLog.Write(
          "Number of orders for period limit"
          " will be reached with next order for scope \"%1%\""
          ": %2% orders over the past %3% (%4% -> %5%)"
          ", allowed not more than %6%.",
          [&](TradingRecord &record) {
            record % GetName() % (m_orderTimePoints.size() + 1) %
                m_settings.ordersFloodControlPeriod %
                m_orderTimePoints.front() % m_orderTimePoints.back() %
                m_orderTimePoints.capacity();
          });
    }

    m_orderTimePoints.push_back(now);
  }

 private:
  //! Calculates order volumes.
  /** @return First value - order subject (base currency, security and so on),
   *         second value - money or quote currency.
   */
  static std::pair<Volume, Volume> CalcOrderVolumes(
      const Security &security,
      const Currency &currency,
      const Qty &qty,
      const Price &orderPrice,
      const RiskControlSymbolContext::Side &side) {
    const Symbol &symbol = security.GetSymbol();

    AssertEq(SECURITY_TYPE_FOR, symbol.GetSecurityType());
    AssertNe(symbol.GetFotBaseCurrency(), symbol.GetFotQuoteCurrency());
    Assert(symbol.GetFotBaseCurrency() == currency ||
           symbol.GetFotQuoteCurrency() == currency);

    const auto quoteCurrencyDirection = side.direction * -1;

    //! @sa See TRDK-107, TRDK-176 and TRDK-110 for order side details. But we
    //!     don't any such logic here - we have "logic order side", it's enough
    //!     to calculate the volume. Trading system will do the same, but using
    //!     "native order side".
    if (symbol.GetFotBaseCurrency() == currency) {
      return std::make_pair(qty * side.direction,
                            (qty * orderPrice) * quoteCurrencyDirection);
    } else {
      return std::make_pair((qty / orderPrice) * side.direction,
                            qty * quoteCurrencyDirection);
    }
  }

  static Volume CalcFundsRest(
      const Volume &position,
      const RiskControlSymbolContext::Position &limits) {
    return position < 0 ? limits.shortLimit + position
                        : limits.longLimit - position;
  }

  void BlockFunds(const RiskControlOperationId &operationId,
                  Security &security,
                  const Currency &currency,
                  const Qty &qty,
                  const Price &orderPrice,
                  const RiskControlSymbolContext::Side &side) const {
    const Symbol &symbol = security.GetSymbol();
    if (symbol.GetSecurityType() != SECURITY_TYPE_FOR) {
      throw WrongSettingsException("Unknown security type");
    }

    RiskControlSymbolContext::Scope &context =
        security.GetRiskControlContext(GetTradingMode()).GetScope(m_index);
    RiskControlSymbolContext::Position &baseCurrency =
        *context.baseCurrencyPosition;
    RiskControlSymbolContext::Position &quoteCurrency =
        *context.quoteCurrencyPosition;

    const auto &blocked =
        CalcOrderVolumes(security, currency, qty, orderPrice, side);
    const auto &newPosition =
        std::make_pair(baseCurrency.position + blocked.first,
                       quoteCurrency.position + blocked.second);
    const auto &rest =
        std::make_pair(CalcFundsRest(newPosition.first, baseCurrency),
                       CalcFundsRest(newPosition.second, quoteCurrency));

    m_tradingLog.Write(
        "funds\tblock\t%1%\t%2%\t%3%\t%4%\t%5$f - %6$f =\t%7$f\t%8$f",
        [&](TradingRecord &record) {
          record % GetName() % side.name % operationId %
              symbol.GetFotBaseCurrency() % baseCurrency.position %
              blocked.first % newPosition.first % rest.first;
        });
    m_tradingLog.Write(
        "funds\tblock\t%1%\t%2%\t%3%\t%4%\t%5$f - %6$f =\t%7$f\t%8$f",
        [&](TradingRecord &record) {
          record % GetName() % side.name % operationId %
              symbol.GetFotQuoteCurrency() % quoteCurrency.position %
              blocked.second % newPosition.second % rest.second;
        });

    if (rest.first < 0) {
      throw NotEnoughFundsException("Not enough funds for new order");
    } else if (rest.second < 0) {
      throw NotEnoughFundsException("Not enough funds for new order");
    }

    SetPositions(newPosition.first, baseCurrency, newPosition.second,
                 quoteCurrency);
  }

  void ConfirmUsedFunds(const RiskControlOperationId &operationId,
                        const OrderStatus &status,
                        Security &security,
                        const Currency &currency,
                        const Price &orderPrice,
                        const Qty &remainingQty,
                        const Trade *trade,
                        const RiskControlSymbolContext::Side &side) {
    static_assert(numberOfOrderStatuses == 7, "Status list changed.");
    switch (status) {
      default:
        AssertEq(ORDER_STATUS_ERROR, status);
        return;
      case ORDER_STATUS_SENT:
      case ORDER_STATUS_OPENED:
      case ORDER_STATUS_FILLED_FULLY:
      case ORDER_STATUS_FILLED_PARTIALLY:
        if (!trade) {
          throw Exception("Filled order has no trade information");
        }
        AssertNe(0, trade->price);
        ConfirmBlockedFunds(operationId, security, currency, orderPrice, *trade,
                            side);
        break;
      case ORDER_STATUS_CANCELED:
      case ORDER_STATUS_REJECTED:
      case ORDER_STATUS_ERROR:
        Assert(!trade);
        UnblockFunds(operationId, security, currency, orderPrice, remainingQty,
                     side);
        break;
    }
  }

  void ConfirmBlockedFunds(const RiskControlOperationId &operationId,
                           Security &security,
                           const Currency &currency,
                           const Price &orderPrice,
                           const Trade &trade,
                           const RiskControlSymbolContext::Side &side) {
    const Symbol &symbol = security.GetSymbol();
    if (symbol.GetSecurityType() != SECURITY_TYPE_FOR) {
      throw WrongSettingsException("Unknown security type");
    }

    RiskControlSymbolContext::Scope &context =
        security.GetRiskControlContext(GetTradingMode()).GetScope(m_index);
    RiskControlSymbolContext::Position &baseCurrency =
        *context.baseCurrencyPosition;
    RiskControlSymbolContext::Position &quoteCurrency =
        *context.quoteCurrencyPosition;

    const auto &blocked =
        CalcOrderVolumes(security, currency, trade.qty, orderPrice, side);
    const auto &used =
        CalcOrderVolumes(security, currency, trade.qty, trade.price, side);

    const std::pair<Volume, Volume> &newPosition =
        std::make_pair(baseCurrency.position - blocked.first + used.first,
                       quoteCurrency.position - blocked.second + used.second);

    m_tradingLog.Write(
        "funds\tuse\t%1%\t%2%\t%3%\t%4%\t%5$f - %6$f +  %7$f =\t%8$f\t%9$f",
        [&](TradingRecord &record) {
          record % GetName() % side.name % operationId %
              symbol.GetFotBaseCurrency() % baseCurrency.position %
              blocked.first % used.first % newPosition.first %
              CalcFundsRest(newPosition.first, baseCurrency);
        });
    m_tradingLog.Write(
        "funds\tuse\t%1%\t%2%\t%3%\t%4%\t%5$f - %6$f +  %7$f =\t%8$f\t%9$f",
        [&](TradingRecord &record) {
          record % GetName() % side.name % operationId %
              symbol.GetFotQuoteCurrency() % quoteCurrency.position %
              blocked.second % used.second % newPosition.second %
              CalcFundsRest(newPosition.second, quoteCurrency);
        });

    SetPositions(newPosition.first, baseCurrency, newPosition.second,
                 quoteCurrency);
  }

  void UnblockFunds(const RiskControlOperationId &operationId,
                    Security &security,
                    const Currency &currency,
                    const Price &orderPrice,
                    const Qty &remainingQty,
                    const RiskControlSymbolContext::Side &side) {
    const Symbol &symbol = security.GetSymbol();
    if (symbol.GetSecurityType() != SECURITY_TYPE_FOR) {
      throw WrongSettingsException("Unknown security type");
    }

    RiskControlSymbolContext::Scope &context =
        security.GetRiskControlContext(GetTradingMode()).GetScope(m_index);
    RiskControlSymbolContext::Position &baseCurrency =
        *context.baseCurrencyPosition;
    RiskControlSymbolContext::Position &quoteCurrency =
        *context.quoteCurrencyPosition;

    const auto &blocked =
        CalcOrderVolumes(security, currency, remainingQty, orderPrice, side);
    const auto &newPosition =
        std::make_pair(baseCurrency.position - blocked.first,
                       quoteCurrency.position - blocked.second);

    m_tradingLog.Write(
        "funds\tunblock\t%1%\t%2%\t%3%\t%4%\t%5$f - %6$f =\t%7$f\t%8$f",
        [&](TradingRecord &record) {
          record % GetName() % side.name % operationId %
              symbol.GetFotBaseCurrency() % baseCurrency.position %
              blocked.first % newPosition.first %
              CalcFundsRest(newPosition.first, baseCurrency);
        });
    m_tradingLog.Write(
        "funds\tunblock\t%1%\t%2%\t%3%\t%4%\t%5$f - %6$f =\t%7$f\t%8$f",
        [&](TradingRecord &record) {
          record % GetName() % side.name % operationId %
              symbol.GetFotQuoteCurrency() % quoteCurrency.position %
              blocked.second % newPosition.second %
              CalcFundsRest(newPosition.second, quoteCurrency);
        });

    SetPositions(newPosition.first, baseCurrency, newPosition.second,
                 quoteCurrency);
  }

 private:
  virtual void SetPositions(
      const Volume &newBaseCurrencyValue,
      RiskControlSymbolContext::Position &baseCurrencyState,
      const Volume &newQuoteCurrencyValue,
      RiskControlSymbolContext::Position &quoteCurrencyState) const = 0;

 private:
  Context &m_context;

  const std::string m_name;
  size_t m_index;

  Mutex m_mutex;

  ModuleEventsLog m_log;
  mutable ModuleTradingLog m_tradingLog;

  Settings m_settings;

  FloodControlBuffer m_orderTimePoints;
};

class GlobalRiskControlScope : public StandardRiskControlScope {
 public:
  typedef StandardRiskControlScope Base;

 public:
  explicit GlobalRiskControlScope(Context &context,
                                  const ptr::ptree &conf,
                                  const std::string &name,
                                  size_t index,
                                  const TradingMode &tradingMode)
      : Base(context,
             name,
             index,
             tradingMode,
             ReadSettings(conf),
             conf.get<size_t>("floodControlOrders.maxNumber")) {}

  virtual ~GlobalRiskControlScope() {}

  virtual void OnSettingsUpdate(const ptr::ptree &conf) {
    Base::OnSettingsUpdate(conf);
    SetSettings(ReadSettings(conf),
                conf.get<size_t>("floodControl.orders.maxNumber"));
  }

  Settings ReadSettings(const ptr::ptree &conf) {
    return Settings(
        pt::milliseconds(conf.get<size_t>("floodControl.orders.periodMs")),
        conf.get<double>("pnl.loss"), conf.get<double>("pnl.profit"),
        conf.get<uint16_t>("winRatio.firstOperationsToSkip"),
        conf.get<uint16_t>("winRatio.min"));
  }

 private:
  virtual void SetPositions(
      const Volume &newBaseCurrencyValue,
      RiskControlSymbolContext::Position &baseCurrencyState,
      const Volume &newQuoteCurrencyValue,
      RiskControlSymbolContext::Position &quoteCurrencyState) const {
    baseCurrencyState.position = newBaseCurrencyValue;
    quoteCurrencyState.position = newQuoteCurrencyValue;
  }
};

class LocalRiskControlScope : public StandardRiskControlScope {
 public:
  typedef StandardRiskControlScope Base;

  explicit LocalRiskControlScope(Context &context,
                                 const ptr::ptree &conf,
                                 const std::string &name,
                                 const size_t index,
                                 const TradingMode &tradingMode)
      : Base(context,
             name,
             index,
             tradingMode,
             ReadSettings(conf),
             conf.get<size_t>("riskControl.floodControl.orders.maxNumber")) {}

  virtual ~LocalRiskControlScope() {}

  virtual void OnSettingsUpdate(const ptr::ptree &conf) {
    Base::OnSettingsUpdate(conf);
    SetSettings(ReadSettings(conf),
                conf.get<size_t>("riskControl.floodControl.orders.maxNumber"));
  }

  Settings ReadSettings(const ptr::ptree &conf) {
    return Settings(
        pt::milliseconds(
            conf.get<size_t>("riskControl.floodControl.orders.periodMs")),
        conf.get<double>("riskControl.pnl.loss"),
        conf.get<double>("riskControl.pnl.profit"),
        conf.get<uint16_t>("riskControl.winRatio.firstOperationsToSkip"),
        conf.get<uint16_t>("riskControl.winRatio.min"));
  }

 private:
  virtual void SetPositions(
      const Volume &newbaseCurrencyValue,
      RiskControlSymbolContext::Position &baseCurrencyState,
      const Volume &newQuoteCurrencyValue,
      RiskControlSymbolContext::Position &quoteCurrencyState) const {
    baseCurrencyState.position = newbaseCurrencyValue;
    quoteCurrencyState.position = newQuoteCurrencyValue;
  }
};

//////////////////////////////////////////////////////////////////////////

RiskControlSymbolContext::RiskControlSymbolContext(
    const Symbol &symbol,
    const ptr::ptree &conf,
    const PositionFabric &positionFabric)
    : m_symbol(symbol) {
  m_scopes.emplace_back(new Scope);
  InitScope(*m_scopes.back(), conf, positionFabric, false);
}

const Symbol &RiskControlSymbolContext::GetSymbol() const { return m_symbol; }

void RiskControlSymbolContext::AddScope(const ptr::ptree &conf,
                                        const PositionFabric &posFabric,
                                        const size_t index) {
  if (index <= m_scopes.size()) {
    m_scopes.resize(index + 1);
  }
  m_scopes[index].reset(new Scope);
  InitScope(*m_scopes[index], conf, posFabric, true);
}

void RiskControlSymbolContext::InitScope(Scope &scope,
                                         const ptr::ptree &conf,
                                         const PositionFabric &posFabric,
                                         bool isAdditinalScope) const {
  {
    const auto &readLimit = [&](const Currency &currency, const char *type) {
      boost::format key("%1%.limit.%2%");
      key % currency % type;
      return conf.get<double>(!isAdditinalScope
                                  ? key.str()
                                  : std::string("riskControl.") + key.str(),
                              0);
    };

    scope.baseCurrencyPosition =
        posFabric(m_symbol.GetFotBaseCurrency(),
                  readLimit(m_symbol.GetFotBaseCurrency(), "short"),
                  readLimit(m_symbol.GetFotBaseCurrency(), "long"));
    scope.quoteCurrencyPosition =
        posFabric(m_symbol.GetFotQuoteCurrency(),
                  readLimit(m_symbol.GetFotQuoteCurrency(), "short"),
                  readLimit(m_symbol.GetFotQuoteCurrency(), "long"));
  }
}

RiskControlSymbolContext::Scope &RiskControlSymbolContext::GetScope(
    size_t index) {
  AssertGt(m_scopes.size(), index);
  return *m_scopes[index];
}

RiskControlSymbolContext::Scope &RiskControlSymbolContext::GetGlobalScope() {
  AssertGt(m_scopes.size(), 0);
  return *m_scopes[0];
}

size_t RiskControlSymbolContext::GetScopesNumber() const {
  return m_scopes.size();
}

////////////////////////////////////////////////////////////////////////////////

RiskControl::Exception::Exception(const char *what) throw()
    : RiskControlException(what) {}

RiskControl::WrongSettingsException::WrongSettingsException(
    const char *what) throw()
    : Exception(what) {}

RiskControl::NumberOfOrdersLimitException::NumberOfOrdersLimitException(
    const char *what) throw()
    : Exception(what) {}

RiskControl::NotEnoughFundsException::NotEnoughFundsException(
    const char *what) throw()
    : Exception(what) {}

RiskControl::WrongOrderParameterException::WrongOrderParameterException(
    const char *what) throw()
    : Exception(what) {}

RiskControl::PnlIsOutOfRangeException::PnlIsOutOfRangeException(
    const char *what) throw()
    : Exception(what) {}

RiskControl::WinRatioIsOutOfRangeException::WinRatioIsOutOfRangeException(
    const char *what) throw()
    : Exception(what) {}

////////////////////////////////////////////////////////////////////////////////

class RiskControl::Implementation : boost::noncopyable {
 public:
  typedef std::map<Currency,
                   boost::shared_ptr<RiskControlSymbolContext::Position>>
      PositionsCache;

  struct ScopeInfo {
    std::string name;
    ptr::ptree conf;
    PositionsCache positionsCache;

    explicit ScopeInfo(const std::string &name, const ptr::ptree &conf)
        : name(name), conf(conf) {}
  };

  Context &m_context;
  mutable ModuleEventsLog m_log;
  ModuleTradingLog m_tradingLog;

  const ptr::ptree m_conf;

  const TradingMode m_tradingMode;

  PositionsCache m_globalScopePositionsCache;
  std::vector<boost::shared_ptr<RiskControlSymbolContext>> m_symbols;

  // Item can't be ptr as we will copy it when will create new scope.
  std::vector<ScopeInfo> m_additionalScopesInfo;

  std::unique_ptr<GlobalRiskControlScope> m_globalScope;

  boost::atomic<RiskControlOperationId> m_lastOperationId;

 public:
  explicit Implementation(Context &context,
                          const ptr::ptree &conf,
                          const TradingMode &tradingMode)
      : m_context(context),
        m_log(logPrefix, m_context.GetLog()),
        m_tradingLog(logPrefix, m_context.GetTradingLog()),
        m_conf(conf),
        m_tradingMode(tradingMode),
        m_lastOperationId(0) {
    if (m_conf.get<bool>("isEnabled")) {
      m_globalScope.reset(new GlobalRiskControlScope(
          m_context, m_conf, "Global", m_additionalScopesInfo.size(),
          m_tradingMode));
    }
  }

  boost::shared_ptr<RiskControlSymbolContext::Position> CreatePosition(
      const std::string &scopeName,
      PositionsCache &cache,
      const Currency &currency,
      const Volume &shortLimit,
      const Volume &longLimit) const {
    {
      const auto &cacheIt = cache.find(currency);
      if (cacheIt != cache.cend()) {
        AssertEq(shortLimit, cacheIt->second->shortLimit);
        AssertEq(longLimit, cacheIt->second->longLimit);
        return cacheIt->second;
      }
    }
    const boost::shared_ptr<RiskControlSymbolContext::Position> pos(
        new RiskControlSymbolContext::Position(currency, shortLimit,
                                               longLimit));
    m_log.Info(
        "%1% position limits for scope \"%2%\": short = %3$f, long = %4$f.",
        currency, scopeName, pos->shortLimit, pos->longLimit);
    cache[currency] = pos;
    return pos;
  }

  void ReportSymbolScopeSettings(const RiskControlSymbolContext &,
                                 const RiskControlSymbolContext::Scope &,
                                 const std::string &) const {}

  void AddScope(size_t scopeIndex,
                ScopeInfo &scopeInfo,
                RiskControlSymbolContext &symbolContex) const {
    symbolContex.AddScope(
        scopeInfo.conf,
        boost::bind(&Implementation::CreatePosition, this,
                    ConvertToString(m_tradingMode) + "." + scopeInfo.name,
                    boost::ref(scopeInfo.positionsCache), _1, _2, _3),
        scopeIndex);
    ReportSymbolScopeSettings(symbolContex, symbolContex.GetScope(scopeIndex),
                              scopeInfo.name);
  }
};

////////////////////////////////////////////////////////////////////////////////

RiskControl::RiskControl(Context &context,
                         const ptr::ptree &conf,
                         const TradingMode &tradingMode)
    : m_pimpl(new Implementation(
          context, conf.get_child("riskControl"), tradingMode)) {}

RiskControl::~RiskControl() { delete m_pimpl; }

const TradingMode &RiskControl::GetTradingMode() const {
  return m_pimpl->m_tradingMode;
}

boost::shared_ptr<RiskControlSymbolContext> RiskControl::CreateSymbolContext(
    const Symbol &symbol) const {
  if (!m_pimpl->m_globalScope) {
    return boost::shared_ptr<RiskControlSymbolContext>();
  }

  auto posCache(m_pimpl->m_globalScopePositionsCache);
  auto scopesInfoCache(m_pimpl->m_additionalScopesInfo);

  boost::shared_ptr<RiskControlSymbolContext> result(
      new RiskControlSymbolContext(
          symbol, m_pimpl->m_conf,
          boost::bind(&Implementation::CreatePosition, m_pimpl,
                      boost::cref(m_pimpl->m_globalScope->GetName()),
                      boost::ref(posCache), _1, _2, _3)));
  {
    size_t scopeIndex = 1;
    for (auto &scopeInfo : scopesInfoCache) {
      m_pimpl->AddScope(scopeIndex, scopeInfo, *result);
      ++scopeIndex;
    }
  }

  m_pimpl->ReportSymbolScopeSettings(*result, result->GetGlobalScope(),
                                     m_pimpl->m_globalScope->GetName());
  m_pimpl->m_symbols.push_back(result);

  scopesInfoCache.swap(m_pimpl->m_additionalScopesInfo);
  posCache.swap(m_pimpl->m_globalScopePositionsCache);

  return result;
}

std::unique_ptr<RiskControlScope> RiskControl::CreateScope(
    const std::string &name, const ptr::ptree &conf) const {
  if (!m_pimpl->m_globalScope) {
    return boost::make_unique<EmptyRiskControlScope>(GetTradingMode(), name);
  }

  auto additionalScopesInfo(m_pimpl->m_additionalScopesInfo);
  additionalScopesInfo.emplace_back(name, conf);

  const auto scopeIndex = additionalScopesInfo.size();
  AssertLt(0, scopeIndex);  // zero - always Global scope

  Implementation::PositionsCache cache;
  for (auto &symbolContex : m_pimpl->m_symbols) {
    m_pimpl->AddScope(scopeIndex, additionalScopesInfo.back(), *symbolContex);
  }

  auto result = boost::make_unique<LocalRiskControlScope>(
      m_pimpl->m_context, conf, name, scopeIndex, GetTradingMode());

  additionalScopesInfo.swap(m_pimpl->m_additionalScopesInfo);

  return result;
}

RiskControlOperationId RiskControl::CheckNewBuyOrder(
    RiskControlScope &scope,
    Security &security,
    const Currency &currency,
    const Qty &qty,
    const Price &price,
    const TimeMeasurement::Milestones &timeMeasurement) {
  if (!m_pimpl->m_globalScope) {
    return 0;
  }
  timeMeasurement.Measure(TimeMeasurement::SM_PRE_RISK_CONTROL_START);
  const RiskControlOperationId operationId = ++m_pimpl->m_lastOperationId;
  scope.CheckNewBuyOrder(operationId, security, currency, qty, price);
  m_pimpl->m_globalScope->CheckNewBuyOrder(operationId, security, currency, qty,
                                           price);
  timeMeasurement.Measure(TimeMeasurement::SM_PRE_RISK_CONTROL_COMPLETE);
  return operationId;
}

RiskControlOperationId RiskControl::CheckNewSellOrder(
    RiskControlScope &scope,
    Security &security,
    const Currency &currency,
    const Qty &qty,
    const Price &price,
    const TimeMeasurement::Milestones &timeMeasurement) {
  if (!m_pimpl->m_globalScope) {
    return 0;
  }
  timeMeasurement.Measure(TimeMeasurement::SM_PRE_RISK_CONTROL_START);
  const RiskControlOperationId operationId = ++m_pimpl->m_lastOperationId;
  scope.CheckNewSellOrder(operationId, security, currency, qty, price);
  m_pimpl->m_globalScope->CheckNewSellOrder(operationId, security, currency,
                                            qty, price);
  timeMeasurement.Measure(TimeMeasurement::SM_PRE_RISK_CONTROL_COMPLETE);
  return operationId;
}

void RiskControl::ConfirmBuyOrder(
    const RiskControlOperationId &operationId,
    RiskControlScope &scope,
    const OrderStatus &orderStatus,
    Security &security,
    const Currency &currency,
    const Price &orderPrice,
    const Qty &remainingQty,
    const Trade *trade,
    const TimeMeasurement::Milestones &timeMeasurement) {
  if (!m_pimpl->m_globalScope) {
    AssertEq(0, operationId);
    return;
  }
  timeMeasurement.Measure(TimeMeasurement::SM_POST_RISK_CONTROL_START);
  m_pimpl->m_globalScope->ConfirmBuyOrder(operationId, orderStatus, security,
                                          currency, orderPrice, remainingQty,
                                          trade);
  scope.ConfirmBuyOrder(operationId, orderStatus, security, currency,
                        orderPrice, remainingQty, trade);
  timeMeasurement.Measure(TimeMeasurement::SM_POST_RISK_CONTROL_COMPLETE);
}

void RiskControl::ConfirmSellOrder(
    const RiskControlOperationId &operationId,
    RiskControlScope &scope,
    const OrderStatus &orderStatus,
    Security &security,
    const Currency &currency,
    const Price &orderPrice,
    const Qty &remainingQty,
    const Trade *trade,
    const TimeMeasurement::Milestones &timeMeasurement) {
  if (!m_pimpl->m_globalScope) {
    AssertEq(0, operationId);
    return;
  }
  timeMeasurement.Measure(TimeMeasurement::SM_POST_RISK_CONTROL_START);
  m_pimpl->m_globalScope->ConfirmSellOrder(operationId, orderStatus, security,
                                           currency, orderPrice, remainingQty,
                                           trade);
  scope.ConfirmSellOrder(operationId, orderStatus, security, currency,
                         orderPrice, remainingQty, trade);
  timeMeasurement.Measure(TimeMeasurement::SM_POST_RISK_CONTROL_COMPLETE);
}

void RiskControl::CheckTotalPnl(const RiskControlScope &scope,
                                const Volume &pnl) const {
  scope.CheckTotalPnl(pnl);
  m_pimpl->m_globalScope->CheckTotalPnl(pnl);
}

void RiskControl::CheckTotalWinRatio(const RiskControlScope &scope,
                                     size_t totalWinRatio,
                                     size_t operationsCount) const {
  if (!m_pimpl->m_globalScope) {
    return;
  }
  AssertGe(100, totalWinRatio);
  scope.CheckTotalWinRatio(totalWinRatio, operationsCount);
  m_pimpl->m_globalScope->CheckTotalWinRatio(totalWinRatio, operationsCount);
}

void RiskControl::OnSettingsUpdate(const ptr::ptree &conf) {
  if (!m_pimpl->m_globalScope) {
    return;
  }
  m_pimpl->m_globalScope->OnSettingsUpdate(conf.get_child("riskControl"));
}

////////////////////////////////////////////////////////////////////////////////
