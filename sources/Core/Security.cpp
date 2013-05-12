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
using namespace trdk::Lib::Interlocking;

////////////////////////////////////////////////////////////////////////////////

namespace {

	typedef std::array<volatile int64_t, numberOfLevel1TickTypes> Level1;
	
	template<typename T>
	bool IsSet(const T &val) {
		return val != std::numeric_limits<T>::max();
	}

	template<Level1TickType tick>
	typename Level1TickValuePolicy<tick>::ValueType
	GetIfSet(
				const Level1 &level1) {
		return
			static_cast<typename Level1TickValuePolicy<tick>::ValueType>(
				IsSet(level1[tick]) ? level1[tick] : 0);
	}

	template<typename T>
	void Unset(T &val) {
		val = std::numeric_limits<T>::max();
	}

}

//////////////////////////////////////////////////////////////////////////

class Security::Implementation : private boost::noncopyable {

public:

	class MarketDataLog : private boost::noncopyable {
	public:
		explicit MarketDataLog(
					const Context &context,
					const std::string &fullSymbol);
	public:
		void Append(
				const pt::ptime &timeOfReception,
				const pt::ptime &lastTradeTime,
				double last,
				double ask,
				double bid);
	private:
		const Context &m_context;
		std::ofstream m_file;
	};
	MarketDataLog *m_marketDataLog;

	mutable boost::signals2::signal<Level1UpdateSlotSignature>
		m_level1UpdateSignal;
	mutable boost::signals2::signal<Level1TickSlotSignature>
		m_level1TickSignal;
	mutable boost::signals2::signal<NewTradeSlotSignature> m_tradeSignal;

	Level1 m_level1;
	volatile int64_t m_marketDataTime;
	volatile long m_isLevel1Started;

public:

	Implementation(
					const Instrument &instrument,
					bool logMarketData)
			: m_marketDataLog(nullptr),
			m_marketDataTime(0),
			m_isLevel1Started(false) {
		foreach (auto &item, m_level1) {
			Unset(item);
		}
		if (logMarketData) {
			m_marketDataLog = new MarketDataLog(
				instrument.GetContext(),
				instrument.GetFullSymbol());
		}
	}

	~Implementation() {
		delete m_marketDataLog;
	}

	unsigned int GetPriceScale() const throw() {
		return 100;
	}

	ScaledPrice ScalePrice(double price) const {
		return Scale(price, GetPriceScale());
	}

	double DescalePrice(ScaledPrice price) const {
		return Descale(price, GetPriceScale());
	}

	bool IsLevel1Started() const {
		return m_isLevel1Started == 0 ? false : true;
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
		const bool isChanged
			= Exchange(m_level1[tick.type], tickValue) != tickValue;
		FlushLevel1Update(time, flush, isChanged, isPreviouslyChanged);
		return isChanged;
	}

	bool CompareAndSetLevel1(
				const pt::ptime &time,
				const Level1TickValue &tick,
				long long prevValue,
				bool flush,
				bool isPreviouslyChanged) {
		const auto tickValue = tick.Get();
		AssertNe(tickValue, prevValue);
		if (tickValue == prevValue) {
			return true;
		}
		const bool isChanged
			= CompareExchange(
					m_level1[tick.type],
					tickValue,
					prevValue)
				== prevValue;
		if (!isChanged) {
			return false;
		}
		FlushLevel1Update(time, flush, isChanged, isPreviouslyChanged);
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
			foreach (auto item, m_level1) {
				if (!IsSet(item)) {
					return;
				}
			}
			Verify(!Interlocking::Exchange(m_isLevel1Started, true));
		}

		Assert(!time.is_not_a_date_time());
		AssertLe(GetLastMarketDataTime(), time);
		Exchange(m_marketDataTime, ConvertToInt64(time));

		m_level1UpdateSignal();

		if (m_marketDataLog) {
			m_marketDataLog->Append(
				boost::get_system_time(),
				GetLastMarketDataTime(),
				DescalePrice(GetIfSet<LEVEL1_TICK_LAST_PRICE>(m_level1)),
				DescalePrice(GetIfSet<LEVEL1_TICK_ASK_PRICE>(m_level1)),
				DescalePrice(GetIfSet<LEVEL1_TICK_BID_PRICE>(m_level1)));
		}

	}

	pt::ptime GetLastMarketDataTime() const {
		const pt::ptime result = ConvertToPTime(m_marketDataTime);
		Assert(!result.is_not_a_date_time());
		return result;
	}

};

//////////////////////////////////////////////////////////////////////////

Security::Implementation::MarketDataLog::MarketDataLog(
			const Context &context,
			const std::string &fullSymbol)
		: m_context(context) {
	const fs::path filePath = SymbolToFilePath(
		Defaults::GetMarketDataLogDir(),
		fullSymbol,
		"csv");
	const bool isNew = !fs::exists(filePath);
	if (isNew) {
		fs::create_directories(filePath.branch_path());
	}
	m_file.open(
		filePath.c_str(),
		std::ios::out | std::ios::ate | std::ios::app);
	if (!m_file) {
		m_context.GetLog().Error(
			"Failed to open log file %1% for security market data.",
			filePath);
		throw Exception("Failed to open log file for security market data");
	}
	m_context.GetLog().Info(
		"Logging \"%1%\" market data into %2%...",
		boost::make_tuple(boost::cref(fullSymbol), boost::cref(filePath)));
	if (isNew) {
		m_file
			<< "time of reception,last trade time,last price,ask,bid"
			<< std::endl;
	}
}

void Security::Implementation::MarketDataLog::Append(
			const pt::ptime &timeOfReception,
			const pt::ptime &lastTradeTime,
			double last,
			double ask,
			double bid) {
	m_file
		<< (timeOfReception + GetEdtDiff()).time_of_day()
		<< ',' << (lastTradeTime + GetEdtDiff()).time_of_day()
		<< ',' << last
		<< ',' << ask
		<< ',' << bid
		<< std::endl;
}

//////////////////////////////////////////////////////////////////////////


Security::Security(
			Context &context,
			const std::string &symbol,
			const std::string &primaryExchange,
			const std::string &exchange,
			bool logMarketData)
		: Base(context, symbol, primaryExchange, exchange) {
	m_pimpl = new Implementation(*this, logMarketData);
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

pt::ptime Security::GetLastMarketDataTime() const {
	return m_pimpl->GetLastMarketDataTime();
}

OrderId Security::SellAtMarketPrice(Qty qty, Position &position) {
	return GetContext().GetTradeSystem().SellAtMarketPrice(
		*this,
		qty,
		position.GetSellOrderStatusUpdateSlot());
}

OrderId Security::Sell(
			Qty qty,
			ScaledPrice price,
			Position &position) {
	return GetContext().GetTradeSystem().Sell(
		*this,
		qty,
		price,
		position.GetSellOrderStatusUpdateSlot());
}

OrderId Security::SellAtMarketPriceWithStopPrice(
			Qty qty,
			ScaledPrice stopPrice,
			Position &position) {
	return GetContext().GetTradeSystem().SellAtMarketPriceWithStopPrice(
		*this,
		qty,
		stopPrice,
		position.GetSellOrderStatusUpdateSlot());
}

OrderId Security::SellOrCancel(
			Qty qty,
			ScaledPrice price,
			Position &position) {
	return GetContext().GetTradeSystem().SellOrCancel(
		*this,
		qty,
		price,
		position.GetSellOrderStatusUpdateSlot());
}

OrderId Security::BuyAtMarketPrice(Qty qty, Position &position) {
	return GetContext().GetTradeSystem().BuyAtMarketPrice(
		*this,
		qty,
		position.GetBuyOrderStatusUpdateSlot());
}

OrderId Security::Buy(
			Qty qty,
			ScaledPrice price,
			Position &position) {
	return GetContext().GetTradeSystem().Buy(
		*this,
		qty,
		price,
		position.GetBuyOrderStatusUpdateSlot());
}

OrderId Security::BuyAtMarketPriceWithStopPrice(
			Qty qty,
			ScaledPrice stopPrice,
			Position &position) {
	return GetContext().GetTradeSystem().BuyAtMarketPriceWithStopPrice(
		*this,
		qty,
		stopPrice,
		position.GetBuyOrderStatusUpdateSlot());
}

OrderId Security::BuyOrCancel(
			Qty qty,
			ScaledPrice price,
			Position &position) {
	return GetContext().GetTradeSystem().BuyOrCancel(
		*this,
		qty,
		price,
		position.GetBuyOrderStatusUpdateSlot());
}

void Security::CancelOrder(OrderId orderId) {
	GetContext().GetTradeSystem().CancelOrder(orderId);
}

void Security::CancelAllOrders() {
	GetContext().GetTradeSystem().CancelAllOrders(*this);
}

bool Security::IsLevel1Started() const {
	return m_pimpl->IsLevel1Started();
}

bool Security::IsStarted() const {
	return IsLevel1Started();
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

Security::Level1UpdateSlotConnection Security::SubcribeToLevel1Updates(
			const Level1UpdateSlot &slot)
		const {
	return m_pimpl->m_level1UpdateSignal.connect(slot);
}

Security::Level1UpdateSlotConnection Security::SubcribeToLevel1Ticks(
			const Level1TickSlot &slot)
		const {
	return m_pimpl->m_level1TickSignal.connect(slot);
}

Security::NewTradeSlotConnection Security::SubcribeToTrades(
			const NewTradeSlot &slot)
		const {
	return m_pimpl->m_tradeSignal.connect(slot);
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

void Security::SetLevel1(
			const pt::ptime &time,
			const Level1TickValue &tick) {
	Assert(IsLevel1Required());
	m_pimpl->SetLevel1(time, tick, true, false);
}

void Security::SetLevel1(
			const pt::ptime &time,
			const Level1TickValue &tick1,
			const Level1TickValue &tick2) {
	Assert(IsLevel1UpdatesRequired());
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
	Assert(IsLevel1UpdatesRequired());
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
	Assert(IsLevel1Required());
	m_pimpl->AddLevel1Tick(time, tick, true, false);
}

void Security::AddLevel1Tick(
			const pt::ptime &time,
			const Level1TickValue &tick1,
			const Level1TickValue &tick2) {
	Assert(IsLevel1Required());
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
	Assert(IsLevel1Required());
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
	
	Assert(IsTradesRequired() || useAsLastTrade == IsLevel1Required());

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
			const auto prevVal = GetTradedVolume();
			const auto newVal
				= Level1TickValue::Create<LEVEL1_TICK_TRADING_VOLUME>(
					prevVal + qty);
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

//////////////////////////////////////////////////////////////////////////

std::ostream & std::operator <<(std::ostream &oss, const Security &security) {
	oss << security.GetFullSymbol();
	return oss;
}

//////////////////////////////////////////////////////////////////////////
