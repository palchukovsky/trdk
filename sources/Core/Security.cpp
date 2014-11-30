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

////////////////////////////////////////////////////////////////////////////////

class Security::Implementation : private boost::noncopyable {

public:

	const MarketDataSource &m_source;

	mutable boost::signals2::signal<Level1UpdateSlotSignature>
		m_level1UpdateSignal;
	mutable boost::signals2::signal<Level1TickSlotSignature> m_level1TickSignal;
	mutable boost::signals2::signal<NewTradeSlotSignature> m_tradeSignal;
	mutable boost::signals2::signal<BrokerPositionUpdateSlotSignature>
		m_brokerPositionUpdateSignal;
	mutable boost::signals2::signal<NewBarSlotSignature> m_barSignal;
	mutable boost::signals2::signal<BookUpdateTickSlotSignature>
		m_bookUpdateTickSignal;

	Level1 m_level1;
	boost::atomic<Qty> m_brokerPosition;
	boost::atomic_int64_t m_marketDataTime;
	boost::atomic_bool m_isLevel1Started;

	pt::ptime m_requestedDataStartTime;

	Book m_book;

public:

	Implementation(const MarketDataSource &source)
			: m_source(source),
			m_brokerPosition(0),
			m_marketDataTime(0),
			m_isLevel1Started(false) {
		foreach (auto &item, m_level1) {
			Unset(item);
		}
	}

	unsigned int GetPriceScale() const throw() {
		return 100;
	}

	uint8_t GetPricePrecision() const throw() {
		return 2;
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
				const TimeMeasurement::Milestones &timeMeasurement,
				bool flush,
				bool isPreviouslyChanged) {
		const bool isChanged = SetLevel1(
			time,
			tick,
			timeMeasurement,
			flush,
			isPreviouslyChanged);
		m_level1TickSignal(time, tick, flush);
		return isChanged;
	}

	bool SetLevel1(
				const pt::ptime &time,
				const Level1TickValue &tick,
				const TimeMeasurement::Milestones &timeMeasurement,
				bool flush,
				bool isPreviouslyChanged) {
		const auto tickValue = tick.Get();
		const bool isChanged
			= m_level1[tick.type].exchange(tickValue) != tickValue;
		FlushLevel1Update(
			time,
			timeMeasurement,
			flush,
			isChanged,
			isPreviouslyChanged);
		return isChanged;
	}

	bool CompareAndSetLevel1(
				const pt::ptime &time,
				const Level1TickValue &tick,
				intmax_t prevValue,
				const TimeMeasurement::Milestones &timeMeasurement,
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
		FlushLevel1Update(
			time,
			timeMeasurement,
			flush,
			true,
			isPreviouslyChanged);
		return true;
	}

	void FlushLevel1Update(
				const pt::ptime &time,
				const TimeMeasurement::Milestones &timeMeasurement,
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

		m_level1UpdateSignal(timeMeasurement);

	}

	pt::ptime GetLastMarketDataTime() const {
		const pt::ptime result = ConvertToPTimeFromMicroseconds(m_marketDataTime);
		Assert(!result.is_not_a_date_time());
		return result;
	}

};

//////////////////////////////////////////////////////////////////////////

Security::Security(
			Context &context,
			const Symbol &symbol,
			const MarketDataSource &source)
		: Base(context, symbol),
		m_pimpl(new Implementation(source)) {
	//...//
}

Security::~Security() {
	delete m_pimpl;
}

const MarketDataSource & Security::GetSource() const {
	return m_pimpl->m_source;
}

uint8_t Security::GetPricePrecision() const throw() {
	return m_pimpl->GetPricePrecision();
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

Security::BookUpdateTickSlotConnection
Security::SubscribeToBookUpdateTicks(const BookUpdateTickSlot &slot) const {
	return m_pimpl->m_bookUpdateTickSignal.connect(slot);
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
			const Level1TickValue &tick,
			const TimeMeasurement::Milestones &timeMeasurement) {
	m_pimpl->SetLevel1(time, tick, timeMeasurement, true, false);
}

void Security::SetLevel1(
			const pt::ptime &time,
			const Level1TickValue &tick1,
			const Level1TickValue &tick2,
			const TimeMeasurement::Milestones &timeMeasurement) {
	AssertNe(tick1.type, tick2.type);
	m_pimpl->SetLevel1(
		time,
		tick2,
		timeMeasurement,
		true,
		m_pimpl->SetLevel1(time, tick1, timeMeasurement, false, false));
}

void Security::SetLevel1(
			const pt::ptime &time,
			const Level1TickValue &tick1,
			const Level1TickValue &tick2,
			const Level1TickValue &tick3,
			const TimeMeasurement::Milestones &timeMeasurement) {
	AssertNe(tick1.type, tick2.type);
	AssertNe(tick1.type, tick3.type);
	AssertNe(tick2.type, tick3.type);
	m_pimpl->SetLevel1(
		time,
		tick3,
		timeMeasurement,
		true,
		m_pimpl->SetLevel1(
			time,
			tick2,
			timeMeasurement,
			false,
			m_pimpl->SetLevel1(time, tick1, timeMeasurement, false, false)));
}

void Security::SetLevel1(
			const pt::ptime &time,
			const Level1TickValue &tick1,
			const Level1TickValue &tick2,
			const Level1TickValue &tick3,
			const Level1TickValue &tick4,
			const TimeMeasurement::Milestones &timeMeasurement) {
	AssertNe(tick1.type, tick2.type);
	AssertNe(tick1.type, tick3.type);
	AssertNe(tick1.type, tick4.type);
	AssertNe(tick2.type, tick4.type);
	AssertNe(tick3.type, tick2.type);
	AssertNe(tick3.type, tick4.type);
	m_pimpl->SetLevel1(
		time,
		tick4,
		timeMeasurement,
		true,
		m_pimpl->SetLevel1(
			time,
			tick3,
			timeMeasurement,
			false,
			m_pimpl->SetLevel1(
				time,
				tick2,
				timeMeasurement,
				false,
				m_pimpl->SetLevel1(
					time,
					tick1,
					timeMeasurement,
					false,
					false))));
}

void Security::AddLevel1Tick(
			const pt::ptime &time,
			const Level1TickValue &tick,
			const TimeMeasurement::Milestones &timeMeasurement) {
	m_pimpl->AddLevel1Tick(time, tick, timeMeasurement, true, false);
}

void Security::AddLevel1Tick(
			const pt::ptime &time,
			const Level1TickValue &tick1,
			const Level1TickValue &tick2,
			const TimeMeasurement::Milestones &timeMeasurement) {
	AssertNe(tick1.type, tick2.type);
	m_pimpl->AddLevel1Tick(
		time,
		tick2,
		timeMeasurement,
		true,
		m_pimpl->AddLevel1Tick(time, tick1, timeMeasurement, false, false));
}

void Security::AddLevel1Tick(
			const pt::ptime &time,
			const Level1TickValue &tick1,
			const Level1TickValue &tick2,
			const Level1TickValue &tick3,
			const TimeMeasurement::Milestones &timeMeasurement) {
	AssertNe(tick1.type, tick2.type);
	AssertNe(tick1.type, tick3.type);
	AssertNe(tick2.type, tick3.type);
	m_pimpl->AddLevel1Tick(
		time,
		tick3,
		timeMeasurement,
		true,
		m_pimpl->AddLevel1Tick(
			time,
			tick2,
			timeMeasurement,
			false,
			m_pimpl->AddLevel1Tick(
				time,
				tick1,
				timeMeasurement,
				false,
				false)));
}

void Security::AddTrade(
			const boost::posix_time::ptime &time,
			OrderSide side,
			ScaledPrice price,
			Qty qty,
			const TimeMeasurement::Milestones &timeMeasurement,
			bool useAsLastTrade,
			bool useForTradedVolume) {
	
	bool isLevel1Changed = false;
	if (useAsLastTrade) {
		if (m_pimpl->SetLevel1(
				time,
				Level1TickValue::Create<LEVEL1_TICK_LAST_QTY>(qty),
				timeMeasurement,
				false,
				false)) {
			isLevel1Changed = true;
		}
		if (m_pimpl->SetLevel1(
				time,
				Level1TickValue::Create<LEVEL1_TICK_LAST_PRICE>(price),
				timeMeasurement,
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
						timeMeasurement,
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

Security::BookUpdateOperation Security::StartBookUpdate() {
	return BookUpdateOperation(*this, m_pimpl->m_book);
}

const Security::Book & Security::GetBook() const {
	return m_pimpl->m_book;
}

////////////////////////////////////////////////////////////////////////////////

class Security::Book::Side::Implementation : private boost::noncopyable {
public:
	std::map<ScaledPrice, Qty> m_levels;
};

Security::Book::Side::Side()
	: m_pimpl(new Implementation) {
	//...//
}

Security::Book::Side::~Side() {
	delete m_pimpl;
}

size_t Security::Book::Side::GetLevelsCount() const {
	return m_pimpl->m_levels.size();
}

Security::Book::Level Security::Book::Side::GetLevel(size_t index) const {
	if (index >= m_pimpl->m_levels.size()) {
		throw LogicError("Book price level is out of range");
	}
	auto level = m_pimpl->m_levels.cbegin();
	std::advance(level, index);
	return Level(abs(level->first), level->second);
}

///

const Security::Book::Side & Security::Book::GetBids() const {
	return m_bids;
}

const Security::Book::Side & Security::Book::GetOffers() const {
	return m_offers;
}

////////////////////////////////////////////////////////////////////////////////

class Security::BookUpdateOperation::Storage : private boost::noncopyable {

public:

	std::vector<boost::tuple<BookUpdateAction, ScaledPrice, Qty>> updates;

};

class Security::BookUpdateOperation::Implementation
		: private boost::noncopyable {

public:

	struct Side {

		BookUpdateOperation::Storage storage;
		BookUpdateOperation::Side operations;

		explicit Side(Security &security)
			: operations(security, storage) {
			//...//
		}

	};
	
	Side m_bids;
	Side m_offers;

	Security::Book &m_book;

	explicit Implementation(Security &security, Security::Book &book)
		: m_bids(security),
		m_offers(security),
		m_book(book) {
		//...//
	}

};

Security::BookUpdateOperation::Side::Side(
		Security &security,
		Security::BookUpdateOperation::Storage &storage)
	: m_security(security),
	m_storage(storage) {
	//...//
}

Security & Security::BookUpdateOperation::Side::GetSecurity() {
	return m_security;
}

void Security::BookUpdateOperation::Side::Add(
		const ScaledPrice &price,
		const Qty &qty) {
	m_storage.updates.push_back(
		boost::make_tuple(BOOK_UPDATE_ACTION_NEW, price, qty));
}
void Security::BookUpdateOperation::Side::Add(
		double price,
		const trdk::Qty &qty) {
	Add(m_security.ScalePrice(price), qty);
}

void Security::BookUpdateOperation::Side::Update(
		const ScaledPrice &price,
		const Qty &qty) {
	m_storage.updates.push_back(
		boost::make_tuple(BOOK_UPDATE_ACTION_UPDATE, price, qty));
}
void Security::BookUpdateOperation::Side::Update(double price, const Qty &qty) {
	Update(m_security.ScalePrice(price), qty);
}

void Security::BookUpdateOperation::Side::Delete(const ScaledPrice &price) {
	m_storage.updates.push_back(
		boost::make_tuple(BOOK_UPDATE_ACTION_DELETE, price, 0));
}
void Security::BookUpdateOperation::Side::Delete(double price) {
	Delete(m_security.ScalePrice(price));
}

Security::BookUpdateOperation::BookUpdateOperation(
		Security &security,
		Book &book)
	: m_pimpl(new Implementation(security, book)) {
	//...//
}

Security::BookUpdateOperation::BookUpdateOperation(BookUpdateOperation &&rhs)
	: m_pimpl(rhs.m_pimpl) {
	rhs.m_pimpl = nullptr;
}

Security::BookUpdateOperation::~BookUpdateOperation() {
	delete m_pimpl;
}

Security::BookUpdateOperation::Side & Security::BookUpdateOperation::GetBids() {
	return m_pimpl->m_bids.operations;
}

Security::BookUpdateOperation::Side &
Security::BookUpdateOperation::GetOffers() {
	return m_pimpl->m_offers.operations;
}

void Security::BookUpdateOperation::Update(const BookUpdateTick &update) {
	switch (update.side) {
		case ORDER_SIDE_BID:
			m_pimpl->m_bids.storage.updates.push_back(
				boost::make_tuple(update.action, update.price, update.qty));
			break;
		case ORDER_SIDE_OFFER:
			m_pimpl->m_offers.storage.updates.push_back(
				boost::make_tuple(update.action, update.price, update.qty));
			break;
		default:
			AssertEq(ORDER_SIDE_BID, update.side);
			break;
	}
}

void Security::BookUpdateOperation::Commit(
		const TimeMeasurement::Milestones &timeMeasurement) {
	
	const auto &update = [&](
				const OrderSide &side,
				Implementation::Side &sideData) {
		foreach (const auto &update, sideData.storage.updates) {
			const BookUpdateTick tick = {
				boost::get<0>(update),
				side,
				boost::get<1>(update),
				boost::get<2>(update),
			};
			Book::Side &side = tick.side == ORDER_SIDE_BID
				?	m_pimpl->m_book.m_bids
				:	m_pimpl->m_book.m_offers;
			ScaledPrice price = tick.price;
			if (tick.side == ORDER_SIDE_BUY) {
				price = -price;
			}
			static_assert(
				numberOfBookUpdateActions == 3,
				"Action list changed.");
			switch (tick.action) {
				case BOOK_UPDATE_ACTION_NEW:
				case BOOK_UPDATE_ACTION_UPDATE:
					side.m_pimpl->m_levels[price] = tick.qty;
					break;
				case BOOK_UPDATE_ACTION_DELETE:
					side.m_pimpl->m_levels.erase(price);
					break;
				default:
					AssertEq(BOOK_UPDATE_ACTION_NEW, tick.action);
					break;
			}
		}
	};

	const auto &signal = [&](
				const OrderSide &side,
				Implementation::Side &sideData) {
		const auto &bookUpdateTickSignal
			= sideData.operations.GetSecurity().m_pimpl->m_bookUpdateTickSignal;
		foreach (const auto &update, sideData.storage.updates) {
			const BookUpdateTick tick = {
				boost::get<0>(update),
				side,
				boost::get<1>(update),
				boost::get<2>(update),
			};
			bookUpdateTickSignal(0, tick, timeMeasurement);
		}
	};

	update(ORDER_SIDE_BID, m_pimpl->m_bids);
	update(ORDER_SIDE_OFFER, m_pimpl->m_offers);
	signal(ORDER_SIDE_BID, m_pimpl->m_bids);
	signal(ORDER_SIDE_OFFER, m_pimpl->m_offers);

}

////////////////////////////////////////////////////////////////////////////////
