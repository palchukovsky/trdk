/**************************************************************************
 *   Created: 2016/09/12 21:49:43
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#pragma once

#include "Core/MarketDataSource.hpp"

namespace trdk { namespace Tests {

	class DummyMarketDataSource : public MarketDataSource {

	public:

		DummyMarketDataSource();
		virtual ~DummyMarketDataSource();

		static DummyMarketDataSource & GetInstance();

	public:

		virtual void Connect(const trdk::Lib::IniSectionRef &);

		virtual void SubscribeToSecurities();

	protected:

		virtual trdk::Security & CreateNewSecurityObject(
				const trdk::Lib::Symbol &);

	};

} }