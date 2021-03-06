//
//    Created: 2018/10/05 06:29 PM
//     Author: Eugene V. Palchukovsky
//     E-mail: eugene@palchukovsky.com
// ------------------------------------------
//    Project: Trading Robot Development Kit
//        URL: http://robotdk.com
//  Copyright: Eugene V. Palchukovsky
//

#include "Prec.hpp"
#include "Common/VersionInfo.hpp"

using namespace trdk::Lib;

extern "C" TRDK_INTERACTION_KRAKEN_API void GetTrdkModuleVersionInfoV1(
    VersionInfoV1 *result) {
  *result = VersionInfoV1(TRDK_INTERACTION_KRAKEN_FILE_NAME);
}
