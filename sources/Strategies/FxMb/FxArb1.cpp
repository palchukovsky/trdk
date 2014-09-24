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

const size_t FxArb1::nEquationsIndex = std::numeric_limits<size_t>::max();

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

	{
		size_t i = 0;
		GetContext().ForEachMarketDataSource(
			[&](trdk::MarketDataSource &mds) -> bool {
				m_brokersConf[i].name = mds.GetTag();
				m_brokersConf[i].tradeSystem = &GetContext().GetTradeSystem(i);
				++i;
				return true;
			});
	}

	// Loading volume and direction configuration for each symbol:
	size_t pairIndex[BROKERS_COUNT] = {};
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
				const std::string symbol = boost::trim_copy(subs.back());
				PositionConf &pos = conf.pos[symbol];
				AssertEq(0, pos.index);
				pos.index = pairIndex[brokerIndex - 1]++;
				const auto &qty = boost::lexical_cast<Qty>(value);
				pos.qty = abs(qty) * 1000;
				pos.isLong = qty >= 0;
				pos.requiredVol = 3.0;
				const char *const direction
					= !pos.isLong ? "short" : "long";
				GetLog().Info(
					"Using \"%1%\" at %2% with qty %3% (%4%).",
					boost::make_tuple(symbol, pos.index, pos.qty, direction));
				return true;
			}

			// Getting required volume:
			if (	subs.size() == 2
					&& boost::iequals(subs.front(), "requiredVol")) {
				const std::string symbol = boost::trim_copy(subs.back());
				PositionConf &pos = conf.pos[symbol];
				pos.requiredVol = boost::lexical_cast<double>(value);
				GetLog().Info(
					"Requiring %1%%% availability for \"%2%\".",
					boost::make_tuple(pos.requiredVol * 100, symbol));
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

	size_t pairIndex = std::numeric_limits<size_t>::max();
			
	// New security start - caching security object for fast getting:
	foreach (const auto &cahed, m_brokersConf.front().sendList) {
		if (!cahed.security) {
			// Cell not set yet, but already allocated (for ex. if we got
			// call with index 3 before index 1).
			continue;
		} else if (cahed.security->GetSymbol() == security.GetSymbol()) {
			pairIndex = cahed.index;
			break;
		}
	}

	if (pairIndex == std::numeric_limits<size_t>::max()) {
		// not cached yet...
		foreach (auto &broker, m_brokersConf) {
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
			Assert(
				std::numeric_limits<size_t>::max() == pairIndex
				|| pairIndex == conf->second.index);
			pairIndex = conf->second.index;
			SecurityPositionConf pos;
			static_cast<PositionConf &>(pos) = conf->second;
			pos.security = &security;
			pos.tradeSystem = broker.tradeSystem;
			// caching:
			if (pairIndex >= broker.sendList.size()) {
				broker.sendList.resize(pairIndex + 1);
			}
			Assert(!broker.sendList[pairIndex].security);
			broker.sendList[pairIndex] = pos;
		}
	}

	AssertNe(std::numeric_limits<size_t>::max(), pairIndex);

	return Base::OnSecurityStart(security);

}

FxArb1::Equations FxArb1::CreateEquations() {
			
	Equations result;
	result.resize(EQUATIONS_COUNT);
	size_t i = 0;

	const auto add = [&result, &i](const Equation &equation) {
		result[i++].first = equation;
	};
	const auto addPrint = [&result, &i](const EquationPrint &equation) {
		result[i++].second = equation;
	};
	typedef const Broker B;

	add([](const B &b1, const B &b2, double &result) -> bool {result = b1.p1.bid.Get() 			* b2.p2.bid.Get() 			* (1 / b1.p3.ask.Get());	return result > 1.000055; });
	add([](const B &b1, const B &b2, double &result) -> bool {result = b1.p1.bid.Get() 			* (1 / b2.p3.ask.Get()) 	* b1.p2.bid.Get();			return result > 1.000055; });
	add([](const B &b1, const B &b2, double &result) -> bool {result = b1.p2.bid.Get() 			* b2.p1.bid.Get() 			* (1 / b1.p3.ask.Get());	return result > 1.000055; });
	add([](const B &b1, const B &b2, double &result) -> bool {result = b1.p2.bid.Get() 			* (1 / b2.p3.ask.Get()) 	* b1.p1.bid.Get();			return result > 1.000055; });
	add([](const B &b1, const B &b2, double &result) -> bool {result = (1 / b1.p3.ask.Get()) 	* b2.p1.bid.Get() 			* b1.p2.bid.Get();			return result > 1.000055; });
	add([](const B &b1, const B &b2, double &result) -> bool {result = (1 / b1.p3.ask.Get()) 	* b2.p2.bid.Get() 			* b1.p1.bid.Get();			return result > 1.000055; });
	
	add([](const B &b1, const B &b2, double &result) -> bool {result = (1 / b1.p1.ask.Get()) 	* (1 / b2.p2.ask.Get()) 	* b1.p3.bid.Get();			return result > 1.000055; });
	add([](const B &b1, const B &b2, double &result) -> bool {result = (1 / b1.p1.ask.Get()) 	* b2.p3.bid.Get() 			* (1 / b1.p2.ask.Get());	return result > 1.000055; });
	add([](const B &b1, const B &b2, double &result) -> bool {result = (1 / b1.p2.ask.Get()) 	* (1 / b2.p1.ask.Get()) 	* b1.p3.bid.Get();			return result > 1.000055; });
	add([](const B &b1, const B &b2, double &result) -> bool {result = (1 / b1.p2.ask.Get()) 	* b2.p3.bid.Get() 			* (1 / b1.p1.ask.Get());	return result > 1.000055; });
	add([](const B &b1, const B &b2, double &result) -> bool {result = b1.p3.bid.Get() 			* (1 / b2.p1.ask.Get()) 	* (1 / b1.p2.ask.Get());	return result > 1.000055; });
	add([](const B &b1, const B &b2, double &result) -> bool {result = b1.p3.bid.Get() 			* (1 / b2.p2.ask.Get()) 	* (1 / b1.p1.ask.Get());	return result > 1.000055; });

	i = 0;

	addPrint([](const B &b1, const B &b2, Module::Log &log) { log.Trading("Equation 1 : %1% * %2% * (1 / %3%) = %4%",	boost::make_tuple(	b1.p1.bid.GetConst(), b2.p2.bid.GetConst(), b1.p3.ask.GetConst(),	b1.p1.bid.GetConst() * b2.p2.bid.GetConst()  * (1 / b1.p3.ask.GetConst())	));});
	
	addPrint([](const B &b1, const B &b2, Module::Log &log) { log.Trading("Equation 2 : %1% * (1 / %2%) * %3% = %4%",	boost::make_tuple(	b1.p1.bid.GetConst(), b2.p3.ask.GetConst(), b1.p2.bid.GetConst(),	b1.p1.bid.GetConst() * (1 / b2.p3.ask.GetConst()) * b1.p2.bid.GetConst()		));});
	
	addPrint([](const B &b1, const B &b2, Module::Log &log) { log.Trading("Equation 3 : %1% * %2% * (1 / %3%) = %4%",	boost::make_tuple(	b1.p2.bid.GetConst(), b2.p1.bid.GetConst(), b1.p3.bid.GetConst(),	b1.p2.bid.GetConst() * b2.p1.bid.GetConst() * (1 / b1.p3.bid.GetConst())		));});
	
	addPrint([](const B &b1, const B &b2, Module::Log &log) { log.Trading("Equation 4 : %1% * (1 / %2%) * %3% = %4%",	boost::make_tuple(	b1.p2.bid.GetConst(), b2.p3.bid.GetConst(), b1.p1.bid.GetConst(),	b1.p2.bid.GetConst() * (1 / b2.p3.bid.GetConst()) * b1.p1.bid.GetConst()		));});
	
	addPrint([](const B &b1, const B &b2, Module::Log &log) { log.Trading("Equation 5 : (1 / %1%) * %2% * %3% = %4%",	boost::make_tuple(	b1.p3.bid.GetConst(), b2.p1.bid.GetConst(), b1.p2.bid.GetConst(),	(1 / b1.p3.bid.GetConst()) * b2.p1.bid.GetConst() * b1.p2.bid.GetConst()		));});
	
	addPrint([](const B &b1, const B &b2, Module::Log &log) { log.Trading("Equation 6 : (1 / %1%) * %2% * %3% = %4%",	boost::make_tuple(	b1.p3.bid.GetConst(), b2.p2.bid.GetConst(), b1.p1.bid.GetConst(),	(1 / b1.p3.bid.GetConst()) * b2.p2.bid.GetConst() * b1.p1.bid.GetConst()		));});
	
	

	addPrint([](const B &b1, const B &b2, Module::Log &log) { log.Trading("Equation 7 : (1 / %1%) * (1 / %2%) * %3% = %4%",		boost::make_tuple(b1.p1.bid.GetConst(), b2.p2.bid.GetConst(), b1.p3.bid.GetConst(),		(1 / b1.p1.bid.GetConst()) * (1 / b2.p2.bid.GetConst()) * b1.p3.bid.GetConst()		));});
	
	addPrint([](const B &b1, const B &b2, Module::Log &log) { log.Trading("Equation 8 : (1 / %1%) * %2% * (1 / %3%) = %4%",		boost::make_tuple(b1.p1.bid.GetConst(), b2.p3.bid.GetConst(), b1.p2.bid.GetConst(),		(1 / b1.p1.bid.GetConst()) * b2.p3.bid.GetConst() * (1 / b1.p2.bid.GetConst())		));});
	
	addPrint([](const B &b1, const B &b2, Module::Log &log) { log.Trading("Equation 9 : (1 / %1%) * (1 / %2%) * %3% = %4%",		boost::make_tuple(b1.p2.bid.GetConst(), b2.p1.bid.GetConst(), b1.p3.bid.GetConst(),		(1 / b1.p2.bid.GetConst()) * (1 / b2.p1.bid.GetConst()) * b1.p3.bid.GetConst()		));});
	
	addPrint([](const B &b1, const B &b2, Module::Log &log) { log.Trading("Equation 10 : (1 / %1%) * %2% * (1 / %3%) = %4%",	boost::make_tuple(b1.p2.bid.GetConst(), b2.p3.bid.GetConst(), b1.p1.bid.GetConst(),		(1 / b1.p2.bid.GetConst()) * b2.p3.bid.GetConst() * (1 / b1.p1.bid.GetConst())		));});

	addPrint([](const B &b1, const B &b2, Module::Log &log) { log.Trading("Equation 11 : %1% * (1 / %2%) * (1 / %3%) = %4%",	boost::make_tuple(b1.p3.bid.GetConst(), b2.p1.bid.GetConst(), b1.p2.bid.GetConst(),		b1.p3.bid.GetConst() * (1 / b2.p1.bid.GetConst()) * (1 / b1.p2.bid.GetConst())		));});
	
	addPrint([](const B &b1, const B &b2, Module::Log &log) { log.Trading("Equation 12 : %1% * (1 / %2%) * (1 / %3%) = %4%",	boost::make_tuple(b1.p3.bid.GetConst(), b2.p2.bid.GetConst(), b1.p1.bid.GetConst(),		b1.p3.bid.GetConst() * (1 / b2.p2.bid.GetConst()) * (1 / b1.p1.bid.GetConst())		));});

	AssertEq(EQUATIONS_COUNT, result.size());
	result.shrink_to_fit();

	return result;

}

bool FxArb1::GetEquationPositionWay(size_t equationIndex, bool invert, bool opening)
{
//	GetLog().Debug("GetEquationPositionWay for equation %1% / 11 on %2% will %3% be invert", boost::make_tuple(equationIndex, opening ? "opening" : "closing", invert ? "" : "not"));

	if (opening)
	{
	    if (!invert)
	    {
		return ((equationIndex < (EQUATIONS_COUNT / 2)) ? false : true);
	    }
	    else
	    {
		return ((equationIndex < (EQUATIONS_COUNT / 2)) ? true : false);
	    }
	}
	else
	{
	    if (!invert)
	    {
		return ((equationIndex < (EQUATIONS_COUNT / 2)) ? true : false);
	    }
	    else
	    {
		return ((equationIndex < (EQUATIONS_COUNT / 2)) ? false : true);
	    }
	}

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

size_t FxArb1::CancelAllInEquationAtMarketPrice(
			size_t equationIndex,
			const Position::CloseType &closeType)
		throw() {
	size_t result = 0;
	// Cancels all opened for equation orders and close positions for it.
	try {
		auto &positions = m_positionsByEquation[equationIndex];
		foreach (auto &position, positions.positions) {
			if (position->CancelAtMarketPrice(closeType)) {
				++result;
			}
		}
		positions.positions.clear();
	} catch (...) {
		AssertFailNoException();
		Block();
	}
	return result;
}

void FxArb1::CheckConf() {
	
	// At first update it needs to check configuration, one time:
	
	if (m_isPairsByBrokerChecked) {
		return;
	}
	
	foreach (const auto &broker, m_brokersConf) {
		// Printing pairs order in sendList (must be the same as in INI-file):
		std::vector<std::string> pairs;
		foreach (const auto &pair, broker.sendList) {
			pairs.push_back(pair.security->GetSymbol().GetSymbol());
		}
		GetLog().Info("Send-list pairs order: %1%.", boost::join(pairs, ", "));
	}

	m_isPairsByBrokerChecked = true;

}

void FxArb1::LogBrokersState(
			size_t equationIndex,
			const Broker &b1,
			const Broker &b2)
		const {
	// Logging current bid/ask values for all pairs (if logging enabled).
	GetEquations()[equationIndex].second(b1, b2, GetLog());
}

void FxArb1::StartPositionsOpening(
			size_t equationIndex,
			size_t opposideEquationIndex,
			const Broker &b1,
			const Broker &b2,
			TimeMeasurement::Milestones &timeMeasurement) {

	// Sends open-orders for each configured security:

	AssertNe(equationIndex, opposideEquationIndex);

	// Logging current bid/ask values for all pairs (if logging enabled):
	LogBrokersState(equationIndex, b1, b2);

	timeMeasurement.Measure(TimeMeasurement::SM_STRATEGY_DECISION_START);

	// Info about positions (which not yet opened) for this equation:
	auto &equationPositions = GetEquationPosition(equationIndex);
	AssertEq(0, equationPositions.positions.size());
	AssertEq(0, equationPositions.activeCount);

	const auto &getSecurityByPairIndex = [&](
				size_t index)
			-> SecurityPositionConf & {
		switch (index) {
			case 0:
				Assert(b1.checkedSecurities[equationIndex][0]);
				return *b1.checkedSecurities[equationIndex][0];
			case 1:
				Assert(b2.checkedSecurities[equationIndex][0]);
				return *b2.checkedSecurities[equationIndex][0];
			case 2:
				Assert(b1.checkedSecurities[equationIndex][1]);
				return *b1.checkedSecurities[equationIndex][1];
		}
		AssertFail("Pair index is out of range");
		throw std::out_of_range("Pair index is out of range");
	};

	try {

		// Check for required volume for each pair:
		for (size_t i = 0; i < PAIRS_COUNT; ++i) {
			const SecurityPositionConf &conf = getSecurityByPairIndex(i);
			const Qty &actualQty = conf.isLong
				?	conf.security->GetAskQty()
				:	conf.security->GetBidQty();
			if (conf.qty * conf.requiredVol > actualQty) {
				GetLog().TradingEx(
					[&]() -> boost::format {
						boost::format message(
							"Can't trade: required %1% * %2% = %3% > %4%"
								" for %5% %6%, but it's not.");
						message
							%	conf.qty
							%	conf.requiredVol
							%	actualQty
							%	(conf.qty * conf.requiredVol)
							%	conf.security->GetSymbol().GetSymbol()
							%	(conf.isLong ? "ask" : "bid");
						return message;
					});
				return;
			}
		}

		// For each configured symbol we create position object and
		// sending open-order:
		for (size_t i = 0; i < PAIRS_COUNT; ++i) {
			
			const SecurityPositionConf &conf = getSecurityByPairIndex(i);

			// Position must be "shared" as it uses pattern
			// "shared from this":
			boost::shared_ptr<EquationPosition> position;
			
//			GetLog().Debug("Equation %1% / 11 pair %2% (%5%) will %3% on %4% with %6% euros", boost::make_tuple(equationIndex, (i + 1), "open", GetEquationPositionWay(equationIndex, conf.isLong, true) ? "BUY" : "SELL", conf.security->GetSymbol(), conf.qty));
			
			if (GetEquationPositionWay(equationIndex, conf.isLong, true)) {
				position.reset(
					new EquationLongPosition(
						equationIndex,
						opposideEquationIndex,
						*this,
						*conf.tradeSystem,
						*conf.security,
						conf.security->GetSymbol().GetCashCurrency(),
						conf.qty,
						conf.security->GetAskPriceScaled(),
						timeMeasurement));
			} else {
				position.reset(
					new EquationShortPosition(
						equationIndex,
						opposideEquationIndex,
						*this,
						*conf.tradeSystem,
						*conf.security,
						conf.security->GetSymbol().GetCashCurrency(),
						conf.qty,
						conf.security->GetBidPriceScaled(),
						timeMeasurement));
			}

			if (!equationPositions.activeCount) {
				timeMeasurement.Measure(
					TimeMeasurement::SM_STRATEGY_EXECUTION_START);
			}

			// Sends orders to broker:
			position->OpenAtMarketPriceImmediatelyOrCancel();
				
			// Binding all positions into one equation:
			equationPositions.positions.push_back(position);
			Verify(++equationPositions.activeCount <= PAIRS_COUNT);

		}

	} catch (...) {
		CancelAllInEquationAtMarketPrice(
			equationIndex,
			Position::CLOSE_TYPE_SYSTEM_ERROR);
		throw;
	}

	timeMeasurement.Measure(TimeMeasurement::SM_STRATEGY_DECISION_STOP);

}

void FxArb1::OnPositionUpdate(trdk::Position &positionRef) {

	// Method calls each time when one of strategy positions updates.
			
	EquationPosition &position
		= dynamic_cast<EquationPosition &>(positionRef);
	if (!position.IsObservationActive()) {
		return;
	}

	if (position.IsCompleted() || position.IsCanceled()) {

		auto &equationPositions
			= GetEquationPosition(position.GetEquationIndex());

		// Position closed, need to find out why:
		if (position.GetOpenedQty() == 0) {
			// IOC was canceled without filling, cancel all another
			// orders at equation and close positions if another orders
			// are filled:
			Assert(!IsEquationOpenedFully(position.GetEquationIndex()));
			CancelAllInEquationAtMarketPrice(
				position.GetEquationIndex(),
				Position::CLOSE_TYPE_OPEN_FAILED);
		}
		AssertEq(0, equationPositions.positions.size());
		AssertLt(0, equationPositions.activeCount);

		// We completed work with this position, forget it...
		Verify(equationPositions.activeCount--);

		position.DeactivatieObservation();

		return;

	}

	Assert(position.IsStarted());
	AssertEq(
		PAIRS_COUNT,
		GetEquationPosition(position.GetEquationIndex()).activeCount);
	AssertEq(
		PAIRS_COUNT,
		GetEquationPosition(position.GetEquationIndex()).positions.size());
	Assert(position.IsOpened());
	AssertEq(position.GetPlanedQty(), position.GetOpenedQty());

	if (!IsEquationOpenedFully(position.GetEquationIndex())) {
		// Not all orders are filled yet, we need wait more...
		return;
	}

	position.GetTimeMeasurement().Measure(
		TimeMeasurement::SM_STRATEGY_EXECUTION_REPLY);

}
