/**************************************************************************
 *   Created: 2013/04/28 22:54:08
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#include "Prec.hpp"
#include "IbTradeSystem.hpp"

#ifdef BOOST_WINDOWS
#	define TRDK_INTERACTION_INTERACTIVEBROKERS_API
#else
#	define TRDK_INTERACTION_INTERACTIVEBROKERS_API extern "C"
#endif

using namespace trdk;
using namespace trdk::Lib;
namespace ib = trdk::Interaction::InteractiveBrokers;

TRDK_INTERACTION_INTERACTIVEBROKERS_API
TradeSystemFactoryResult CreateTradeSystem(
			const IniSectionRef &configuration,
			Context::Log &log) {
	TradeSystemFactoryResult result;
	boost::shared_ptr<ib::TradeSystem> tradeSystem(
		new ib::TradeSystem(configuration, log));
	boost::get<0>(result) = tradeSystem;
	boost::get<1>(result) = tradeSystem;
	return result;
}

TRDK_INTERACTION_INTERACTIVEBROKERS_API
boost::shared_ptr<MarketDataSource> CreateMarketDataSource(
			const IniSectionRef &configuration,
			Context::Log &log) {
	return boost::shared_ptr<MarketDataSource>(
		new ib::TradeSystem(configuration, log));
}