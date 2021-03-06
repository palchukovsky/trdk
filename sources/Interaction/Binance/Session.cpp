﻿//
//    Created: 2018/10/24 10:58
//     Author: Eugene V. Palchukovsky
//     E-mail: eugene@palchukovsky.com
// ------------------------------------------
//    Project: Trading Robot Development Kit
//        URL: http://robotdk.com
//  Copyright: Eugene V. Palchukovsky
//

#include "Prec.hpp"
#include "Session.hpp"

using namespace trdk;
using namespace Interaction;
using namespace Rest;
namespace net = Poco::Net;

std::unique_ptr<net::HTTPSClientSession> Binance::CreateSession(
    const Rest::Settings &settings, const bool isTrading) {
  return Rest::CreateSession("api.binance.com", settings, isTrading);
}