﻿//
//    Created: 2018/11/14 15:36
//     Author: Eugene V. Palchukovsky
//     E-mail: eugene@palchukovsky.com
// ------------------------------------------
//    Project: Trading Robot Development Kit
//        URL: http://robotdk.com
//  Copyright: Eugene V. Palchukovsky
//

#pragma once

namespace trdk {
namespace Interaction {
namespace Poloniex {

typedef uintmax_t ProductId;

struct Product {
  ProductId id;
};

const boost::unordered_map<std::string, Product> &GetProductList();

}  // namespace Poloniex
}  // namespace Interaction
}  // namespace trdk