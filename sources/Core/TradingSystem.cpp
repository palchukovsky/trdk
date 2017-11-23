/**************************************************************************
 *   Created: 2012/07/24 10:07:02
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#include "Prec.hpp"
#include "TradingSystem.hpp"
#include "DropCopy.hpp"
#include "RiskControl.hpp"
#include "Security.hpp"
#include "Timer.hpp"
#include "TradingLog.hpp"
#include "TransactionContext.hpp"

using namespace trdk;
using namespace trdk::Lib;
using namespace trdk::Lib::TimeMeasurement;

namespace pt = boost::posix_time;

//////////////////////////////////////////////////////////////////////////

TradingSystem::Error::Error(const char *what) noexcept : Base::Error(what) {}

TradingSystem::OrderParamsError::OrderParamsError(const char *what,
                                                  const Qty &,
                                                  const OrderParams &) noexcept
    : Error(what) {}

TradingSystem::OrderIsUnknown::OrderIsUnknown(const char *what) noexcept
    : Error(what) {}

TradingSystem::ConnectionDoesntExistError::ConnectionDoesntExistError(
    const char *what) noexcept
    : Error(what) {}

//////////////////////////////////////////////////////////////////////////

namespace {

std::string FormatStringId(const std::string &instanceName,
                           const TradingMode &mode) {
  std::string result("TradingSystem");
  if (!instanceName.empty()) {
    result += '.';
    result += instanceName;
  }
  result += '.';
  result += ConvertToString(mode);
  return result;
}

template <trdk::Lib::Concurrency::Profile profile>
struct ActiveOrderConcurrencyPolicy {
  static_assert(profile == trdk::Lib::Concurrency::PROFILE_RELAX,
                "Wrong concurrency profile");
  typedef boost::mutex Mutex;
  typedef Mutex::scoped_lock Lock;
};
template <>
struct ActiveOrderConcurrencyPolicy<trdk::Lib::Concurrency::PROFILE_HFT> {
  typedef trdk::Lib::Concurrency::SpinMutex Mutex;
  typedef Mutex::ScopedLock Lock;
};
typedef ActiveOrderConcurrencyPolicy<TRDK_CONCURRENCY_PROFILE>::Mutex
    ActiveOrderMutex;
typedef ActiveOrderConcurrencyPolicy<TRDK_CONCURRENCY_PROFILE>::Lock
    ActiveOrderLock;

struct ActiveOrder {
  boost::shared_ptr<OrderTransactionContext> transactionContext;
  TradingSystem::OrderStatusUpdateSlot statusUpdateSignal;
  Qty remainingQty;
  Price price;
  OrderStatus status;
  pt::ptime updateTime;
  std::unique_ptr<trdk::Timer::Scope> timerScope;

  bool IsChanged(const OrderStatus &rhsStatus,
                 const Qty &rhsRemainingQty) const {
    return status != rhsStatus || remainingQty != rhsRemainingQty;
  }
};
}

class TradingSystem::Implementation : private boost::noncopyable {
 public:
  TradingSystem &m_self;

  const TradingMode m_mode;

  const size_t m_index;

  Context &m_context;

  const std::string m_instanceName;
  const std::string m_stringId;

  TradingSystem::Log m_log;
  TradingSystem::TradingLog m_tradingLog;

  ActiveOrderMutex m_activeOrdersMutex;
  boost::unordered_map<OrderId, ActiveOrder> m_activeOrders;
  std::unique_ptr<Timer::Scope> m_lastOrderTimerScope;

  explicit Implementation(TradingSystem &self,
                          const TradingMode &mode,
                          size_t index,
                          Context &context,
                          const std::string &instanceName)
      : m_self(self),
        m_mode(mode),
        m_index(index),
        m_context(context),
        m_instanceName(instanceName),
        m_stringId(FormatStringId(m_instanceName, m_mode)),
        m_log(m_stringId, m_context.GetLog()),
        m_tradingLog(instanceName, m_context.GetTradingLog()) {}

  ~Implementation() {
    try {
      if (!m_activeOrders.empty()) {
        m_log.Warn("%1% orders still be active.", m_activeOrders.size());
      }
    } catch (...) {
      AssertFailNoException();
    }
  }

  void LogNewOrder(Security &security,
                   const Currency &currency,
                   const Qty &qty,
                   const boost::optional<Price> &orderPrice,
                   const Price &actualPrice,
                   const OrderSide &side,
                   const TimeInForce &tif) {
    m_tradingLog.Write(
        "{'order': {'new': {'side': '%1%', 'security': '%2%', 'currency': "
        "'%3%', 'type': '%4%', 'price': %5$.8f, 'qty': %6$.8f, 'tif': '%7%'}}}",
        [&](TradingRecord &record) {
          record % side                                              // 1
              % security                                             // 2
              % currency                                             // 3
              % (orderPrice ? ORDER_TYPE_LIMIT : ORDER_TYPE_MARKET)  // 4
              % actualPrice                                          // 5
              % qty                                                  // 6
              % tif;                                                 // 7
        });
  }
  void LogOrderSent(const OrderId &orderId) {
    m_tradingLog.Write("{'order': {'sent': {'id': '%1%'}}}",
                       [&](TradingRecord &record) { record % orderId; });
  }
  void LogOrderUpdate(const OrderId &orderId,
                      const OrderStatus &orderStatus,
                      const Qty &remainingQty,
                      const boost::optional<Volume> &commission,
                      const TradeInfo *trade) {
    m_tradingLog.Write(
        "{'order': {'status': {'status': '%1%', 'remainingQty': %2$.8f, "
        "'id': '%3%', 'commission': %4$.8f}}}",
        [&](TradingRecord &record) {
          record % orderStatus  // 1
              % remainingQty    // 2
              % orderId;        // 3
          if (commission) {
            record % *commission;  // 4
          } else {
            record % "null";  // 4
          }
        });
    if (trade) {
      m_tradingLog.Write(
          "{'order': {'trade': {'id': '%1%', 'qty': %2$.8f, 'price': %3$.8f}}}",
          [&trade](TradingRecord &record) {
            if (trade->id) {
              record % *trade->id;  // 1
            } else {
              record % "";  // 1
            }
            record % trade->qty  // 2
                % trade->price;  // 3
          });
    }
  }

  RiskControlOperationId CheckNewBuyOrder(RiskControlScope &riskControlScope,
                                          Security &security,
                                          const Currency &currency,
                                          const Qty &qty,
                                          const Price &price,
                                          const Milestones &delaysMeasurement) {
    return m_context.GetRiskControl(m_mode).CheckNewBuyOrder(
        riskControlScope, security, currency, qty, price, delaysMeasurement);
  }
  RiskControlOperationId CheckNewSellOrder(RiskControlScope &riskControlScope,
                                           Security &security,
                                           const Currency &currency,
                                           const Qty &qty,
                                           const Price &price,
                                           const Milestones &timeMeasurement) {
    return m_context.GetRiskControl(m_mode).CheckNewSellOrder(
        riskControlScope, security, currency, qty, price, timeMeasurement);
  }

  void ConfirmBuyOrder(const RiskControlOperationId &riskControlOperationId,
                       RiskControlScope &riskControlScope,
                       const OrderStatus &orderStatus,
                       Security &security,
                       const Currency &currency,
                       const Price &orderPrice,
                       const Qty &remainingQty,
                       const TradeInfo *trade,
                       const Milestones &timeMeasurement) {
    m_context.GetRiskControl(m_mode).ConfirmBuyOrder(
        riskControlOperationId, riskControlScope, orderStatus, security,
        currency, orderPrice, remainingQty, trade, timeMeasurement);
  }

  void ConfirmSellOrder(const RiskControlOperationId &operationId,
                        RiskControlScope &riskControlScope,
                        const OrderStatus &orderStatus,
                        Security &security,
                        const Currency &currency,
                        const Price &orderPrice,
                        const Qty &remainingQty,
                        const TradeInfo *trade,
                        const Milestones &timeMeasurement) {
    m_context.GetRiskControl(m_mode).ConfirmSellOrder(
        operationId, riskControlScope, orderStatus, security, currency,
        orderPrice, remainingQty, trade, timeMeasurement);
  }

  void RegisterCallback(
      const boost::shared_ptr<OrderTransactionContext> &transactionContext,
      const OrderStatusUpdateSlot &&callback,
      const Qty &qty,
      const Price &price) {
    auto result = m_activeOrders.emplace(
        transactionContext->GetOrderId(),
        ActiveOrder{transactionContext, std::move(callback), qty, price,
                    ORDER_STATUS_SENT});
    if (!result.second) {
      m_log.Error("Order ID %1% is not unique.",
                  transactionContext->GetOrderId());
      throw Error("Order ID is not unique");
    }
    Assert(!result.first->second.timerScope);
    if (m_lastOrderTimerScope) {
      result.first->second.timerScope = std::move(m_lastOrderTimerScope);
    }
  }

  void OnOrderStatusUpdate(
      const OrderId &orderId,
      const OrderStatus &status,
      const boost::optional<Qty> &remainingQty,
      const boost::optional<Volume> &commission,
      boost::optional<TradeInfo> &&tradeInfo,
      const boost::function<void(OrderTransactionContext &)> &callback) {
    const auto &time = m_context.GetCurrentTime();
    ActiveOrderLock lock(m_activeOrdersMutex);
    const auto &it = m_activeOrders.find(orderId);
    if (it == m_activeOrders.cend()) {
      m_log.Warn(
          "Failed to handle status update for order %1% as order is unknown.",
          orderId);
      throw OrderIsUnknown(
          "Failed to handle status update for order as order is unknown");
    }
    OnOrderStatusUpdate(orderId, status, remainingQty, commission,
                        std::move(tradeInfo), time, it, std::move(lock),
                        callback);
  }

  void OnOrderStatusUpdate(
      const OrderId &orderId,
      OrderStatus status,
      const boost::optional<Qty> &remainingQty,
      const boost::optional<Volume> &commission,
      boost::optional<TradeInfo> &&tradeInfo,
      const pt::ptime &time,
      const boost::unordered_map<OrderId, ActiveOrder>::iterator &it,
      ActiveOrderLock &&lock,
      const boost::function<void(OrderTransactionContext &)> &callback) {
    auto &cache = it->second;

    static_assert(numberOfOrderStatuses == 8, "List changed.");
    switch (status) {
      case ORDER_STATUS_SENT:
      case ORDER_STATUS_SUBMITTED:
        switch (cache.status) {
          case ORDER_STATUS_FILLED:
          case ORDER_STATUS_FILLED_PARTIALLY:
            status = cache.status;
            break;
        }
        break;
    }

    if (tradeInfo && !tradeInfo->price) {
      tradeInfo->price = cache.price;
    }

    Qty actualRemainingQty;
    if (remainingQty) {
      AssertGe(cache.remainingQty, *remainingQty);
      if (tradeInfo && !tradeInfo->qty) {
        tradeInfo->qty = cache.remainingQty - *remainingQty;
      }
      actualRemainingQty = *remainingQty;
    } else {
      actualRemainingQty = cache.remainingQty;
    }

    if (callback) {
      callback(*cache.transactionContext);
    }

    const auto &callSignal =
        [&](const TradingSystem::OrderStatusUpdateSlot &signal) {
          signal(orderId, status, actualRemainingQty, commission,
                 tradeInfo ? &*tradeInfo : nullptr);
        };
    static_assert(numberOfOrderStatuses == 8, "List changed.");
    switch (status) {
      case ORDER_STATUS_FILLED:
        Assert(!remainingQty || *remainingQty == 0);
      case ORDER_STATUS_CANCELLED:
      case ORDER_STATUS_REJECTED:
      case ORDER_STATUS_ERROR: {
        const auto signal = std::move(cache.statusUpdateSignal);
        m_activeOrders.erase(it);
        lock.unlock();
        callSignal(signal);
        break;
      }
      default: {
        cache.status = status;
        if (remainingQty) {
          cache.remainingQty = *remainingQty;
        }
        cache.updateTime = time;
        // Maybe better to make is shared_ptr to avoid copying.
        const auto signal = cache.statusUpdateSignal;
        lock.unlock();
        callSignal(signal);
        break;
      }
    }

    m_context.InvokeDropCopy([&](DropCopy &dropCopy) {
      dropCopy.CopyOrderStatus(orderId, m_self, time, status,
                               actualRemainingQty);
      if (tradeInfo) {
        dropCopy.CopyTrade(time, tradeInfo->id, orderId, m_self,
                           tradeInfo->price, tradeInfo->qty);
      }
    });
  }
};

TradingSystem::TradingSystem(const TradingMode &mode,
                             size_t index,
                             Context &context,
                             const std::string &instanceName)
    : m_pimpl(boost::make_unique<Implementation>(
          *this, mode, index, context, instanceName)) {}

TradingSystem::~TradingSystem() = default;

const TradingMode &TradingSystem::GetMode() const { return m_pimpl->m_mode; }

size_t TradingSystem::GetIndex() const { return m_pimpl->m_index; }

Context &TradingSystem::GetContext() { return m_pimpl->m_context; }

const Context &TradingSystem::GetContext() const {
  return const_cast<TradingSystem *>(this)->GetContext();
}

TradingSystem::Log &TradingSystem::GetLog() const noexcept {
  return m_pimpl->m_log;
}

TradingSystem::TradingLog &TradingSystem::GetTradingLog() const noexcept {
  return m_pimpl->m_tradingLog;
}

const std::string &TradingSystem::GetInstanceName() const {
  return m_pimpl->m_instanceName;
}

const std::string &TradingSystem::GetStringId() const noexcept {
  return m_pimpl->m_stringId;
}

const TradingSystem::Account &TradingSystem::GetAccount() const {
  throw MethodIsNotImplementedException("Account Cash Balance not implemented");
}

std::vector<OrderId> TradingSystem::GetActiveOrderList() const {
  std::vector<OrderId> result;
  const ActiveOrderLock lock(m_pimpl->m_activeOrdersMutex);
  result.reserve(m_pimpl->m_activeOrders.size());
  for (const auto &order : m_pimpl->m_activeOrders) {
    result.emplace_back(order.first);
  }
  return result;
}

void TradingSystem::Connect(const IniSectionRef &conf) {
  if (IsConnected()) {
    return;
  }
  CreateConnection(conf);
}

boost::shared_ptr<const OrderTransactionContext> TradingSystem::SendOrder(
    Security &security,
    const Currency &currency,
    const Qty &qty,
    const boost::optional<Price> &price,
    const OrderParams &params,
    const OrderStatusUpdateSlot &callback,
    RiskControlScope &riskControlScope,
    const OrderSide &side,
    const TimeInForce &tif,
    const Milestones &delaysMeasurement) {
  const Price &actualPrice = price ? *price
                                   : side == ORDER_SIDE_BUY
                                         ? security.GetAskPrice()
                                         : security.GetBidPrice();
  m_pimpl->LogNewOrder(security, currency, qty, price, actualPrice, side, tif);

  const auto riskControlOperationId =
      side == ORDER_SIDE_BUY
          ? m_pimpl->CheckNewBuyOrder(riskControlScope, security, currency, qty,
                                      actualPrice, delaysMeasurement)
          : m_pimpl->CheckNewSellOrder(riskControlScope, security, currency,
                                       qty, actualPrice, delaysMeasurement);

  try {
    return SendOrderTransaction(
        security, currency, qty, price, actualPrice, params, side, tif,
        [this, &riskControlScope, riskControlOperationId, &security, currency,
         actualPrice, delaysMeasurement, callback, side](
            const OrderId &orderId, const OrderStatus &orderStatus,
            const Qty &remainingQty, const boost::optional<Volume> &commission,
            const TradeInfo *trade) {
          m_pimpl->LogOrderUpdate(orderId, orderStatus, remainingQty,
                                  commission, trade);
          side == ORDER_SIDE_BUY
              ? m_pimpl->ConfirmBuyOrder(riskControlOperationId,
                                         riskControlScope, orderStatus,
                                         security, currency, actualPrice,
                                         remainingQty, trade, delaysMeasurement)
              : m_pimpl->ConfirmSellOrder(
                    riskControlOperationId, riskControlScope, orderStatus,
                    security, currency, actualPrice, remainingQty, trade,
                    delaysMeasurement);
          callback(orderId, orderStatus, remainingQty, commission, trade);
        });
  } catch (const std::exception &ex) {
    GetTradingLog().Write(
        "{'order': {'sendError': {'reason': '%1%'}}}",
        [&ex](TradingRecord &record) { record % std::string(ex.what()); });
    GetLog().Warn("Error while sending order transaction: \"%1%\".", ex.what());
    m_pimpl->ConfirmSellOrder(riskControlOperationId, riskControlScope,
                              ORDER_STATUS_ERROR, security, currency,
                              actualPrice, qty, nullptr, delaysMeasurement);
    throw;
  } catch (...) {
    GetTradingLog().Write(
        "{'order': {'sendError': {'reason': 'Unknown exception'}}}");
    GetLog().Error("Unknown error while sending order transaction.");
    AssertFailNoException();
    m_pimpl->ConfirmSellOrder(riskControlOperationId, riskControlScope,
                              ORDER_STATUS_ERROR, security, currency,
                              actualPrice, qty, nullptr, delaysMeasurement);
    throw;
  }
}

boost::shared_ptr<OrderTransactionContext> TradingSystem::SendOrderTransaction(
    Security &security,
    const Currency &currency,
    const Qty &qty,
    const boost::optional<Price> &price,
    const Price &actualPrice,
    const OrderParams &params,
    const OrderSide &side,
    const TimeInForce &tif,
    const OrderStatusUpdateSlot &&callback) {
  const auto &time = GetContext().GetCurrentTime();
  boost::shared_ptr<OrderTransactionContext> result;
  {
    const ActiveOrderLock lock(m_pimpl->m_activeOrdersMutex);
    result =
        SendOrderTransaction(security, currency, qty, price, params, side, tif);
    m_pimpl->LogOrderSent(result->GetOrderId());
    m_pimpl->RegisterCallback(result, std::move(callback), qty, actualPrice);
    GetContext().InvokeDropCopy([&](DropCopy &dropCopy) {
      dropCopy.CopySubmittedOrder(result->GetOrderId(), time, security,
                                  currency, *this, side, qty, price, tif);
    });
  }
  OnTransactionSent(result->GetOrderId());
  return result;
}

void TradingSystem::OnTransactionSent(const OrderId &) {}

std::unique_ptr<OrderTransactionContext>
TradingSystem::SendOrderTransactionAndEmulateIoc(
    Security &security,
    const Currency &currency,
    const Qty &qty,
    const boost::optional<Price> &price,
    const OrderParams &params,
    const OrderSide &side) {
  auto result = SendOrderTransaction(security, currency, qty, price, params,
                                     side, TIME_IN_FORCE_GTC);
  Assert(!m_pimpl->m_lastOrderTimerScope);
  const auto &orderId = result->GetOrderId();
  m_pimpl->m_lastOrderTimerScope = boost::make_unique<Timer::Scope>();
  GetContext().GetTimer().Schedule(
      params.goodInTime ? *params.goodInTime : pt::milliseconds(300),
      [this, orderId] { CancelOrder(orderId); },
      *m_pimpl->m_lastOrderTimerScope);
  return result;
}

bool TradingSystem::CancelOrder(const OrderId &orderId) {
  GetTradingLog().Write(
      "{'order': {'cancel': {'id': %1%}}}",
      [&orderId](TradingRecord &record) { record % orderId; });
  try {
    SendCancelOrderTransaction(orderId);
  } catch (const OrderIsUnknown &ex) {
    GetTradingLog().Write(
        "{'order': {'cancelSendError': {'id': %1%, 'reason': '%2%'}}}",
        [&orderId, &ex](TradingRecord &record) {
          record % orderId               // 1
              % std::string(ex.what());  // 2
        });
    OnTransactionSent(orderId);
    return false;
  } catch (const std::exception &ex) {
    GetTradingLog().Write(
        "{'order': {'cancelSendError': {'id': %1%, 'reason': '%2%'}}}",
        [&orderId, &ex](TradingRecord &record) {
          record % orderId               // 1
              % std::string(ex.what());  // 2
        });
    GetLog().Warn(
        "Error while sending order cancel transaction for order %1%: "
        "\"%2%\".",
        orderId,                  // 1
        std::string(ex.what()));  // 2
    OnTransactionSent(orderId);
    throw;
  } catch (...) {
    GetTradingLog().Write(
        "{'order': {'cancelSendError': {'id': %1%, 'reason': 'Unknown "
        "exception'}}}",
        [&orderId](TradingRecord &record) { record % orderId; });
    GetLog().Error(
        "Unknown error while sending order cancel transaction for order %1%.",
        orderId);
    AssertFailNoException();
    throw;
  }
  GetTradingLog().Write("{'order': {'cancelSent': {'id': '%1%'}}}",
                        [&](TradingRecord &record) { record % orderId; });
  OnTransactionSent(orderId);
  return true;
}

void TradingSystem::OnSettingsUpdate(const IniSectionRef &) {}

void TradingSystem::OnOrderStatusUpdate(const OrderId &orderId,
                                        const OrderStatus &status,
                                        const Qty &remainingQty,
                                        const Volume &commission,
                                        TradeInfo &&trade) {
  m_pimpl->OnOrderStatusUpdate(
      orderId, status, remainingQty, std::move(commission), std::move(trade),
      boost::function<void(OrderTransactionContext &)>());
}
void TradingSystem::OnOrderStatusUpdate(const OrderId &orderId,
                                        const OrderStatus &status,
                                        const Qty &remainingQty) {
  m_pimpl->OnOrderStatusUpdate(
      orderId, status, remainingQty, boost::none, boost::none,
      boost::function<void(OrderTransactionContext &)>());
}
void TradingSystem::OnOrderStatusUpdate(const OrderId &orderId,
                                        const OrderStatus &status,
                                        const Qty &remainingQty,
                                        const Volume &commission) {
  m_pimpl->OnOrderStatusUpdate(
      orderId, status, remainingQty, commission, boost::none,
      boost::function<void(OrderTransactionContext &)>());
}
void TradingSystem::OnOrderStatusUpdate(const OrderId &orderId,
                                        const OrderStatus &status,
                                        const Qty &remainingQty,
                                        TradeInfo &&trade) {
  m_pimpl->OnOrderStatusUpdate(
      orderId, status, remainingQty, boost::none, std::move(trade),
      boost::function<void(OrderTransactionContext &)>());
}
void TradingSystem::OnOrderStatusUpdate(
    const OrderId &orderId,
    const OrderStatus &status,
    const Qty &remainingQty,
    const boost::function<void(OrderTransactionContext &)> &callback) {
  m_pimpl->OnOrderStatusUpdate(orderId, status, remainingQty, boost::none,
                               boost::none, callback);
}
void TradingSystem::OnOrderStatusUpdate(
    const OrderId &orderId,
    const OrderStatus &status,
    const Qty &remainingQty,
    TradeInfo &&trade,
    const boost::function<void(OrderTransactionContext &)> &callback) {
  m_pimpl->OnOrderStatusUpdate(orderId, status, remainingQty, boost::none,
                               std::move(trade), callback);
}

void TradingSystem::OnOrderCancel(const OrderId &orderId) {
  m_pimpl->OnOrderStatusUpdate(
      orderId, ORDER_STATUS_CANCELLED, boost::none, boost::none, boost::none,
      boost::function<void(OrderTransactionContext &)>());
}

void TradingSystem::OnOrderError(const OrderId &orderId,
                                 const std::string &&error) {
  GetTradingLog().Write("{'order': {'error': {'id': %1%, 'reason': '%2%'}}}",
                        [&](TradingRecord &record) {
                          record % orderId  // 1
                              % error;      // 2
                        });
  GetLog().Warn(
      "Operation request for the order %1% is rejected with the reason: "
      "\"%2%\".",
      orderId,  // 1
      error);   // 2
  m_pimpl->OnOrderStatusUpdate(
      orderId, ORDER_STATUS_ERROR, boost::none, boost::none, boost::none,
      boost::function<void(OrderTransactionContext &)>());
}

void TradingSystem::OnOrderReject(const OrderId &orderId,
                                  const std::string &&reason) {
  GetTradingLog().Write("{'order': {'reject': {'id': %1%, 'reason': '%2%'}}}",
                        [&](TradingRecord &record) {
                          record % orderId  // 1
                              % reason;     // 2
                        });
  m_pimpl->OnOrderStatusUpdate(
      orderId, ORDER_STATUS_REJECTED, boost::none, boost::none, boost::none,
      boost::function<void(OrderTransactionContext &)>());
}

void TradingSystem::OnOrder(const OrderId &orderId,
                            const std::string &symbol,
                            const OrderStatus &status,
                            const Qty &qty,
                            const Qty &remainingQty,
                            const boost::optional<Price> &price,
                            const OrderSide &side,
                            const TimeInForce &tif,
                            const pt::ptime &openTime,
                            const pt::ptime &updateTime) {
  {
    ActiveOrderLock lock(m_pimpl->m_activeOrdersMutex);
    const auto &it = m_pimpl->m_activeOrders.find(orderId);
    if (it != m_pimpl->m_activeOrders.cend()) {
      if (!it->second.IsChanged(status, remainingQty)) {
        return;
      }
      boost::optional<TradeInfo> trade;
      switch (status) {
        case ORDER_STATUS_FILLED:
        case ORDER_STATUS_FILLED_PARTIALLY:
          trade = TradeInfo{price ? *price : 0};
      }
      m_pimpl->OnOrderStatusUpdate(
          orderId, status, remainingQty, boost::none, std::move(trade),
          updateTime, it, std::move(lock),
          boost::function<void(OrderTransactionContext &)>());
      return;
    }
  }
  GetContext().InvokeDropCopy([&](DropCopy &dropCopy) {
    dropCopy.CopyOrder(orderId, *this, symbol, status, qty, remainingQty, price,
                       side, tif, openTime, updateTime);
  });
}

////////////////////////////////////////////////////////////////////////////////

LegacyTradingSystem::LegacyTradingSystem(const TradingMode &tradingMode,
                                         size_t index,
                                         Context &context,
                                         const std::string &instanceName)
    : TradingSystem(tradingMode, index, context, instanceName) {}

boost::shared_ptr<OrderTransactionContext>
LegacyTradingSystem::SendOrderTransaction(
    Security &security,
    const Currency &currency,
    const Qty &qty,
    const boost::optional<Price> &price,
    const Price &,
    const OrderParams &params,
    const OrderSide &side,
    const TimeInForce &tif,
    const OrderStatusUpdateSlot &&callback) {
  if (side == ORDER_SIDE_BUY) {
    if (price) {
      switch (tif) {
        case TIME_IN_FORCE_GTC:
          return boost::make_shared<OrderTransactionContext>(SendBuy(
              security, currency, qty, *price, params, std::move(callback)));
        case TIME_IN_FORCE_IOC:
          return boost::make_unique<OrderTransactionContext>(
              SendBuyImmediatelyOrCancel(security, currency, qty, *price,
                                         params, std::move(callback)));
      }
    } else {
      switch (tif) {
        case TIME_IN_FORCE_GTC:
          return boost::make_shared<OrderTransactionContext>(
              SendBuyAtMarketPrice(security, currency, qty, params,
                                   std::move(callback)));
      }
    }
  } else {
    if (price) {
      switch (tif) {
        case TIME_IN_FORCE_GTC:
          return boost::make_shared<OrderTransactionContext>(SendSell(
              security, currency, qty, *price, params, std::move(callback)));
        case TIME_IN_FORCE_IOC:
          return boost::make_shared<OrderTransactionContext>(
              SendSellImmediatelyOrCancel(security, currency, qty, *price,
                                          params, std::move(callback)));
      }
    } else {
      switch (tif) {
        case TIME_IN_FORCE_GTC:
          return boost::make_shared<OrderTransactionContext>(
              SendSellAtMarketPrice(security, currency, qty, params,
                                    std::move(callback)));
      }
    }
  }
  AssertFail("Legacy trading system can not support all order types.");
  throw Error("Unsupported order type");
}

std::unique_ptr<OrderTransactionContext>
LegacyTradingSystem::SendOrderTransaction(Security &,
                                          const Currency &,
                                          const Qty &,
                                          const boost::optional<Price> &,
                                          const OrderParams &,
                                          const OrderSide &,
                                          const TimeInForce &) {
  AssertFail(
      "Internal error of legacy trading system implementation: this method "
      "never should be called, as legacy trading system uses own methods "
      "with callback argument.");
  throw Error("Internal error of legacy trading system implementation");
}

//////////////////////////////////////////////////////////////////////////

std::ostream &trdk::operator<<(std::ostream &oss,
                               const TradingSystem &tradingSystem) {
  oss << tradingSystem.GetStringId();
  return oss;
}

////////////////////////////////////////////////////////////////////////////////
