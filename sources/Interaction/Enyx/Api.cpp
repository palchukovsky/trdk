/**************************************************************************
 *   Created: 2012/09/11 01:34:47
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot
 **************************************************************************/

#include "Prec.hpp"

#ifdef BOOST_WINDOWS

#	include "Core/MarketDataSource.hpp"

	boost::shared_ptr< ::LiveMarketDataSource> CreateLiveMarketDataSource() {
		Log::Info("Enyx Marked Data not implemented for this platform, loading Fake Market Data Source...");
		Dll dll("Fake", true);
		boost::shared_ptr< ::LiveMarketDataSource> result
			= dll.GetFunction<boost::shared_ptr< ::LiveMarketDataSource>()>(
				"CreateLiveMarketDataSource")();
		dll.Release();
		return result;
	}

#else

#	include "MarketData.hpp"

	using namespace Trader;
	using namespace Trader::Interaction::Enyx;

	extern "C" boost::shared_ptr< ::LiveMarketDataSource> CreateLiveMarketDataSource() {
		static volatile long isEnyxInited = false;
		if (!Interlocking::Exchange(isEnyxInited, true)) {
			const auto result = EnyxMD::init();
			Log::Info(
				TRADER_ENYX_LOG_PREFFIX "%1% Market Data interfaces available on this system.",
				result);
		}
		return boost::shared_ptr< ::LiveMarketDataSource>(new MarketDataSource);
	}

#endif
