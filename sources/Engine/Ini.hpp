/**************************************************************************
 *   Created: 2012/07/22 23:16:55
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot
 **************************************************************************/

class Settings;

namespace Ini {

	//////////////////////////////////////////////////////////////////////////

	namespace Sections {
		extern const std::string common;
		extern const std::string algo;
		namespace MarketData {
			namespace Log {
				extern const std::string symbols;
			}
			extern const std::string request;
		}
	}

	namespace Key {
		extern const std::string module;
		extern const std::string symbols;
	}

	//////////////////////////////////////////////////////////////////////////

	boost::shared_ptr<Settings> LoadSettings(
			const boost::filesystem::path &,
			const boost::posix_time::ptime &now,
			bool isPlayMode);

	//////////////////////////////////////////////////////////////////////////

}
