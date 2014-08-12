/**************************************************************************
 *   Created: 2014/08/12 23:28:45
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#pragma once

#include "Core/Security.hpp"

namespace trdk { namespace Interaction { namespace OnixsFixConnector {

	class CurrenexSecurity : public trdk::Security {

	public:

		typedef trdk::Security Base;

	public:

		explicit CurrenexSecurity(Context &context, const Lib::Symbol &symbol)
				: Base(context, symbol) {
			StartLevel1();
		}

	public:

		void SetBid(double price, Qty qty) {
			SetLevel1(
				boost::posix_time::microsec_clock::universal_time(),
				Level1TickValue::Create<LEVEL1_TICK_BID_PRICE>(
					ScalePrice(price)),
				Level1TickValue::Create<LEVEL1_TICK_BID_QTY>(qty));
		}

		void SetOffer(double price, Qty qty) {
			SetLevel1(
				boost::posix_time::microsec_clock::universal_time(),
				Level1TickValue::Create<LEVEL1_TICK_ASK_PRICE>(
					ScalePrice(price)),
				Level1TickValue::Create<LEVEL1_TICK_ASK_QTY>(qty));
		}

	};

} } }
