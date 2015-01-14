/**************************************************************************
 *   Created: 2014/12/15 14:06:49
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#pragma once

#include "Core/Position.hpp"

namespace trdk { namespace Strategies { namespace FxMb { namespace Twd {

	////////////////////////////////////////////////////////////////////////////////

	enum Pair {
		//! Like a EUR/USD.
		PAIR_AB,
		//! Like a USD/JPY.
		PAIR_BC,
		//! Like a EUR/JPY.
		PAIR_AC,
		numberOfPairs = 3
	};

	struct PairSpeed {
		double rising;
		double falling;
	};

	////////////////////////////////////////////////////////////////////////////////

	class Position : virtual public trdk::Position {

	public:

		explicit Position(
				trdk::Strategy &strategy,
				trdk::TradeSystem &tradeSystem,
				trdk::Security &security,
				const trdk::Lib::Currency &currency,
				const trdk::Qty &qty,
				const trdk::ScaledPrice &startPrice,
				const Lib::TimeMeasurement::Milestones &timeMeasurement,
				const Pair &pair,
				const size_t leg,
				size_t ordersCount)
			: trdk::Position(
				strategy,
				tradeSystem,
				security,
				currency,
				qty,
				startPrice,
				timeMeasurement),
			m_pair(pair),
			m_leg(leg),
			m_ordersCount(ordersCount),
			m_isActive(true) {
			//...//
		}

	public:

		void UpdateStartOpenPriceFromCurrent() {
			const auto &price = GetType() == Position::TYPE_LONG
				?	GetSecurity().GetAskPriceScaled()
				:	GetSecurity().GetBidPriceScaled();
			SetOpenStartPrice(price);
		}

	public:

		void OpenAtStartPrice() {
			AssertLt(.0, GetOpenStartPrice());
			OpenImmediatelyOrCancel(GetOpenStartPrice());
			++m_ordersCount;
		}

		void OpenAtCurrentPrice() {
			AssertLt(.0, GetOpenStartPrice());
			const auto &price = GetType() == Position::TYPE_LONG
				?	GetSecurity().GetAskPriceScaled()
				:	GetSecurity().GetBidPriceScaled();
			if (Lib::IsZero(price)) {
				OpenAtStartPrice();
				return;
			}
			OpenImmediatelyOrCancel(price);
			++m_ordersCount;
		}

		void CloseAtStartPrice(const CloseType &closeType) {
			Assert(!m_isActive);
			const auto &price = GetOpenStartPrice();
			CloseImmediatelyOrCancel(closeType, price);
			m_isActive = true;
			if (Lib::IsZero(GetCloseStartPrice())) {
				SetCloseStartPrice(price);
				m_ordersCount = 1;
			} else {
				++m_ordersCount;
			}
		}

		void CloseAtCurrentPrice(const CloseType &closeType) {
			Assert(!m_isActive);
			const auto &price = GetType() == Position::TYPE_LONG
				?	GetSecurity().GetBidPriceScaled()
				:	GetSecurity().GetAskPriceScaled();
			if (Lib::IsZero(price)) {
				CloseAtStartPrice(closeType);
				return;
			}
			CloseImmediatelyOrCancel(closeType, price);
			m_isActive = true;
			if (Lib::IsZero(GetCloseStartPrice())) {
				SetCloseStartPrice(price);
				m_ordersCount = 1;
			} else {
				++m_ordersCount;
			}
		}

		const Pair & GetPair() const {
			return m_pair;
		}

		size_t GetLeg() const {
			return m_leg;
		}

		size_t GetOrdersCount() const {
			return m_ordersCount;
		}

		//! @todo remove this workaround: https://trello.com/c/QOBSd8RZ
		bool IsActive() const {
			return m_isActive;
		}

 		//! @todo remove this workaround: https://trello.com/c/QOBSd8RZ
		void Deactivate() {
			Assert(m_isActive);
			m_isActive = false;
		}

	private:

		const Pair m_pair;
		const size_t m_leg;
		size_t m_ordersCount;
		bool m_isActive;

	};

	////////////////////////////////////////////////////////////////////////////////

	class LongPosition : public Twd::Position, public trdk::LongPosition {

	public:

		explicit LongPosition(
				trdk::Strategy &strategy,
				trdk::TradeSystem &tradeSystem,
				trdk::Security &security,
				const trdk::Lib::Currency &currency,
				const trdk::Qty &qty,
				const trdk::ScaledPrice &startPrice,
				const Lib::TimeMeasurement::Milestones &timeMeasurement,
				const Pair &pair,
				const size_t leg,
				size_t ordersCount)
			: trdk::Position(
				strategy,
				tradeSystem,
				security,
				currency,
				qty,
				startPrice,
				timeMeasurement),
			Twd::Position(
				strategy,
				tradeSystem,
				security,
				currency,
				qty,
				startPrice,
				timeMeasurement,
				pair,
				leg,
				ordersCount),
			trdk::LongPosition(
				strategy,
				tradeSystem,
				security,
				currency,
				qty,
				startPrice,
				timeMeasurement) {
			//...//
		}


	};

	////////////////////////////////////////////////////////////////////////////////

	class ShortPosition : public Twd::Position, public trdk::ShortPosition {

	public:

		explicit ShortPosition(
				trdk::Strategy &strategy,
				trdk::TradeSystem &tradeSystem,
				trdk::Security &security,
				const trdk::Lib::Currency &currency,
				const trdk::Qty &qty,
				const trdk::ScaledPrice &startPrice,
				const Lib::TimeMeasurement::Milestones &timeMeasurement,
				const Pair &pair,
				const size_t leg,
				size_t ordersCount)
			: trdk::Position(
				strategy,
				tradeSystem,
				security,
				currency,
				qty,
				startPrice,
				timeMeasurement),
			Twd::Position(
				strategy,
				tradeSystem,
				security,
				currency,
				qty,
				startPrice,
				timeMeasurement,
				pair,
				leg,
				ordersCount),
			trdk::ShortPosition(
				strategy,
				tradeSystem,
				security,
				currency,
				qty,
				startPrice,
				timeMeasurement) {
			//...//
		}

	};

} } } }
