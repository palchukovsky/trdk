/**************************************************************************
 *   Created: 2012/07/09 18:42:14
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#include "Prec.hpp"
#include "Security.hpp"
#include "Position.hpp"
#include "Settings.hpp"
#include "Context.hpp"

namespace fs = boost::filesystem;
namespace lt = boost::local_time;
namespace pt = boost::posix_time;

using namespace trdk;
using namespace trdk::Lib;

////////////////////////////////////////////////////////////////////////////////

namespace {
	
	typedef intmax_t Level1Value;

	typedef std::array<boost::atomic<Level1Value>, numberOfLevel1TickTypes> Level1;
	
	bool IsSet(const boost::atomic<Level1Value> &val) {
		return val != std::numeric_limits<Level1Value>::max();
	}

	template<Level1TickType tick>
	typename Level1TickValuePolicy<tick>::ValueType
	GetIfSet(
				const Level1 &level1) {
		if (!IsSet(level1[tick])) {
			return 0;
		}
		return static_cast<typename Level1TickValuePolicy<tick>::ValueType>(
			level1[tick]);
	}

	void Unset(boost::atomic<Level1Value> &val) {
		val = std::numeric_limits<Level1Value>::max();
	}

	boost::atomic_size_t impliedVolatilityUpdatePeriod(30);

}

////////////////////////////////////////////////////////////////////////////////

Security::Bar::Bar(
			const pt::ptime &time,
			const pt::time_duration &size,
			Type type)
		: time(time),
		size(size),
		type(type) {
	//...//
}

//////////////////////////////////////////////////////////////////////////

class Security::Implementation : private boost::noncopyable {

public:

	mutable boost::signals2::signal<Level1UpdateSlotSignature>
		m_level1UpdateSignal;
	mutable boost::signals2::signal<Level1TickSlotSignature> m_level1TickSignal;
	mutable boost::signals2::signal<NewTradeSlotSignature> m_tradeSignal;
	mutable boost::signals2::signal<BrokerPositionUpdateSlotSignature>
		m_brokerPositionUpdateSignal;
	mutable boost::signals2::signal<NewBarSlotSignature> m_barSignal;

	Level1 m_level1;
	boost::atomic<Qty> m_brokerPosition;
	boost::atomic_int64_t m_marketDataTime;
	boost::atomic_bool m_isLevel1Started;

	pt::ptime m_requestedDataStartTime;

	class ImpliedVolatility : private boost::noncopyable {

	public:

		ImpliedVolatility(
					boost::mutex &mutex,
					boost::condition_variable &condition,
					boost::atomic_bool &isAnotherSet)
				: m_mutex(mutex),
				m_condition(condition),
				m_actualValue(-2.0),
				m_lastValue(.0),
				m_isAnotherSet(isAnotherSet) {
			//...//
		}

	public:
	
		void Set(double newValue) {
			const boost::mutex::scoped_lock lock(m_mutex);
			AssertLe(-1, newValue);
			if (newValue >= 0 || m_actualValue < 0) {
				m_actualValue = newValue;
				if (m_actualValue >= 0) {
					m_isAnotherSet = true;
				}
			}
			if (newValue >= 0) {
				m_lastValue = newValue;
			}
			m_condition.notify_all();
		}

		void SetActual() {
			const boost::mutex::scoped_lock lock(m_mutex);
			if (m_actualValue >= 0 && m_lastValue > 0) {
				m_actualValue = m_lastValue;
				if (m_actualValue >= 0) {
					m_isAnotherSet = true;
				}
			}
			m_condition.notify_all();
		}

		double Get(bool wait) const {

			boost::mutex::scoped_lock lock(m_mutex);
			
			if (m_actualValue >= 0) {
				return m_actualValue;
			}
			
			for ( ; ; ) {
				if (m_actualValue >= 0) {
					return m_actualValue;
				} else if (!wait) {
					return 0;
				}
				if (m_actualValue < -1) {
					m_condition.wait(lock);
				} else {
					const bool isAnotherWasSet = m_isAnotherSet;
					const long timeToWait = !isAnotherWasSet ? 10 : 2;
					m_condition.timed_wait(lock, pt::seconds(timeToWait));
					if (m_actualValue < 0) {
						if (!m_isAnotherSet || isAnotherWasSet) {
							Log::Error("Implied Volatility condition timeout.");
							m_actualValue = .0;
						} else {
							Log::Info(
								"Implied Volatility condition fast wait...");
						}
					}
				}
			}
		
		}
	
	private:

		mutable boost::atomic<double> m_actualValue;
		mutable double m_lastValue;
		boost::mutex &m_mutex;
		boost::condition_variable &m_condition;
		boost::atomic_bool &m_isAnotherSet;
	
	};

	boost::mutex m_impliedVolatilityMutex;
	boost::condition_variable m_impliedVolatilityCondition;
	boost::atomic_bool m_isAnotherImpliedVolatilitySet;
	ImpliedVolatility m_impliedVolatilityLast;
	ImpliedVolatility m_impliedVolatilityAsk;
	ImpliedVolatility m_impliedVolatilityBid;

	mutable boost::mutex m_ivTimeMutex;
	pt::ptime m_ivLastTime;

	void CheckIv() {
		const auto &now = pt::second_clock::universal_time();
		const boost::mutex::scoped_lock lock(m_ivTimeMutex);
		if (	m_ivLastTime.is_not_a_date_time()
				|| m_ivLastTime + pt::seconds(long(impliedVolatilityUpdatePeriod))
						<= now) {
			m_impliedVolatilityLast.SetActual();
			m_impliedVolatilityAsk.SetActual();
			m_impliedVolatilityBid.SetActual();
			m_ivLastTime = now;
		}
	}

public:

	Implementation()
			: m_brokerPosition(0),
			m_marketDataTime(0),
			m_isLevel1Started(false),
			m_isAnotherImpliedVolatilitySet(false),
			m_impliedVolatilityLast(
				m_impliedVolatilityMutex,
				m_impliedVolatilityCondition,
				m_isAnotherImpliedVolatilitySet),
			m_impliedVolatilityAsk(
				m_impliedVolatilityMutex,
				m_impliedVolatilityCondition,
				m_isAnotherImpliedVolatilitySet),
			m_impliedVolatilityBid(
				m_impliedVolatilityMutex,
				m_impliedVolatilityCondition,
				m_isAnotherImpliedVolatilitySet) {
		foreach (auto &item, m_level1) {
			Unset(item);
		}
	}

	unsigned int GetPriceScale() const throw() {
		return 100000;
	}

	ScaledPrice ScalePrice(double price) const {
		return Scale(price, GetPriceScale());
	}

	double DescalePrice(ScaledPrice price) const {
		return Descale(price, GetPriceScale());
	}

	double DescalePrice(double price) const {
		return Descale(price, GetPriceScale());
	}

	bool AddLevel1Tick(
				const pt::ptime &time,
				const Level1TickValue &tick,
				bool flush,
				bool isPreviouslyChanged) {
		const bool isChanged
			= SetLevel1(time, tick, flush, isPreviouslyChanged);
		m_level1TickSignal(time, tick, flush);
		return isChanged;
	}

	bool SetLevel1(
				const pt::ptime &time,
				const Level1TickValue &tick,
				bool flush,
				bool isPreviouslyChanged) {
		const auto tickValue = tick.Get();
		const bool isChanged = m_level1[tick.type].exchange(tickValue) != tickValue;
		FlushLevel1Update(time, flush, isChanged, isPreviouslyChanged);
		return isChanged;
	}

	bool CompareAndSetLevel1(
				const pt::ptime &time,
				const Level1TickValue &tick,
				intmax_t prevValue,
				bool flush,
				bool isPreviouslyChanged) {
		const auto tickValue = tick.Get();
		AssertNe(tickValue, prevValue);
		if (tickValue == prevValue) {
			return true;
		}
		if (!m_level1[tick.type].compare_exchange_weak(prevValue, tickValue)) {
			return false;
		}
		FlushLevel1Update(time, flush, true, isPreviouslyChanged);
		return true;
	}

	void FlushLevel1Update(
				const pt::ptime &time,
				bool flush,
				bool isChanged,
				bool isPreviouslyChanged) {
		
		if (!flush || !(isChanged || isPreviouslyChanged)) {
			return;
		}
		
		if (!m_isLevel1Started) {
			foreach (const auto &item, m_level1) {
				if (!IsSet(item)) {
					return;
				}
			}
			Assert(!m_isLevel1Started);
			m_isLevel1Started = true;
		}

		Assert(!time.is_not_a_date_time());
		AssertLe(GetLastMarketDataTime(), time);
		m_marketDataTime = ConvertToMicroseconds(time);

		m_level1UpdateSignal();

	}

	pt::ptime GetLastMarketDataTime() const {
		const pt::ptime result = ConvertToPTimeFromMicroseconds(m_marketDataTime);
		Assert(!result.is_not_a_date_time());
		return result;
	}

};

//////////////////////////////////////////////////////////////////////////

Security::Security(Context &context, const Symbol &symbol)
		: Base(context, symbol),
		m_pimpl(new Implementation) {
	//...//
}

Security::~Security() {
	delete m_pimpl;
}

unsigned int Security::GetPriceScale() const throw() {
	return m_pimpl->GetPriceScale();
}

ScaledPrice Security::ScalePrice(double price) const {
	return m_pimpl->ScalePrice(price);
}

double Security::DescalePrice(ScaledPrice price) const {
	return m_pimpl->DescalePrice(price);
}
double Security::DescalePrice(double price) const {
	return m_pimpl->DescalePrice(price);
}

pt::ptime Security::GetLastMarketDataTime() const {
	return m_pimpl->GetLastMarketDataTime();
}

OrderId Security::SellAtMarketPrice(
			Qty qty,
			const OrderParams &params,
			Position &position) {
	AssertLt(0, qty);
	return GetContext().GetTradeSystem().SellAtMarketPrice(
		*this,
		qty,
		params,
		position.GetSellOrderStatusUpdateSlot());
}

OrderId Security::Sell(
			Qty qty,
			ScaledPrice price,
			const OrderParams &params,
			Position &position) {
	AssertLt(0, qty);
	return GetContext().GetTradeSystem().Sell(
		*this,
		qty,
		price,
		params,
		position.GetSellOrderStatusUpdateSlot());
}

OrderId Security::SellAtMarketPriceWithStopPrice(
			Qty qty,
			ScaledPrice stopPrice,
			const OrderParams &params,
			Position &position) {
	AssertLt(0, qty);
	return GetContext().GetTradeSystem().SellAtMarketPriceWithStopPrice(
		*this,
		qty,
		stopPrice,
		params,
		position.GetSellOrderStatusUpdateSlot());
}

OrderId Security::SellOrCancel(
			Qty qty,
			ScaledPrice price,
			const OrderParams &params,
			Position &position) {
	AssertLt(0, qty);
	return GetContext().GetTradeSystem().SellOrCancel(
		*this,
		qty,
		price,
		params,
		position.GetSellOrderStatusUpdateSlot());
}

OrderId Security::BuyAtMarketPrice(
			Qty qty,
			const OrderParams &params,
			Position &position) {
	AssertLt(0, qty);
	return GetContext().GetTradeSystem().BuyAtMarketPrice(
		*this,
		qty,
		params,
		position.GetBuyOrderStatusUpdateSlot());
}

OrderId Security::Buy(
			Qty qty,
			ScaledPrice price,
			const OrderParams &params,
			Position &position) {
	AssertLt(0, qty);
	return GetContext().GetTradeSystem().Buy(
		*this,
		qty,
		price,
		params,
		position.GetBuyOrderStatusUpdateSlot());
}

OrderId Security::BuyAtMarketPriceWithStopPrice(
			Qty qty,
			ScaledPrice stopPrice,
			const OrderParams &params,
			Position &position) {
	AssertLt(0, qty);
	return GetContext().GetTradeSystem().BuyAtMarketPriceWithStopPrice(
		*this,
		qty,
		stopPrice,
		params,
		position.GetBuyOrderStatusUpdateSlot());
}

OrderId Security::BuyOrCancel(
			Qty qty,
			ScaledPrice price,
			const OrderParams &params,
			Position &position) {
	AssertLt(0, qty);
	return GetContext().GetTradeSystem().BuyOrCancel(
		*this,
		qty,
		price,
		params,
		position.GetBuyOrderStatusUpdateSlot());
}

void Security::CancelOrder(OrderId orderId) {
	GetContext().GetTradeSystem().CancelOrder(orderId);
}

void Security::CancelAllOrders() {
	GetContext().GetTradeSystem().CancelAllOrders(*this);
}

bool Security::IsLevel1Started() const {
	return m_pimpl->m_isLevel1Started;
}

void Security::StartLevel1() {
	Assert(!m_pimpl->m_isLevel1Started);
	m_pimpl->m_isLevel1Started = true;
}

bool Security::IsStarted() const {
	return IsLevel1Started();
}

void Security::SetRequestedDataStartTime(const pt::ptime &newTime) {
	if (	m_pimpl->m_requestedDataStartTime != pt::not_a_date_time
			&& m_pimpl->m_requestedDataStartTime <= newTime) {
		return;
	}
	m_pimpl->m_requestedDataStartTime = newTime;
}

const pt::ptime & Security::GetRequestedDataStartTime() const {
	return m_pimpl->m_requestedDataStartTime;
}

double Security::GetLastImpliedVolatility(bool wait /*= true*/) const {
	m_pimpl->CheckIv();
	return m_pimpl->m_impliedVolatilityLast.Get(wait);
}

void Security::SetLastImpliedVolatility(double value) {
	m_pimpl->m_impliedVolatilityLast.Set(value);
}

double Security::GetAskImpliedVolatility(bool wait /*= true*/) const {
	m_pimpl->CheckIv();
	return m_pimpl->m_impliedVolatilityAsk.Get(wait);
}

void Security::SetAskImpliedVolatility(double value) {
	m_pimpl->m_impliedVolatilityAsk.Set(value);
}

double Security::GetBidImpliedVolatility(bool wait /*= true*/) const {
	m_pimpl->CheckIv();
	return m_pimpl->m_impliedVolatilityBid.Get(wait);
}

void Security::SetBidImpliedVolatility(double value) {
	m_pimpl->m_impliedVolatilityBid.Set(value);
}

double Security::GetLastPrice() const {
	return DescalePrice(GetLastPriceScaled());
}

ScaledPrice Security::GetLastPriceScaled() const {
	return GetIfSet<LEVEL1_TICK_LAST_PRICE>(m_pimpl->m_level1);
}

Qty Security::GetLastQty() const {
	return GetIfSet<LEVEL1_TICK_LAST_QTY>(m_pimpl->m_level1);
}

Qty Security::GetTradedVolume() const {
	return GetIfSet<LEVEL1_TICK_TRADING_VOLUME>(m_pimpl->m_level1);
}

ScaledPrice Security::GetAskPriceScaled() const {
	return GetIfSet<LEVEL1_TICK_ASK_PRICE>(m_pimpl->m_level1);
}

double Security::GetAskPrice() const {
	return DescalePrice(GetAskPriceScaled());
}

Qty Security::GetAskQty() const {
	return GetIfSet<LEVEL1_TICK_ASK_QTY>(m_pimpl->m_level1);
}

ScaledPrice Security::GetBidPriceScaled() const {
	return GetIfSet<LEVEL1_TICK_BID_PRICE>(m_pimpl->m_level1);
}

double Security::GetBidPrice() const {
	return DescalePrice(GetBidPriceScaled());
}

Qty Security::GetBidQty() const {
	return GetIfSet<LEVEL1_TICK_BID_QTY>(m_pimpl->m_level1);
}

Qty Security::GetBrokerPosition() const {
	return m_pimpl->m_brokerPosition;
}

Security::Level1UpdateSlotConnection Security::SubscribeToLevel1Updates(
			const Level1UpdateSlot &slot)
		const {
	return m_pimpl->m_level1UpdateSignal.connect(slot);
}

Security::Level1UpdateSlotConnection Security::SubscribeToLevel1Ticks(
			const Level1TickSlot &slot)
		const {
	return m_pimpl->m_level1TickSignal.connect(slot);
}

Security::NewTradeSlotConnection Security::SubscribeToTrades(
			const NewTradeSlot &slot)
		const {
	return m_pimpl->m_tradeSignal.connect(slot);
}

Security::NewTradeSlotConnection Security::SubscribeToBrokerPositionUpdates(
			const BrokerPositionUpdateSlot &slot)
		const {
	return m_pimpl->m_brokerPositionUpdateSignal.connect(slot);
}

Security::NewBarSlotConnection Security::SubscribeToBars(
			const NewBarSlot &slot)
		const {
	return m_pimpl->m_barSignal.connect(slot);
}

bool Security::IsLevel1Required() const {
	return IsLevel1UpdatesRequired() || IsLevel1TicksRequired();
}

bool Security::IsLevel1UpdatesRequired() const {
	return !m_pimpl->m_level1UpdateSignal.empty();
}

bool Security::IsLevel1TicksRequired() const {
	return !m_pimpl->m_level1TickSignal.empty();
}

bool Security::IsTradesRequired() const {
	return !m_pimpl->m_tradeSignal.empty();
}

bool Security::IsBrokerPositionRequired() const {
	return !m_pimpl->m_brokerPositionUpdateSignal.empty();
}

bool Security::IsBarsRequired() const {
	return !m_pimpl->m_barSignal.empty();
}

void Security::SetLevel1(
			const pt::ptime &time,
			const Level1TickValue &tick) {
	m_pimpl->SetLevel1(time, tick, true, false);
}

void Security::SetLevel1(
			const pt::ptime &time,
			const Level1TickValue &tick1,
			const Level1TickValue &tick2) {
	AssertNe(tick1.type, tick2.type);
	m_pimpl->SetLevel1(
		time,
		tick2,
		true,
		m_pimpl->SetLevel1(time, tick1, false, false));
}

void Security::SetLevel1(
			const pt::ptime &time,
			const Level1TickValue &tick1,
			const Level1TickValue &tick2,
			const Level1TickValue &tick3) {
	AssertNe(tick1.type, tick2.type);
	AssertNe(tick1.type, tick3.type);
	AssertNe(tick2.type, tick3.type);
	m_pimpl->SetLevel1(
		time,
		tick3,
		true,
		m_pimpl->SetLevel1(
			time,
			tick2,
			false,
			m_pimpl->SetLevel1(time, tick1, false, false)));
}

void Security::AddLevel1Tick(
			const pt::ptime &time,
			const Level1TickValue &tick) {
	m_pimpl->AddLevel1Tick(time, tick, true, false);
}

void Security::AddLevel1Tick(
			const pt::ptime &time,
			const Level1TickValue &tick1,
			const Level1TickValue &tick2) {
	AssertNe(tick1.type, tick2.type);
	m_pimpl->AddLevel1Tick(
		time,
		tick2,
		true,
		m_pimpl->AddLevel1Tick(time, tick1, false, false));
}

void Security::AddLevel1Tick(
			const pt::ptime &time,
			const Level1TickValue &tick1,
			const Level1TickValue &tick2,
			const Level1TickValue &tick3) {
	AssertNe(tick1.type, tick2.type);
	AssertNe(tick1.type, tick3.type);
	AssertNe(tick2.type, tick3.type);
	m_pimpl->AddLevel1Tick(
		time,
		tick3,
		true,
		m_pimpl->AddLevel1Tick(
			time,
			tick2,
			false,
			m_pimpl->AddLevel1Tick(time, tick1, false, false)));
}

void Security::AddTrade(
			const boost::posix_time::ptime &time,
			OrderSide side,
			ScaledPrice price,
			Qty qty,
			bool useAsLastTrade,
			bool useForTradedVolume) {
	
	bool isLevel1Changed = false;
	if (useAsLastTrade) {
		if (m_pimpl->SetLevel1(
				time,
				Level1TickValue::Create<LEVEL1_TICK_LAST_QTY>(qty),
				false,
				false)) {
			isLevel1Changed = true;
		}
		if (m_pimpl->SetLevel1(
				time,
				Level1TickValue::Create<LEVEL1_TICK_LAST_PRICE>(price),
				!useForTradedVolume,
				isLevel1Changed)) {
			isLevel1Changed = true;
		}
	}
	
	AssertLt(0, qty);
	if (useForTradedVolume && qty > 0) {
		for ( ; ; ) {
			const auto &prevVal = m_pimpl->m_level1[LEVEL1_TICK_TRADING_VOLUME];
			const auto newVal
				= Level1TickValue::Create<LEVEL1_TICK_TRADING_VOLUME>(
					IsSet(prevVal) ? Qty(prevVal) + qty : qty);
			if (	m_pimpl->CompareAndSetLevel1(
						time,
						newVal,
						prevVal,
						true,
						isLevel1Changed)) {
				break;
			}
		}
	}

	m_pimpl->m_tradeSignal(time, price, qty, side);

}

void Security::AddBar(const Bar &bar) {
	m_pimpl->m_barSignal(bar);
}

void Security::SetBrokerPosition(trdk::Qty qty, bool isInitial) {
	if (m_pimpl->m_brokerPosition.exchange(qty) == qty) {
		return;
	}
	m_pimpl->m_brokerPositionUpdateSignal(qty, isInitial);
}

void Security::SetImpliedVolatilityUpdatePeriodSec(size_t seconds) {
	impliedVolatilityUpdatePeriod = seconds;
}

////////////////////////////////////////////////////////////////////////////////
