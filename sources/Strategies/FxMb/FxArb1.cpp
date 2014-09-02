/**************************************************************************
 *   Created: 2014/09/02 23:34:59
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#include "Prec.hpp"
#include "FxArb1.hpp"
#include "Core/StrategyPositionReporter.hpp"
#include "Core/MarketDataSource.hpp"

using namespace trdk;
using namespace trdk::Lib;
using namespace trdk::Strategies::FxMb;

namespace pt = boost::posix_time;

FxArb1::FxArb1(
			Context &context,
			const std::string &name,
			const std::string &tag,
			const IniSectionRef &conf)
		: Base(context, name, tag),
		m_equations(CreateEquations()),
		m_isPairsByBrokerChecked(false) {

	if (GetContext().GetTradeSystemsCount() != BROKERS_COUNT) {
		throw Exception("Strategy requires exact number of brokers");
	}

	if (GetContext().GetMarketDataSourcesCount() != BROKERS_COUNT) {
		throw Exception(
			"Strategy requires exact number of Market Data Sources");
	}

	// Loading volume and direction configuration for each symbol:
	conf.ForEachKey(
		[&](const std::string &key, const std::string &value) -> bool {
					
			// checking for "magic"-prefix in key name:
					
			std::deque<std::string> subs;
			boost::split(subs, key, boost::is_any_of("."));
			if (subs.size() < 2) {
				return true;
			}

			boost::trim(subs.front());
			boost::smatch broker;
			if (	!boost::regex_match(
						subs.front(),
						broker,
						boost::regex("broker_(\\d+)"))) {
				return true;
			}
			const size_t brokerIndex = boost::lexical_cast<size_t>(
				broker.str(1));
			if (brokerIndex < 1 || brokerIndex > BROKERS_COUNT) {
				GetLog().Error(
					"Failed to configure strategy:"
						" Unknown broker index %1%.",
					brokerIndex);
				throw Exception(
					"Failed to configure strategy:"
						" Unknown broker index");
			}
			subs.pop_front();
			BrokerConf &conf = m_brokersConf[brokerIndex - 1];

			boost::trim(subs.front());
					
			// Getting qty and side:
			if (	subs.size() == 2
					&& boost::iequals(subs.front(), "qty")) {
				const std::string symbol
					= boost::trim_copy(subs.back());
				PositionConf &pos = conf.pos[symbol];
				const auto &qty = boost::lexical_cast<Qty>(value);
				pos.qty = abs(qty) * 1000;
				pos.isLong = qty >= 0;
				const char *const direction
					= !pos.isLong ? "short" : "long";
				GetLog().Info(
					"Using \"%1%\" with qty %2% (%3%).",
					boost::make_tuple(symbol, pos.qty, direction));
				return true;
			}

			GetLog().Error(
					"Failed to configure strategy:"
						" Unknown broker configuration key \"%1%\".",
					key);
			throw Exception(
				"Failed to configure strategy:"
					" Unknown broker configuration key");
	
		},
		true);

}
		
FxArb1::~FxArb1() {
	//...//
}

void FxArb1::UpdateAlogImplSettings(const IniSectionRef &) {
	//...//
}

void FxArb1::ReportDecision(const Position &) const {
	//...//
}

std::auto_ptr<PositionReporter> FxArb1::CreatePositionReporter() const {
	return std::auto_ptr<PositionReporter>(
		new StrategyPositionReporter<FxArb1>);
}

pt::ptime FxArb1::OnSecurityStart(Security &security) {
			
	// New security start - caching security object for fast getting:
	const auto &key = security.GetSymbol().GetHash();
	if (!m_brokersConf.front().sendList.count(key)) {
		// not cached yet...
		foreach (auto &broker, m_brokersConf) {
			Assert(!broker.sendList.count(key));
			const auto &conf
				= broker.pos.find(security.GetSymbol().GetSymbol());
			if (conf == broker.pos.end()) {
				// symbol hasn't configuration:
				GetLog().Error(
					"Symbol %1% hasn't Qty and direction configuration.",
					security);
				throw Exception(
					"Symbol hasn't Qty and direction configuration");
			}
			SecurityPositionConf pos;
			static_cast<PositionConf &>(pos) = conf->second;
			pos.security = &security;
			// caching:
			broker.sendList.insert(std::make_pair(key, pos));
		}
	}

	// Filling matrix "b{x}.p{y}" for broker with market data source
	// index:

	size_t brokerIndex = 0;
	GetContext().ForEachMarketDataSource(
		[&](const MarketDataSource &source) -> bool {
			if (security.GetSource() == source) {
				return false;
			}
			++brokerIndex;
			return true;
		});
	AssertGt(GetContext().GetMarketDataSourcesCount(), brokerIndex);
	AssertGt(m_brokersConf.size(), brokerIndex);
	BrokerConf &broker = m_brokersConf[brokerIndex];

	bool isSet = false;
	foreach (auto &pair, broker.pairs) {
		if (!pair) {
			pair = &security;
			isSet = true;
			break;
		}
		AssertNe(pair->GetSymbol(), security.GetSymbol());
		Assert(pair->GetSource() == security.GetSource());
	}

	if (!isSet) {
		// We have predefined equations which has fixed variables so
		// configuration must provide required count of pairs.
		// If isSet is false - configuration provides more pairs then
		// required for equations.
		GetLog().Error(
			"Too much pairs (symbols) configured."
				" Count of pairs must be %1%."
				" Count of brokers must be %2%.",
			boost::make_tuple(PAIRS_COUNT, BROKERS_COUNT));
		throw Exception("Too much pairs (symbols) provided.");
	}


	return Base::OnSecurityStart(security);

}

FxArb1::Equations FxArb1::CreateEquations() {
			
	Equations result;
	result.reserve(EQUATIONS_COUNT);

	const auto add = [&result](const Equation &equation) {
		result.push_back(equation);
	};
	typedef const Broker B;

	add([](const B &b1, const B &b2, double &result) -> bool {result =		b1.p1.bid + b2.p1.bid + b2.p1.bid / 3	;	return result > 80.5	;});
	add([](const B &b1, const B &b2, double &result) -> bool {result =		b1.p2.bid + b2.p2.bid + b2.p2.bid / 3	;	return result > 80.5	;});
	add([](const B &b1, const B &b2, double &result) -> bool {result =		b1.p3.bid + b2.p3.bid + b2.p3.bid / 3	;	return result > 80.5	;});
	add([](const B &b1, const B &b2, double &result) -> bool {result =		b1.p1.ask + b2.p1.ask + b2.p1.ask / 3	;	return result > 80.5	;});
	add([](const B &b1, const B &b2, double &result) -> bool {result =		b1.p2.ask + b2.p2.ask + b2.p2.ask / 3	;	return result > 80.5	;});
	add([](const B &b1, const B &b2, double &result) -> bool {result =		b1.p3.ask + b2.p3.ask + b2.p3.ask / 3	;	return result > 80.5	;});
	add([](const B &b1, const B &b2, double &result) -> bool {result =		b1.p1.bid + b2.p2.bid + b2.p3.bid / 3	;	return result > 80.5	;});
	add([](const B &b1, const B &b2, double &result) -> bool {result =		b1.p2.bid + b2.p1.bid + b2.p3.bid / 3	;	return result > 80.5	;});
	add([](const B &b1, const B &b2, double &result) -> bool {result =		b1.p3.bid + b2.p2.bid + b2.p1.bid / 3	;	return result > 80.5	;});
	add([](const B &b1, const B &b2, double &result) -> bool {result =		b1.p1.ask + b2.p2.ask + b2.p3.ask / 3	;	return result > 80.5	;});
	add([](const B &b1, const B &b2, double &result) -> bool {result =		b1.p2.ask + b2.p1.ask + b2.p3.ask / 3	;	return result > 80.5	;});
	add([](const B &b1, const B &b2, double &result) -> bool {result =		b1.p3.ask + b2.p2.ask + b2.p1.ask / 3	;	return result > 80.5	;});

	AssertEq(EQUATIONS_COUNT, result.size());
	result.shrink_to_fit();

	return result;

}

bool FxArb1::IsEquationOpenedFully(size_t equationIndex) const {
	// Returns true if all orders for equation are filled.
	const auto &positions = m_positionsByEquation[equationIndex];
	foreach (const auto &position, positions.positions) {
		if (!position->IsOpened()) {
			return false;
		}
	}
	return !positions.positions.empty();
}

void FxArb1::CancelAllInEquationAtMarketPrice(
			size_t equationIndex,
			const Position::CloseType &closeType)
		throw() {
	// Cancels all opened for equation orders and close positions for it.
	try {
		auto &positions = m_positionsByEquation[equationIndex];
		foreach (auto &position, positions.positions) {
			position->CancelAtMarketPrice(closeType);
		}
		positions.positions.clear();
	} catch (...) {
		AssertFailNoException();
		Block();
	}
}

void FxArb1::CheckConf() {
	
	// At first update it needs to check configuration, one time:
	
	if (m_isPairsByBrokerChecked) {
		return;
	}
	
	foreach (const auto &broker, m_brokersConf) {
		foreach (const auto *pair, broker.pairs) {
			if (!pair) {
				GetLog().Error(
					"One or more pairs (symbols) not find."
						" Count of pairs must be %1%."
						" Count of brokers must be %2%.",
					boost::make_tuple(PAIRS_COUNT, BROKERS_COUNT));
			}
		}
	}

	m_isPairsByBrokerChecked = true;

}

void FxArb1::LogBrokersState(
			size_t equationIndex,
			const Broker &b1,
			const Broker &b2)
		const {
	// Logging current bid/ask values for all pairs (if logging enabled).
	GetLog().TradingEx(
		[&]() -> boost::format {
			// log message format:
			// equation    #    b{x}.p{y}.bid    b{x}.p{y}.ask    ...
			boost::format message(
				"equation\t%1%"
					"\t%2% \t%3%" // b1.p1.bid, b1.p1.ask
					"\t%4% \t%5%" // b1.p2.bid, b1.p2.ask
					"\t%6% \t%7%" // b1.p3.bid, b1.p3.ask
					"\t%8% \t%9%" // b2.p1.bid, b2.p1.ask
					"\t%10% \t%11%" // b2.p2.bid, b2.p2.ask
					"\t%12% \t%13%"); // b2.p3.bid, b2.p3.ask
			message
				% equationIndex
				% b1.p1.bid % b1.p1.ask
				% b1.p2.bid % b1.p2.ask
				% b1.p3.bid % b1.p3.ask
				% b2.p1.bid % b2.p1.ask
				% b2.p2.bid % b2.p2.ask
				% b2.p3.bid % b2.p3.ask; 
			return std::move(message);
		});
}
