/**************************************************************************
 *   Created: 2014/09/02 23:31:12
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#pragma once

#include "EquationPosition.hpp"
#include "Core/Strategy.hpp"

namespace trdk { namespace Strategies { namespace FxMb {

	//! Base class for all FxArb1-strategies.
	class FxArb1 : public Strategy {
		
	public:
		
		typedef Strategy Base;

	protected:
	
		enum {
			EQUATIONS_COUNT = 12,
			BROKERS_COUNT = 2,
			PAIRS_COUNT = 3
		};

		struct PositionConf {
			Security *security;
			Qty qty;
			bool isLong;
		};
		struct SecurityPositionConf : public PositionConf {
			Security *security;
		};

		struct BrokerConf {
			
			std::map<std::string /* symbol */, PositionConf> pos;

			std::map<Lib::Symbol::Hash, SecurityPositionConf> sendList;

			boost::array<Security *, PAIRS_COUNT> pairs;

			BrokerConf() {
				pairs.assign(nullptr);
			}


		};

		struct Broker {

			struct Pair {
				
				double bid;
				double ask;
				
				explicit Pair(const Security &security)
						: bid(security.GetBidPrice()),
						ask(security.GetAskPrice()) {
					//...//
				}

				operator bool() const {
					return !Lib::IsZero(bid) && !Lib::IsZero(ask);
				}

			};

			Pair p1;
			Pair p2;
			Pair p3;

			template<typename Storage>
			explicit Broker(const Storage &storage)
					: p1(*storage[0]),
					p2(*storage[1]),
					p3(*storage[2]) {
				//...//
			}

			operator bool() const {
				return p1 && p2 && p3;
			}

		};

		typedef boost::function<bool (const Broker &b1, const Broker &b2, double &result)>
			Equation;
		typedef std::vector<Equation> Equations;

		struct EquationOpenedPositions {
			
			size_t activeCount;
			std::vector<boost::shared_ptr<EquationPosition>> positions;

			EquationOpenedPositions()
					: activeCount(0) {
				//...//
			}

		};

	public:

		explicit FxArb1(
					Context &,
					const std::string &name,
					const std::string &tag,
					const Lib::IniSectionRef &);
		virtual ~FxArb1();

	public:

		virtual boost::posix_time::ptime OnSecurityStart(Security &);
		
		virtual void ReportDecision(const Position &) const;
		virtual std::auto_ptr<PositionReporter> CreatePositionReporter() const;

	protected:
		
		virtual void UpdateAlogImplSettings(const Lib::IniSectionRef &);

		static Equations CreateEquations();

	protected:

		//! Checks conf. Must be called at each data update.
		void CheckConf();

		Broker GetBroker(size_t id) const {
			return Broker(GetBrokerConf(id).pairs);
		}

		const BrokerConf & GetBrokerConf(size_t id) const {
			AssertLt(0, id);
			AssertGe(m_brokersConf.size(), id);
			return m_brokersConf[id - 1];
		}

		const Equations & GetEquations() const {
			return m_equations;
		}

		EquationOpenedPositions & GetEquationPosition(size_t equationIndex) {
			AssertLe(0, equationIndex);
			AssertGt(m_positionsByEquation.size(), equationIndex);
			return m_positionsByEquation[equationIndex];
		}

		//! Returns true if all orders for equation are filled.
		bool IsEquationOpenedFully(size_t equationIndex) const;
		//! Cancels all opened for equation orders and close positions for it.
		void CancelAllInEquationAtMarketPrice(
					size_t equationIndex,
					const Position::CloseType &closeType)
				throw();
	
		//! Logging current bid/ask values for all pairs (if logging enabled).
		void LogBrokersState(
					size_t equationIndex,
					const Broker &,
					const Broker &)
				const;

	private:

		const Equations m_equations;

		boost::array<BrokerConf, BROKERS_COUNT> m_brokersConf;

		boost::array<EquationOpenedPositions, EQUATIONS_COUNT>
			m_positionsByEquation;

		bool m_isPairsByBrokerChecked;

	};

} } }