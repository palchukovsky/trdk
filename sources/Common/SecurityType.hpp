/**************************************************************************
 *   Created: 2016/04/06 21:02:38
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#pragma once

namespace trdk { namespace Lib {

	enum SecurityType {
		SECURITY_TYPE_STOCK,
		// Future Contract.
		SECURITY_TYPE_FUTURES,
		// Future Option Contract.
		SECURITY_TYPE_FUTURES_OPTIONS,
		//! Foreign Exchange Contract.
		SECURITY_TYPE_FOR,
		//! Foreign Exchange Contract.
		SECURITY_TYPE_FOR_FUTURES_OPTIONS,
		numberOfSecurityTypes
	};

	const std::string & ConvertToString(const trdk::Lib::SecurityType &);
	trdk::Lib::SecurityType ConvertSecurityTypeFromString(const std::string &);

	inline std::ostream & operator <<(
			std::ostream &oss,
			const trdk::Lib::SecurityType &securityType) {
		oss << trdk::Lib::ConvertToString(securityType);
		return oss;
	}

} }

