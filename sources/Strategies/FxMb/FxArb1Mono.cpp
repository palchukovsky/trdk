/**************************************************************************
 *   Created: 2014/08/15 01:40:01
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#include "Prec.hpp"
#include "FxArb1.hpp"

using namespace trdk;
using namespace trdk::Lib;

namespace pt = boost::posix_time;

namespace trdk { namespace Strategies { namespace FxMb {
	
	//! Mono-strategy.
	class FxArb1Mono : public FxArb1 {
		
	public:
		
		typedef FxArb1 Base;

	public:

		explicit FxArb1Mono(
					Context &context,
					const std::string &tag,
					const IniSectionRef &conf)
				: Base(context, "FxArb1Mono", tag, conf) {
			//...//
		}
		
		virtual ~FxArb1Mono() {
			//...//
		}

	public:
		
		virtual void OnLevel1Update(
					Security &,
					TimeMeasurement::Milestones &timeMeasurement) {

			// Level 1 update callback - will be called every time when
			// ask or bid will be changed for any of configured security:

			CheckConf();

			// Getting more human readable format:
			Broker b1 = GetBroker<1>();
			Broker b2 = GetBroker<2>();
			if (!b1 || !b2) {
				// Not all data received yet (from streams)...
				return;
			}
			
			double bestEquationsResult = .0;
			size_t bestEquationsIndex = nEquationsIndex;
			// One or more equations has opened positions or active orders.
			size_t currentEquationIndex = nEquationsIndex;
			// Call with actual prices each equation and search for best
			// equation:
			for (size_t i = 0; i < GetEquations().size(); ++i) {

				const auto &positions = GetEquationPosition(i);
				// activeCount > 0 says that it has "broker postions",
				// positions.empty() that we already sent orders to close.
				// Together - we sent orders, but not yet received update
				// from broker. So this position will be closed in any case
				// - command with MKT orders already sent.
				if (positions.activeCount) {
					if (positions.positions.empty()) {
						// Position in closing process - waiting until it 
						// will be finished:
						timeMeasurement.Measure(
							TimeMeasurement::SM_STRATEGY_WITHOUT_DECISION);
						return;
					}
					currentEquationIndex = i;
					break;
				}

				// Ask equation for result:
				double currentResult = .0;
				// first - call equation
				const auto &equation = GetEquations()[i];
				
				const bool equationResult
					= equation.first(b1, b2, currentResult);
				++b1.equationIndex;
				++b2.equationIndex;
				if (!equationResult) { 
					// Equation not verified.
					continue;
				}
				
				// Check current result for best result:
				if (currentResult > bestEquationsResult) {
					bestEquationsResult = currentResult;
					bestEquationsIndex = i;
				}

			}

			if (currentEquationIndex != nEquationsIndex) {
				// if there is one order opened, we do nothing on opening but
				// we check the closing
				const auto &oppositeEquationIndex
					= GetOppositeEquationIndex(currentEquationIndex);
				// First we should check - is it already has active positions or
				// order or not? At fast updates we can get situation when we
				// already sent orders for this equation at previous MD update,
				// but this orders still not updated own status (opening for
				// this, closing for opposite) as it very fast MD updates, much
				// faster then orders update.
					
				if (!GetEquationPosition(oppositeEquationIndex).activeCount) {

					const auto &equation
						= GetEquations()[oppositeEquationIndex];

					double currentResult = .0;
					b1.equationIndex
						= b2.equationIndex
						= oppositeEquationIndex;
					b1.ResetCheckedSecurities();
					b2.ResetCheckedSecurities();
					if (equation.first(b1, b2, currentResult)) {

						// Opposite equation verified, we close current
						// equation position and will start new after (if
						// at this time one of equation will return "true"
						// again).

#						ifdef DEV_VER
 							GetLog().Debug(
 								"Going to close orders on equation %1% / 12",
	 							oppositeEquationIndex);
#						endif

						LogBrokersState(oppositeEquationIndex, b1, b2);

						timeMeasurement.Measure(
							TimeMeasurement::SM_STRATEGY_DECISION_START);
						CancelAllInEquationAtMarketPrice(
							currentEquationIndex,
							Position::CLOSE_TYPE_TAKE_PROFIT);
						timeMeasurement.Measure(
							TimeMeasurement::SM_STRATEGY_DECISION_STOP);

						return;

					}

				}

				timeMeasurement.Measure(
					TimeMeasurement::SM_STRATEGY_WITHOUT_DECISION);
				return;
			
			}

			if (bestEquationsIndex == nEquationsIndex) {
				// no one equation with "true" result exists...
				timeMeasurement.Measure(
					TimeMeasurement::SM_STRATEGY_WITHOUT_DECISION);
				return;
			}

			// "best equation" exists, open positions for it:
			Assert(!Lib::IsZero(bestEquationsResult));
			AssertGt(GetEquations().size(), bestEquationsIndex);

			if (GetEquationPosition(bestEquationsIndex).activeCount) {
				// Equation already has opened positions.
					timeMeasurement.Measure(
					TimeMeasurement::SM_STRATEGY_WITHOUT_DECISION);
				return;
			}
			
#			ifdef DEV_VER
				GetLog().Debug(
 					"Going to open orders on equation %1% / 12",
	 				(bestEquationsIndex + 1));
#			endif
			
			OnEquation(bestEquationsIndex, b1, b2, timeMeasurement);
		
		}

	private:

		void OnEquation(
					size_t equationIndex,
					const Broker &b1,
					const Broker &b2,
					TimeMeasurement::Milestones &timeMeasurement) {

			// Calculates opposite equation index:
			const auto &opposideEquationIndex
				= GetOppositeEquationIndex(equationIndex);

			AssertEq(BROKERS_COUNT, GetContext().GetTradeSystemsCount());

			// Send open-orders:
			StartPositionsOpening(
				equationIndex,
				opposideEquationIndex,
				b1,
				b2,
				timeMeasurement);

		}

	};
	
} } }


////////////////////////////////////////////////////////////////////////////////

TRDK_STRATEGY_FXMB_API boost::shared_ptr<Strategy> CreateFxArb1MonoStrategy(
			Context &context,
			const std::string &tag,
			const IniSectionRef &conf) {
	return boost::shared_ptr<Strategy>(
		new Strategies::FxMb::FxArb1Mono(context, tag, conf));
}

////////////////////////////////////////////////////////////////////////////////
