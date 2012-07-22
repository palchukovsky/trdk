/**************************************************************************
 *   Created: May 19, 2012 1:07:25 AM
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: HighmanTradingRobot
 **************************************************************************/

#pragma once

#include "TradeSystem.hpp"

class Instrument : private boost::noncopyable {

public:

	explicit Instrument(
				boost::shared_ptr<TradeSystem> tradeSystem,
				const std::string &symbol,
				const std::string &primaryExchange,
				const std::string &exchange)
			: m_tradeSystem(tradeSystem),
			m_symbol(symbol),
			m_primaryExchange(primaryExchange),
			m_exchange(exchange),
			m_fullSymbol(Util::CreateSymbolFullStr(m_symbol, m_primaryExchange, m_exchange)) {
		//...//
	}
	explicit Instrument(
				const std::string &symbol,
				const std::string &primaryExchange,
				const std::string &exchange)
			: m_symbol(symbol),
			m_primaryExchange(primaryExchange),
			m_exchange(exchange),
			m_fullSymbol(Util::CreateSymbolFullStr(m_symbol, m_primaryExchange, m_exchange)) {
		//...//
	}

	virtual ~Instrument() {
		//...//
	}

public:

	const std::string & GetFullSymbol() const throw() {
		return m_fullSymbol;
	}
	const std::string & GetSymbol() const throw() {
		return m_symbol;
	}
	const std::string & GetPrimaryExchange() const {
		return m_primaryExchange;
	}
	const std::string & GetExchange() const {
		return m_exchange;
	}

public:

	const TradeSystem & GetTradeSystem() const {
		if (!m_tradeSystem) {
			throw Exception("Instrument doesn't connected to trade system");
		}
		return *m_tradeSystem;
	}

protected:

	TradeSystem & GetTradeSystem() {
		return *m_tradeSystem;
	}

private:

	const boost::shared_ptr<TradeSystem> m_tradeSystem;
	const std::string m_symbol;
	const std::string m_primaryExchange;
	const std::string m_exchange;
	const std::string m_fullSymbol;

};
