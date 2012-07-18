/**************************************************************************
 *   Created: May 14, 2012 9:07:07 PM
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: HighmanTradingRobot
 **************************************************************************/

#pragma once

#include "Instrument.hpp"

class Position;

////////////////////////////////////////////////////////////////////////////////

class Security : public Instrument {

public:

	typedef Instrument Base;

	typedef TradeSystem::OrderStatusUpdateSlot OrderStatusUpdateSlot;

	typedef boost::posix_time::ptime MarketDataTime;

	typedef TradeSystem::OrderQty Qty;
	typedef TradeSystem::OrderPrice Price;

public:

	explicit Security(
				boost::shared_ptr<TradeSystem>,
				const std::string &symbol,
				const std::string &primaryExchange,
				const std::string &exchange);

public:

	const char * GetCurrency() const {
		return "USD";
	}

	unsigned int GetScale() const throw() {
		return 100;
	}
	
	Price Scale(double price) const {
		return Util::Scale(price, GetScale());
	}

	double Descale(Price price) const {
		return Util::Descale(price, GetScale());
	}

public:

	void Sell(Qty, Position &);
	void Sell(Qty, Price, Position &);
	void SellAtMarketPrice(Qty, Price stopPrice, Position &);
	void SellOrCancel(Qty, Price, Position &);

	void Buy(Qty, Position &);
	void Buy(Qty, Price, Position &);
	void BuyAtMarketPrice(Qty, Price stopPrice, Position &);
	void BuyOrCancel(Qty, Price, Position &);

	void CancelAllOrders();

	bool IsCompleted() const;

};

////////////////////////////////////////////////////////////////////////////////

class DynamicSecurity : public Security {

private:

	struct Level2 {
		
		unsigned int timeTick;
		volatile LONGLONG price;
		volatile LONGLONG size;

		Level2();

	};

public:

	typedef Security Base;

	typedef void (UpdateSlotSignature)();
	typedef boost::function<UpdateSlotSignature> UpdateSlot;
	typedef SignalConnection<UpdateSlot, boost::signals2::connection> UpdateSlotConnection;

public:

	explicit DynamicSecurity(
				boost::shared_ptr<TradeSystem>,
				const std::string &symbol,
				const std::string &primaryExchange,
				const std::string &exchange,
				bool logMarketData);

public:

	//! Check security for valid market data and state.
	operator bool() const;

public:

	Price GetLastScaled() const;
	Price GetAskScaled() const;
	Price GetAskLevel2Scaled() const;
	Price GetBidScaled() const;
	Price GetBidLevel2Scaled() const;
	
	double GetLast() const;
	double GetAsk() const;
	double GetAskLevel2() const;
	double GetBid() const;
	double GetBidLevel2() const;

	Qty GetAskSize();
	Qty GetBidSize();

protected:

	bool SetLast(double);
	
	bool SetAsk(double);
	bool SetBid(double);

	bool SetLast(Price);
	bool SetAsk(Price);
	bool SetBid(Price);

	void SetLevel2(
			unsigned int askTimeTick,
			double askPrice,
			Qty askSize,
			unsigned int bidTimeTick,
			double bidPrice,
			Qty bidSize);
	void SetLevel2(
			unsigned int askTimeTick,
			Price askPrice,
			Qty askSize,
			unsigned int bidTimeTick,
			Price bidPrice,
			Qty bidSize);

public:

	bool IsHistoryData() const {
		return m_isHistoryData ? true : false;
	}

public:

	void UpdateLevel1(const MarketDataTime &, double last, double ask, double bid);
	void UpdateLevel2(
			unsigned int askTimeTick,
			double ask,
			size_t askSize,
			unsigned int bidTimeTick,
			double bid,
			size_t bidSize);

	void OnHistoryDataStart();
	void OnHistoryDataEnd();

public:

	UpdateSlotConnection Subcribe(const UpdateSlot &) const;

private:

	class MarketDataLog;
	std::unique_ptr<MarketDataLog> m_marketDataLevel1Log;

	class MarketDataLevel2Log;
	std::unique_ptr<MarketDataLevel2Log> m_marketDataLevel2Log;

	mutable boost::signals2::signal<UpdateSlotSignature> m_updateSignal;

	volatile LONGLONG m_isHistoryData;

	volatile LONGLONG m_last;
	
	volatile LONGLONG m_ask;
	Level2 m_askLevel2;
	
	volatile LONGLONG m_bid;
	Level2 m_bidLevel2;

};

////////////////////////////////////////////////////////////////////////////////
