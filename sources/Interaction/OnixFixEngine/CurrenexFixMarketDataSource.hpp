/**************************************************************************
 *   Created: 2014/08/12 22:28:20
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#pragma once

#include "CurrenexFixSession.hpp"
#include "Core/MarketDataSource.hpp"

namespace trdk { namespace Interaction { namespace Onyx {

	//! FIX price stream connection with OnixS C++ FIX Engine.
	class CurrenexFixMarketDataSource
			: public trdk::MarketDataSource,
			public OnixS::FIX::ISessionListener {

	public:

		using Interactor::Error;

	public:

		explicit CurrenexFixMarketDataSource(
					const Lib::IniSectionRef &,
					Context::Log &);
		virtual ~CurrenexFixMarketDataSource();

	public:

		virtual void Connect(const trdk::Lib::IniSectionRef &);

		virtual void SubscribeToSecurities();

	public:

		virtual void onInboundApplicationMsg(
					OnixS::FIX::Message &,
					OnixS::FIX::Session *);
		virtual void onStateChange(
					OnixS::FIX::SessionState::Enum newState,
					OnixS::FIX::SessionState::Enum prevState,
					OnixS::FIX::Session *);
		virtual void onError (
					OnixS::FIX::ErrorReason::Enum,
					const std::string &description,
					OnixS::FIX::Session *session);
		virtual void onWarning (
					OnixS::FIX::WarningReason::Enum,
					const std::string &description,
					OnixS::FIX::Session *);

	protected:

		virtual boost::shared_ptr<trdk::Security> CreateSecurity(
					trdk::Context &,
					const trdk::Lib::Symbol &)
				const;

	protected:

		CurrenexSecurity * FindRequestSecurity(const OnixS::FIX::Message &);
		const CurrenexSecurity * FindRequestSecurity(
					const OnixS::FIX::Message &)
				const;
		
		std::string GetRequestSymbolStr(const OnixS::FIX::Message &) const;

	private:

		Context::Log &m_log;
		CurrenexFixSession m_session;
		std::vector<boost::shared_ptr<CurrenexSecurity>> m_securities;

	};

} } }
