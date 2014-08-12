/**************************************************************************
 *   Created: 2014/08/11 11:44:38
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#include "Prec.hpp"
#include "CurrenexFixExchange.hpp"

using namespace trdk;
using namespace trdk::Lib;
using namespace trdk::Interaction::Onyx;

namespace fix = OnixS::FIX;

CurrenexFixExchange::CurrenexFixExchange(
			const Lib::IniSectionRef &config,
			Context::Log &log)
		: Super(config, log) {
	//...//
}

CurrenexFixExchange::~CurrenexFixExchange() {
	//...//
}

void CurrenexFixExchange::ConnectSession(
			const IniSectionRef &config,
			fix::Session &session,
			const std::string &host,
			int port,
			const std::string &prefix) {
	
	fix::Message customLogonMessage("A", GetFixVersion());
	customLogonMessage.set(
		fix::FIX43::Tags::Password,
		config.ReadKey(prefix + ".password"));
#	ifdef _DEBUG
		bool resetSeqNumFlag = true;
#	else
		bool resetSeqNumFlag = false;
#	endif
	const char *const resetSeqNumFlagKey = "engine.reset_seq_num_flag";
	resetSeqNumFlag = config.ReadBoolKey(resetSeqNumFlagKey);
	GetLog().Info(
		"ResetSeqNumFlag %1%: %2% = %3%.",
		boost::make_tuple(
			boost::cref(prefix),
			resetSeqNumFlagKey),
			resetSeqNumFlag));
	if (resetSeqNumFlag) {
		customLogonMessage.setFlag(
			fix::FIX41::Tags::ResetSeqNumFlag,
			fix::FIX41::Values::ResetSeqNumFlag::Yes_reset_sequence_numbers);
	}

	try {
		if (config.ReadBoolKey(prefix + ".use_ssl")) {
			session.encryptionMethod(fix::EncryptionMethod::SSL);
		}
		session.logonAsInitiator(host, port, 30, &customLogonMessage);
	} catch (const fix::Exception &ex) {
		GetLog().Error(
			"Failed to connect to FIX Server (%1%): \"%2%\".",
			boost::make_tuple(
				boost::cref(prefix),
				ex.what()));
		throw Error("Failed to connect to FIX Server");
	}

}
