/**************************************************************************
 *   Created: 2014/11/03 13:53:42
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#include "Prec.hpp"
#include "FixStream.hpp"

namespace fix = OnixS::FIX;

using namespace trdk;
using namespace trdk::Lib;
using namespace trdk::Interaction::OnixsFixConnector;

namespace trdk { namespace Interaction { namespace OnixsFixConnector {

	class AlpariStream : public FixStream {

	public:

		explicit AlpariStream(
				size_t index,
				Context &context,
				const std::string &tag,
				const IniSectionRef &conf)
			: FixStream(index, context, tag, conf) {
			//...//
		}

		virtual ~AlpariStream() {
			try {
				GetSession().Disconnect();
			} catch (...) {
				AssertFailNoException();
			}
		}

	protected:

		virtual void OnLogout() {
			//...//
		}

		virtual void OnReconnecting() {
			// @todo Fix me!
		}

	};

} } }

////////////////////////////////////////////////////////////////////////////////

TRDK_INTERACTION_ONIXSFIXCONNECTOR_API
boost::shared_ptr<MarketDataSource>
CreateAlpariStream(
		size_t index,
		Context &context,
		const std::string &tag,
		const IniSectionRef &configuration) {
	return boost::shared_ptr<MarketDataSource>(
		new AlpariStream(index, context, tag, configuration));
}

////////////////////////////////////////////////////////////////////////////////
