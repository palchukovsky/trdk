/**************************************************************************
 *   Created: 2012/07/22 23:41:37
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot
 **************************************************************************/

#include "Prec.hpp"
#include "Util.hpp"
#include "Core/TradeSystem.hpp"
#include "Core/MarketDataSource.hpp"

namespace pt = boost::posix_time;
using namespace Trader;

void Connect(TradeSystem &tradeSystem, const IniFile &ini, const std::string &section) {
	for ( ; ; ) {
		try {
			tradeSystem.Connect(ini, section);
			break;
		} catch (const TradeSystem::ConnectError &) {
			boost::this_thread::sleep(pt::seconds(5));
		}
	}
}

void Connect(LiveMarketDataSource &marketDataSource, const IniFile &ini, const std::string &section) {
	for ( ; ; ) {
		try {
			marketDataSource.Connect(ini, section);
			break;
		} catch (const MarketDataSource::ConnectError &) {
			boost::this_thread::sleep(pt::seconds(5));
		}
	}
}
