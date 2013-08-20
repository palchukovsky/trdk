/**************************************************************************
 *   Created: 2012/07/15 13:20:35
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#pragma once

#include "Fwd.hpp"
#include "Api.h"

namespace trdk {

	////////////////////////////////////////////////////////////////////////////////

	//!	Market data source factory.
	/** Result can't be nullptr.
	  */
	typedef boost::shared_ptr<trdk::MarketDataSource> (MarketDataSourceFactory)(
			const trdk::Lib::IniFileSectionRef &);

	////////////////////////////////////////////////////////////////////////////////

	class TRDK_CORE_API MarketDataSource : private boost::noncopyable {

	public:

		class TRDK_CORE_API Error : public trdk::Lib::Exception {
		public:
			explicit Error(const char *what) throw();
		};

		class TRDK_CORE_API ConnectError : public Error {
		public:
			ConnectError() throw();
		};

	public:

		MarketDataSource();
		virtual ~MarketDataSource();

	public:

		virtual void Connect(const trdk::Lib::IniFileSectionRef &) = 0;

	public:

		//! Returns security, creates new object if it doesn't exist yet.
		/** Requires thread synchronization with FindSecurity.
		  * @sa trdk::MarketDataSource::FindSecurity
		  */
		trdk::Security & GetSecurity(
					trdk::Context &,
					const trdk::Lib::Symbol &)
				const;

		//! Finds security.
		/** Requires thread synchronization with GetSecurity.
		  * @sa trdk::MarketDataSource::GetSecurity
		  * @return nullptr if security object doesn't exist.
		  */
		trdk::Security * FindSecurity(const trdk::Lib::Symbol &) const;

	protected:

		virtual boost::shared_ptr<trdk::Security> CreateSecurity(
					trdk::Context &,
					const trdk::Lib::Symbol &)
				const
				= 0;

	private:

		class Implementation;
		Implementation *const m_pimpl;

	};

	////////////////////////////////////////////////////////////////////////////////

}
