/**************************************************************************
 *   Created: 2012/07/09 18:42:14
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot
 **************************************************************************/

#include "Prec.hpp"
#include "Security.hpp"
#include "Position.hpp"

namespace fs = boost::filesystem;
namespace lt = boost::local_time;

//////////////////////////////////////////////////////////////////////////

Security::Security(
			boost::shared_ptr<TradeSystem> tradeSystem,
			const std::string &symbol,
			const std::string &primaryExchange,
			const std::string &exchange)
		: Base(tradeSystem, symbol, primaryExchange, exchange) {
	//...//
}

void Security::Sell(Qty qty, Position &position) {
	GetTradeSystem().Sell(*this, qty, position.GetSellOrderStatusUpdateSlot());
}

void Security::Sell(Qty qty, Price price, Position &position) {
	GetTradeSystem().Sell(*this, qty, price, position.GetSellOrderStatusUpdateSlot());
}

void Security::SellAtMarketPrice(Qty qty, Price stopPrice, Position &position) {
	GetTradeSystem().SellAtMarketPrice(*this, qty, stopPrice, position.GetSellOrderStatusUpdateSlot());
}

void Security::SellOrCancel(Qty qty, Price price, Position &position) {
	GetTradeSystem().SellOrCancel(*this, qty, price, position.GetSellOrderStatusUpdateSlot());
}

void Security::Buy(Qty qty, Position &position) {
	GetTradeSystem().Buy(*this, qty, position.GetBuyOrderStatusUpdateSlot());
}

void Security::Buy(Qty qty, Price price, Position &position) {
	GetTradeSystem().Buy(*this, qty, price, position.GetBuyOrderStatusUpdateSlot());
}

void Security::BuyAtMarketPrice(Qty qty, Price stopPrice, Position &position) {
	GetTradeSystem().BuyAtMarketPrice(*this, qty, stopPrice, position.GetBuyOrderStatusUpdateSlot());
}

void Security::BuyOrCancel(Qty qty, Price price, Position &position) {
	GetTradeSystem().BuyOrCancel(*this, qty, price, position.GetBuyOrderStatusUpdateSlot());
}

void Security::CancelAllOrders() {
	GetTradeSystem().CancelAllOrders(*this);
}

bool Security::IsCompleted() const {
	return GetTradeSystem().IsCompleted(*this);
}

//////////////////////////////////////////////////////////////////////////

DynamicSecurity::Level2::Level2()
		: timeTick(0) {
	Interlocking::Exchange(price, 0);
	Interlocking::Exchange(size, 0);
}

class DynamicSecurity::MarketDataLog : private boost::noncopyable {

private:
		
	typedef boost::mutex Mutex;
	typedef Mutex::scoped_lock Lock;

public:

	explicit MarketDataLog(const std::string &fullSymbol) {
		const fs::path filePath = Util::SymbolToFilePath(
			Defaults::GetMarketDataLogDir(),
			fullSymbol,
			"log");
		const bool isNew = !fs::exists(filePath);
		if (isNew) {
			fs::create_directories(filePath.branch_path());
		}
		m_file.open(filePath.c_str(), std::ios::out | std::ios::ate | std::ios::app);
		if (!m_file) {
			Log::Error(
				"Failed to open log file %1% for security market data.",
				filePath);
			throw Exception("Failed to open log file for security market data");
		}
		Log::Info("Logging \"%1%\" market data into %2%...", fullSymbol, filePath);
		if (isNew) {
			m_file << "data time,last price,ask,bid" << std::endl;
		} else {
			m_file << std::endl;
		}
	}

public:

	void Append(const MarketDataTime &time, double last, double ask, double bid) {
		const lt::local_date_time esdTime(time, Util::GetEdtTimeZone());
		// const Lock lock(m_mutex); - not required while it uses only one IQLink thread
		m_file << esdTime << "," << last << "," << ask << "," << bid << std::endl;
	}

private:

	// mutable Mutex m_mutex;
	std::ofstream m_file;

};

class DynamicSecurity::MarketDataLevel2Log : private boost::noncopyable {

private:
		
	typedef boost::mutex Mutex;
	typedef Mutex::scoped_lock Lock;

public:

	explicit MarketDataLevel2Log(const std::string &fullSymbol) {
		const fs::path filePath = Util::SymbolToFilePath(
			Defaults::GetMarketDataLogDir(),
			fullSymbol + ":Level2",
			"log");
		const bool isNew = !fs::exists(filePath);
		if (isNew) {
			fs::create_directories(filePath.branch_path());
		}
		m_file.open(filePath.c_str(), std::ios::out | std::ios::ate | std::ios::app);
		if (!m_file) {
			Log::Error(
				"Failed to open log file %1% for security market data.",
				filePath);
			throw Exception("Failed to open log file for security market data");
		}
		Log::Info("Logging \"%1%\" market data into %2%...", fullSymbol, filePath);
		if (isNew) {
			m_file << "type,time tick, price,size,price,size, tick size" << std::endl;
		} else {
			m_file << std::endl;
		}
	}

public:

	void AppendAsk(unsigned int timeTick, double price, size_t size, boost::int64_t tickSize) {
		m_file << "ask," << timeTick << "," << price << "," << size << "," << tickSize << std::endl;
	}

	void AppendBid(unsigned int timeTick, double price, size_t size, boost::int64_t tickSize) {
		m_file << "bid," << timeTick << "," << price << "," << size << "," << tickSize << std::endl;
	}

private:

	// mutable Mutex m_mutex;
	std::ofstream m_file;

};

DynamicSecurity::DynamicSecurity(
				boost::shared_ptr<TradeSystem> tradeSystem,
				const std::string &symbol,
				const std::string &primaryExchange,
				const std::string &exchange,
				bool logMarketData)
		: Base(tradeSystem, symbol, primaryExchange, exchange) {
	if (logMarketData) {
		m_marketDataLevel1Log.reset(new MarketDataLog(GetFullSymbol()));
		m_marketDataLevel2Log.reset(new MarketDataLevel2Log(GetFullSymbol()));
	}
	Interlocking::Exchange(m_isHistoryData, false);
	Interlocking::Exchange(m_last, 0);
	Interlocking::Exchange(m_ask, 0);
	Interlocking::Exchange(m_bid, 0);
}

void DynamicSecurity::UpdateLevel1(
				const MarketDataTime &time,
				double last,
				double ask,
				double bid) {
	if (m_marketDataLevel1Log) {
		m_marketDataLevel1Log->Append(time, last, ask, bid);
	}
	if (!SetLast(last) || !SetAsk(ask) || !SetBid(bid)) {
		return;
	}
	Assert(*this);
	if (!*this) {
		return;
	}
	m_updateSignal();
}

void DynamicSecurity::UpdateLevel2(
			unsigned int askTimeTick,
			double askPrice,
			size_t askSize,
			unsigned int bidTimeTick,
			double bidPrice,
			size_t bidSize) {
	SetLevel2(askTimeTick, Scale(askPrice), askSize, bidTimeTick, Scale(bidPrice), bidSize);
	if (m_marketDataLevel2Log) {
		if (askTimeTick) {
			m_marketDataLevel2Log->AppendAsk(askTimeTick, askPrice, askSize, m_askLevel2.size);
		}
		if (bidTimeTick) {
			m_marketDataLevel2Log->AppendBid(bidTimeTick, bidPrice, bidSize, m_bidLevel2.size);
		}
	}
	if (*this) {
		m_updateSignal();
	}
}

DynamicSecurity::UpdateSlotConnection DynamicSecurity::Subcribe(
			const UpdateSlot &slot)
		const {
	return UpdateSlotConnection(m_updateSignal.connect(slot));
}

void DynamicSecurity::OnHistoryDataStart() {
	if (!Interlocking::Exchange(m_isHistoryData, true) && *this) {
		m_updateSignal();
	}
}

void DynamicSecurity::OnHistoryDataEnd() {
	if (Interlocking::Exchange(m_isHistoryData, false) && *this) {
		m_updateSignal();
	}
}

DynamicSecurity::operator bool() const {
	return m_last && m_ask && m_bid;
}

DynamicSecurity::Price DynamicSecurity::GetLastScaled() const {
	return m_last;
}

Security::Price DynamicSecurity::GetAskScaled() const {
	return m_ask;
}

Security::Price DynamicSecurity::GetAskLevel2Scaled() const {
	return m_askLevel2.price;
}

Security::Price DynamicSecurity::GetBidScaled() const {
	return m_bid;
}

Security::Price DynamicSecurity::GetBidLevel2Scaled() const {
	return m_bidLevel2.price;
}

double DynamicSecurity::GetLast() const {
	return Descale(GetLastScaled());
}

double DynamicSecurity::GetAsk() const {
	return Descale(GetAskScaled());
}

double DynamicSecurity::GetAskLevel2() const {
	return Descale(GetAskLevel2Scaled());
}

double DynamicSecurity::GetBid() const {
	return Descale(GetBidScaled());
}

double DynamicSecurity::GetBidLevel2() const {
	return Descale(GetBidLevel2Scaled());
}

DynamicSecurity::Qty DynamicSecurity::GetAskSize() {
	return DynamicSecurity::Qty(m_askLevel2.size);
}

DynamicSecurity::Qty DynamicSecurity::GetBidSize() {
	return DynamicSecurity::Qty(m_bidLevel2.size);
}

bool DynamicSecurity::SetLast(double last) {
	return SetLast(Scale(last));
}

bool DynamicSecurity::SetAsk(double ask) {
	return SetAsk(Scale(ask));
}

bool DynamicSecurity::SetBid(double bid) {
	return SetBid(Scale(bid));
}

bool DynamicSecurity::SetLast(Price last) {
	if (!last) {
		return false;
	}
	return Interlocking::Exchange(m_last, last) != last;
}

bool DynamicSecurity::SetAsk(Price ask) {
	if (!ask) {
		return false;
	}
	return Interlocking::Exchange(m_ask, ask) != ask;
}

bool DynamicSecurity::SetBid(Price bid) {
	if (!bid) {
		return false;
	}
	return Interlocking::Exchange(m_bid, bid) != bid;
}

void DynamicSecurity::SetLevel2(
			unsigned int askTimeTick,
			double askPrice,
			Qty askSize,
			unsigned int bidTimeTick,
			double bidPrice,
			Qty bidSize) {
	SetLevel2(askTimeTick, Scale(askPrice), askSize, bidTimeTick, Scale(bidPrice), bidSize);
}

void DynamicSecurity::SetLevel2(
			unsigned int askTimeTick,
			Price askPrice,
			Qty askSize,
			unsigned int bidTimeTick,
			Price bidPrice,
			Qty bidSize) {
	Assert(askTimeTick || bidTimeTick);
	if (bidTimeTick) {
		if (m_bidLevel2.timeTick != bidTimeTick) {
			m_bidLevel2.timeTick = bidTimeTick;
			Interlocking::Exchange(m_bidLevel2.price, bidPrice);
			Interlocking::Exchange(m_bidLevel2.size, bidSize);
		} else {
			Interlocking::Exchange(m_bidLevel2.price, bidPrice);
			Interlocking::Exchange(m_bidLevel2.size, m_bidLevel2.size + bidSize);
		}
	}
	if (askTimeTick) {
		if (m_askLevel2.timeTick != askTimeTick) {
			m_askLevel2.timeTick = askTimeTick;
			Interlocking::Exchange(m_askLevel2.price, askPrice);
			Interlocking::Exchange(m_askLevel2.size, askSize);
		} else {
			Interlocking::Exchange(m_askLevel2.price, askPrice);
			Interlocking::Exchange(m_askLevel2.size, m_askLevel2.size + askSize);
		}
	}
}
