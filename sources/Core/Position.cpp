/**************************************************************************
 *   Created: 2012/07/08 04:07:42
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot
 **************************************************************************/

#include "Prec.hpp"
#include "Position.hpp"
#include "Strategy.hpp"
#include "Settings.hpp"

using namespace Trader;
using namespace Trader::Lib;

//////////////////////////////////////////////////////////////////////////

StrategyPositionState::StrategyPositionState() {
	//...//
}

StrategyPositionState::~StrategyPositionState() {
	//...//
}

//////////////////////////////////////////////////////////////////////////

Position::DynamicData::DynamicData()
		: lastOrderId(0),
		qty(0),
		comission(0),
		hasOrder(false) {
	//...//
}

Position::DynamicData::Price::Price()
		: total(0),
		count(0) {
	//...//
}

//////////////////////////////////////////////////////////////////////////

Position::Position(
			boost::shared_ptr<Security> security,
			const std::string &tag)
		: m_security(security),
		m_isPlanedQtyDynamic(true),
		m_planedQty(0),
		m_openStartPrice(0),
		m_closeStartPrice(0),
		m_closeType(CLOSE_TYPE_NONE),
		m_isReported(false),
		m_isError(false),
		m_isCanceled(false),
		m_tag(tag) {
	//...//
}


Position::Position(
			boost::shared_ptr<Security> security,
			Qty qty,
			ScaledPrice startPrice,
			const std::string &tag,
			boost::shared_ptr<StrategyPositionState> state
				/*= boost::shared_ptr<StrategyPositionState>()*/)
		: m_security(security),
		m_isPlanedQtyDynamic(false),
		m_planedQty(qty),
		m_openStartPrice(startPrice),
		m_closeStartPrice(0),
		m_closeType(CLOSE_TYPE_NONE),
		m_isReported(false),
		m_isError(false),
		m_isCanceled(false),
		m_tag(tag),
		m_strategyState(state) {
	AssertGt(m_planedQty, 0);
}

Position::~Position() {
	//...//
}

bool Position::IsReported() const {
	return m_isReported;
}

void Position::MarkAsReported() {
	Assert(!m_isReported);
	m_isReported = true;
}

const std::string & Position::GetCloseTypeStr() const {
	static const std::string none = "-";
	static const std::string takeProfit = "t/p";
	static const std::string stopLoss = "s/l";
	static const std::string timeout = "timeout";
	static const std::string schedule = "schedule";
	static const std::string engineStop = "engine stop";
	switch (GetCloseType()) {
		default:
			AssertFail("Unknown position close type.");
		case CLOSE_TYPE_NONE:
			return none;
		case CLOSE_TYPE_TAKE_PROFIT:
			return takeProfit;
		case CLOSE_TYPE_STOP_LOSS:
			return stopLoss;
		case CLOSE_TYPE_TIMEOUT:
			return timeout;
		case CLOSE_TYPE_SCHEDULE:
			return schedule;
		case CLOSE_TYPE_ENGINE_STOP:
			return engineStop;
	}
}

void Position::UpdateOpening(
			OrderId orderId,
			TradeSystem::OrderStatus orderStatus,
			Qty filled,
			Qty remaining,
			double avgPrice,
			double /*lastPrice*/) {

	UseUnused(orderId);

	WriteLock lock(m_mutex);

	Assert(!IsOpened());
	Assert(!IsClosed());
	AssertNe(orderId, 0);
	AssertNe(m_opened.lastOrderId, 0);
	AssertEq(m_opened.lastOrderId, orderId);
	AssertEq(m_closed.qty, 0);
	Assert(m_opened.hasOrder);
	Assert(!m_closed.hasOrder);

	bool isCompleted = false;
	switch (orderStatus) {
		default:
			AssertFail("Unknown order status");
			return;
		case TradeSystem::ORDER_STATUS_PENDIGN:
		case TradeSystem::ORDER_STATUS_SUBMITTED:
			return;
		case TradeSystem::ORDER_STATUS_FILLED:
			Interlocking::Exchange(
				m_opened.price.total,
				m_opened.price.total + m_security->ScalePrice(avgPrice));
			Interlocking::Increment(m_opened.price.count);
			Interlocking::Exchange(m_opened.qty, m_opened.qty + filled);
			AssertGt(m_opened.qty, 0);
			ReportOpeningUpdate("filled", orderStatus);
			isCompleted = remaining == 0;
			break;
		case TradeSystem::ORDER_STATUS_INACTIVE:
		case TradeSystem::ORDER_STATUS_ERROR:
			ReportOpeningUpdate("error", orderStatus);
			Interlocking::Exchange(m_isError, true);
			isCompleted = true;
			break;
		case TradeSystem::ORDER_STATUS_CANCELLED:
			if (m_opened.qty > 0) {
				ReportOpeningUpdate("canceled", orderStatus);
			}
			isCompleted = true;
			break;
	}
	
	if (!isCompleted) {
		return;
	}

	if (GetOpenedQty() > 0) {
		try {
			if (m_opened.time.is_not_a_date_time()) {
				m_opened.time = !m_security->GetSettings().IsReplayMode()
					?	boost::get_system_time()
					:	m_security->GetLastMarketDataTime();
			}
		} catch (...) {
			AssertFailNoException();
		}
	}

	Interlocking::Exchange(m_opened.hasOrder, false);

	if (CancelIfSet()) {
		return;
	}

	lock.unlock();
	m_stateUpdateSignal();

}

void Position::UpdateClosing(
			OrderId orderId,
			TradeSystem::OrderStatus orderStatus,
			Qty filled,
			Qty remaining,
			double avgPrice,
			double /*lastPrice*/) {

	UseUnused(orderId);

	WriteLock lock(m_mutex);

	Assert(IsOpened());
	Assert(!IsClosed());
	AssertLe(m_closed.qty, m_opened.qty);
	AssertNe(orderId, 0);
	AssertNe(m_closed.lastOrderId, 0);
	AssertEq(m_closed.lastOrderId, orderId);
	AssertNe(m_opened.lastOrderId, orderId);
	Assert(!m_opened.hasOrder);
	Assert(m_closed.hasOrder);

	bool isCompleted = false;
	switch (orderStatus) {
		default:
			AssertFail("Unknown order status");
			return;
		case TradeSystem::ORDER_STATUS_PENDIGN:
		case TradeSystem::ORDER_STATUS_SUBMITTED:
			return;
		case TradeSystem::ORDER_STATUS_FILLED:
			AssertLe(filled + remaining, m_opened.qty);
			AssertLe(long(m_closed.qty) + filled, m_opened.qty);
			Interlocking::Exchange(
				m_closed.price.total,
				m_closed.price.total + m_security->ScalePrice(avgPrice));
			Interlocking::Increment(m_closed.price.count);
			Interlocking::Exchange(m_closed.qty, m_closed.qty + filled);
			AssertGt(m_closed.qty, 0);
			isCompleted = remaining == 0;
			ReportClosingUpdate("filled", orderStatus);
			break;
		case TradeSystem::ORDER_STATUS_INACTIVE:
		case TradeSystem::ORDER_STATUS_ERROR:
			Log::Error(
				"Position CLOSE error: symbol: \"%1%\", strategy %2%"
					", trade system state: %3%, orders ID: %4%->%5%, qty: %6%->%7%.",
				GetSecurity(),
				m_tag,
				orderStatus,
				m_opened.lastOrderId,
				m_closed.lastOrderId,	
				m_opened.qty,
				m_closed.qty);
			ReportClosingUpdate("error", orderStatus);
			Interlocking::Exchange(m_isError, true);
			isCompleted = true;
			break;
		case TradeSystem::ORDER_STATUS_CANCELLED:
			if (m_closed.qty > 0) {
				ReportClosingUpdate("canceled", orderStatus);
			}
			isCompleted = true;
			break;
	}
	
	if (!isCompleted) {
		return;
	}

	if (GetActiveQty() == 0) {
		try {
			if (m_closed.time.is_not_a_date_time()) {
				m_closed.time = !m_security->GetSettings().IsReplayMode()
					?	boost::get_system_time()
					:	m_security->GetLastMarketDataTime();
			}
		} catch (...) {
			Interlocking::Exchange(m_isError, true);
			AssertFailNoException();
		}
	}

	Interlocking::Exchange(m_closed.hasOrder, false);

	if (CancelIfSet()) {
		return;
	}

	lock.unlock();
	m_stateUpdateSignal();

}

bool Position::CancelIfSet() throw() {
	if (IsClosed() || Interlocking::CompareExchange(m_isCanceled, 2, 1) != 1) {
		return false;
	}
	Log::Trading(
		"position",
		"%1% %2% close-cancel-post %3% qty=%4%->%5% price=market order-id=%6%->%7%"
			" has-orders=%8%/%9% is-error=%10%",
		GetSecurity().GetSymbol(),
		GetTypeStr(),
		m_tag,
		GetOpenedQty(),
		GetClosedQty(),
		GetOpenOrderId(),
		GetCloseOrderId(),
		HasActiveOpenOrders(),
		HasActiveCloseOrders(),
		m_isError);
	try {
		m_cancelMethod();
		return true;
	} catch (...) {
		Interlocking::Exchange(m_isError, true);
		AssertFailNoException();
		return false;
	}
}

Position::Time Position::GetOpenTime() const {
	Position::Time result;
	{
		const ReadLock lock(m_mutex);
		result = m_opened.time;
	}
	return result;
}

Position::Time Position::GetCloseTime() const {
	Position::Time result;
	{
		const ReadLock lock(m_mutex);
		result = m_closed.time;
	}
	return result;
}

ScaledPrice Position::GetCommission() const {
	return m_opened.comission + m_closed.comission;
}

Position::StateUpdateConnection Position::Subscribe(
			const StateUpdateSlot &slot)
		const {
	return StateUpdateConnection(m_stateUpdateSignal.connect(slot));
}

Qty Position::GetPlanedQty() const {
	return m_planedQty;
}

ScaledPrice Position::GetOpenStartPrice() const {
	return m_openStartPrice;
}

ScaledPrice Position::GetOpenPrice() const {
	return m_opened.price.count > 0
		?	m_opened.price.total / m_opened.price.count
		:	0;
}

ScaledPrice Position::GetCloseStartPrice() const {
	return m_closeStartPrice;
}

void Position::SetCloseStartPrice(ScaledPrice price) {
	m_closeStartPrice = price;
}

ScaledPrice Position::GetClosePrice() const {
	return m_closed.price.count > 0
		?	m_closed.price.total / m_closed.price.count
		:	0;
}

void Position::ReportOpeningUpdate(
			const char *eventDesc,
			TradeSystem::OrderStatus orderStatus)
		const
		throw() {
	try {
		Log::Trading(
			"position",
			"%1% %2% open-%3% %4% qty=%5%->%6% price=%7%->%8% order-id=%9%"
				" order-status=%10% is-error=%11%",
			GetSecurity().GetSymbol(),
			GetTypeStr(),
			eventDesc,
			m_tag,
			GetPlanedQty(),
			GetOpenedQty(),
			GetSecurity().DescalePrice(GetOpenStartPrice()),
			GetSecurity().DescalePrice(GetOpenPrice()),
			GetOpenOrderId(),
			GetSecurity().GetTradeSystem().GetStringStatus(orderStatus),
			m_isError);
	} catch (...) {
		AssertFailNoException();
	}
}

void Position::ReportClosingUpdate(
			const char *eventDesc,
			TradeSystem::OrderStatus orderStatus)
		const
		throw() {
	try {
		Log::Trading(
			"position",
			"%1% %2% close-%3% %4% qty=%5%->%6% price=%7% order-id=%8%->%9%"
				" order-status=%10% is-error=%11%",
			GetSecurity().GetSymbol(),
			GetTypeStr(),
			eventDesc,
			m_tag,
			GetOpenedQty(),
			GetClosedQty(),
			GetSecurity().DescalePrice(GetClosePrice()),
			GetOpenOrderId(),
			GetCloseOrderId(),
			GetSecurity().GetTradeSystem().GetStringStatus(orderStatus),
			m_isError);
	} catch (...) {
		AssertFailNoException();
	}
}

void Position::IncreasePlanedQty(Qty qty) throw() {
	for ( ; ; ) {
		const auto prevVal = m_planedQty;
		const auto newVal = prevVal + qty;
		if (Interlocking::CompareExchange(m_planedQty, newVal, prevVal) == prevVal) {
			break;
		}
	}
}

void Position::DecreasePlanedQty(Qty qty) throw() {
	AssertLe(qty, m_planedQty);
	AssertGt(qty, 0);
	for ( ; ; ) {
		const auto prevVal = m_planedQty;
		const auto newVal = prevVal - qty;
		if (Interlocking::CompareExchange(m_planedQty, newVal, prevVal) == prevVal) {
			break;
		}
	}
}

OrderId Position::OpenAtMarketPrice() {
	const WriteLock lock(m_mutex);
	Assert(!IsError());
	Assert(!HasActiveOrders());
	const auto orderId = DoOpenAtMarketPrice(GetNotOpenedQty());
	Interlocking::Exchange(m_opened.hasOrder, true);
	Interlocking::Exchange(m_opened.lastOrderId, orderId);
	return orderId;
}

OrderId Position::Open(ScaledPrice price) {
	const WriteLock lock(m_mutex);
	Assert(!IsError());
	Assert(!HasActiveOrders());
	const auto orderId = DoOpen(price, GetNotOpenedQty());
	Interlocking::Exchange(m_opened.hasOrder, true);
	Interlocking::Exchange(m_opened.lastOrderId, orderId);
	return orderId;
}

OrderId Position::OpenAtMarketPriceWithStopPrice(ScaledPrice stopPrice) {
	const WriteLock lock(m_mutex);
	Assert(!IsError());
	Assert(!HasActiveOrders());
	const auto orderId = DoOpenAtMarketPriceWithStopPrice(stopPrice);
	Interlocking::Exchange(m_opened.hasOrder, true);
	Interlocking::Exchange(m_opened.lastOrderId, orderId);
	return orderId;
}

OrderId Position::OpenOrCancel(ScaledPrice price) {
	const WriteLock lock(m_mutex);
	Assert(!IsError());
	Assert(!HasActiveOrders());
	const auto orderId = DoOpenOrCancel(price);
	Interlocking::Exchange(m_opened.hasOrder, true);
	Interlocking::Exchange(m_opened.lastOrderId, orderId);
	return orderId;
}

OrderId Position::CloseAtMarketPrice(CloseType closeType) {
	const WriteLock lock(m_mutex);
	Assert(!IsError());
	Assert(!HasActiveOrders());
	const auto orderId = DoCloseAtMarketPrice(GetActiveQty());
	Interlocking::Exchange(m_closeType, closeType);
	Interlocking::Exchange(m_closed.hasOrder, true);
	Interlocking::Exchange(m_closed.lastOrderId, orderId);
	return orderId;
}

OrderId Position::CloseAtMarketPrice(CloseType closeType, Qty qty) {
	const WriteLock lock(m_mutex);
	AssertLe(qty, m_planedQty);
	Assert(!IsError());
	Assert(!HasActiveOrders());
	if (m_isPlanedQtyDynamic) {
		DecreasePlanedQty(qty);
	}
	const auto orderId = DoCloseAtMarketPrice(qty);
	Interlocking::Exchange(m_closeType, closeType);
	Interlocking::Exchange(m_closed.hasOrder, true);
	Interlocking::Exchange(m_closed.lastOrderId, orderId);
	return orderId;
}

OrderId Position::Close(CloseType closeType, ScaledPrice price) {
	const WriteLock lock(m_mutex);
	Assert(!IsError());
	Assert(!HasActiveOrders());
	const auto orderId = DoClose(price, GetActiveQty());
	Interlocking::Exchange(m_closeType, closeType);
	Interlocking::Exchange(m_closed.hasOrder, true);
	Interlocking::Exchange(m_closed.lastOrderId, orderId);
	return orderId;
}

OrderId Position::Close(CloseType closeType, ScaledPrice price, Qty qty) {
	const WriteLock lock(m_mutex);
	AssertLe(qty, m_planedQty);
	Assert(!IsError());
	Assert(!HasActiveOrders());
	if (m_isPlanedQtyDynamic) {
		DecreasePlanedQty(qty);
	}
	const auto orderId = DoClose(price, qty);
	Interlocking::Exchange(m_closeType, closeType);
	Interlocking::Exchange(m_closed.hasOrder, true);
	Interlocking::Exchange(m_closed.lastOrderId, orderId);
	return orderId;
}

OrderId Position::CloseAtMarketPriceWithStopPrice(
			CloseType closeType,
			ScaledPrice stopPrice) {
	const WriteLock lock(m_mutex);
	Assert(!IsError());
	Assert(!HasActiveOrders());
	const auto orderId = DoCloseAtMarketPriceWithStopPrice(stopPrice);
	Interlocking::Exchange(m_closeType, closeType);
	Interlocking::Exchange(m_closed.hasOrder, true);
	Interlocking::Exchange(m_closed.lastOrderId, orderId);
	return orderId;
}

OrderId Position::CloseOrCancel(CloseType closeType, ScaledPrice price) {
	const WriteLock lock(m_mutex);
	Assert(!IsError());
	Assert(!HasActiveOrders());
	const auto orderId = DoCloseOrCancel(price);
	Interlocking::Exchange(m_closeType, closeType);
	Interlocking::Exchange(m_closed.hasOrder, true);
	Interlocking::Exchange(m_closed.lastOrderId, orderId);
	return orderId;
}

bool Position::CancelAtMarketPrice(CloseType closeType) {
	const WriteLock lock(m_mutex);
	Assert(IsOpened());
	if (IsClosed() || IsCanceled()) {
		return false;
	}
	Log::Trading(
		"position",
		"%1% %2% close-cancel-pre %3% qty=%4%->%5% price=market order-id=%6%->%7%"
			" has-orders=%8%/%9% is-error=%10%",
		GetSecurity().GetSymbol(),
		GetTypeStr(),
		m_tag,
		GetOpenedQty(),
		GetClosedQty(),
		GetOpenOrderId(),
		GetCloseOrderId(),
		HasActiveOpenOrders(),
		HasActiveCloseOrders(),
		m_isError);
	boost::function<void()> cancelMethod = [this] () {
		Assert(IsOpened());
		Assert(!IsClosed());
		const auto orderId = DoCloseAtMarketPrice(GetActiveQty());
		Interlocking::Exchange(m_closed.hasOrder, true);
		Interlocking::Exchange(m_closed.lastOrderId, orderId);
	};
	if (DoCancelAllOrders()) {
		Assert(HasActiveOrders());
		cancelMethod.swap(m_cancelMethod);
	} else {
		Assert(!HasActiveOrders());
		cancelMethod();
	}
	Interlocking::Exchange(m_closeType, closeType);
	Interlocking::Exchange(m_isCanceled, true);
	return true;
}

bool Position::CancelAllOrders() {
	const WriteLock lock(m_mutex);
	return DoCancelAllOrders();
}

bool Position::DoCancelAllOrders() {
	bool isCanceled = false;
	if (m_opened.hasOrder) {
		GetSecurity().CancelOrder(m_opened.lastOrderId);
		isCanceled = true;
	}
	if (m_closed.hasOrder) {
		Assert(!isCanceled);
		GetSecurity().CancelOrder(m_closed.lastOrderId);
		isCanceled = true;
	}
	return isCanceled;
}

//////////////////////////////////////////////////////////////////////////

LongPosition::LongPosition(
			boost::shared_ptr<Security> security,
			const std::string &tag)
		: Position(security, tag) {
	//...//
}

LongPosition::LongPosition(
			boost::shared_ptr<Security> security,
			Qty qty,
			ScaledPrice startPrice,
			const std::string &tag,
			boost::shared_ptr<StrategyPositionState> state
				/*= boost::shared_ptr<StrategyPositionState>()*/)
		: Position(security, qty, startPrice, tag, state) {
	//...//
}

LongPosition::~LongPosition() {
	//...//
}

LongPosition::Type LongPosition::GetType() const {
	return TYPE_LONG;
}

const std::string & LongPosition::GetTypeStr() const throw() {
	static const std::string result = "long";
	return result;
}

Security::OrderStatusUpdateSlot LongPosition::GetSellOrderStatusUpdateSlot() {
	return boost::bind(&LongPosition::UpdateClosing, shared_from_this(), _1, _2, _3, _4, _5, _6);
}

Security::OrderStatusUpdateSlot LongPosition::GetBuyOrderStatusUpdateSlot() {
	return boost::bind(&LongPosition::UpdateOpening, shared_from_this(), _1, _2, _3, _4, _5, _6);
}

OrderId LongPosition::DoOpenAtMarketPrice(Qty qty) {
	Assert(qty > 0);
	return GetSecurity().BuyAtMarketPrice(qty, *this);
}

OrderId LongPosition::DoOpen(ScaledPrice price, Qty qty) {
	Assert(qty > 0);
	return GetSecurity().Buy(qty, price, *this);
}

OrderId LongPosition::DoOpenAtMarketPriceWithStopPrice(ScaledPrice stopPrice) {
	Assert(GetNotOpenedQty() > 0);
	return GetSecurity().BuyAtMarketPriceWithStopPrice(GetNotOpenedQty(), stopPrice, *this);
}

OrderId LongPosition::DoOpenOrCancel(ScaledPrice price) {
	Assert(GetNotOpenedQty() > 0);
	return GetSecurity().BuyOrCancel(GetNotOpenedQty(), price, *this);
}

OrderId LongPosition::DoCloseAtMarketPrice(Qty qty) {
	Assert(qty > 0);
	return GetSecurity().SellAtMarketPrice(qty, *this);
}

OrderId LongPosition::DoClose(ScaledPrice price, Qty qty) {
	Assert(qty > 0);
	return GetSecurity().Sell(qty, price, *this);
}

OrderId LongPosition::DoCloseAtMarketPriceWithStopPrice(ScaledPrice stopPrice) {
	Assert(GetActiveQty() > 0);
	return GetSecurity().SellAtMarketPriceWithStopPrice(GetActiveQty(), stopPrice, *this);
}

OrderId LongPosition::DoCloseOrCancel(ScaledPrice price) {
	Assert(GetActiveQty() > 0);
	return GetSecurity().SellOrCancel(GetActiveQty(), price, *this);
}

//////////////////////////////////////////////////////////////////////////

ShortPosition::ShortPosition(
			boost::shared_ptr<Security> security,
			const std::string &tag)
		: Position(security, tag) {
	//...//
}

ShortPosition::ShortPosition(
			boost::shared_ptr<Security> security,
			Qty qty,
			ScaledPrice startPrice,
			const std::string &tag,
			boost::shared_ptr<StrategyPositionState> state
				/*= boost::shared_ptr<StrategyPositionState>()*/)
		: Position(security, qty, startPrice, tag, state) {
	//...//
}

ShortPosition::~ShortPosition() {
	//...//
}

LongPosition::Type ShortPosition::GetType() const {
	return TYPE_SHORT;
}

const std::string & ShortPosition::GetTypeStr() const throw() {
	static const std::string result = "short";
	return result;
}

Security::OrderStatusUpdateSlot ShortPosition::GetSellOrderStatusUpdateSlot() {
	return boost::bind(&ShortPosition::UpdateOpening, shared_from_this(), _1, _2, _3, _4, _5, _6);
}

Security::OrderStatusUpdateSlot ShortPosition::GetBuyOrderStatusUpdateSlot() {
	return boost::bind(&ShortPosition::UpdateClosing, shared_from_this(), _1, _2, _3, _4, _5, _6);
}

OrderId ShortPosition::DoOpenAtMarketPrice(Qty qty) {
	Assert(qty > 0);
	return GetSecurity().SellAtMarketPrice(qty, *this);
}

OrderId ShortPosition::DoOpen(ScaledPrice price, Qty qty) {
	Assert(qty > 0);
	return GetSecurity().Sell(qty, price, *this);
}

OrderId ShortPosition::DoOpenAtMarketPriceWithStopPrice(ScaledPrice stopPrice) {
	Assert(GetNotOpenedQty() > 0);
	return GetSecurity().SellAtMarketPriceWithStopPrice(GetNotOpenedQty(), stopPrice, *this);
}

OrderId ShortPosition::DoOpenOrCancel(ScaledPrice price) {
	Assert(GetNotOpenedQty() > 0);
	return GetSecurity().SellOrCancel(GetNotOpenedQty(), price, *this);
}

OrderId ShortPosition::DoCloseAtMarketPrice(Qty qty) {
	Assert(qty > 0);
	return GetSecurity().BuyAtMarketPrice(qty, *this);
}

OrderId ShortPosition::DoClose(ScaledPrice price, Qty qty) {
	Assert(qty > 0);
	return GetSecurity().Buy(qty, price, *this);
}

OrderId ShortPosition::DoCloseAtMarketPriceWithStopPrice(ScaledPrice stopPrice) {
	Assert(GetActiveQty() > 0);
	return GetSecurity().BuyAtMarketPriceWithStopPrice(GetActiveQty(), stopPrice, *this);
}

OrderId ShortPosition::DoCloseOrCancel(ScaledPrice price) {
	Assert(GetActiveQty() > 0);
	return GetSecurity().BuyOrCancel(GetActiveQty(), price, *this);
}

//////////////////////////////////////////////////////////////////////////
