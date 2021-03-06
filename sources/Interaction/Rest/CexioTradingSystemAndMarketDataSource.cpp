/*******************************************************************************
 *   Created: 2018/01/18 00:30:52
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 ******************************************************************************/

#include "Prec.hpp"
#include "CexioMarketDataSource.hpp"
#include "CexioTradingSystem.hpp"

using namespace trdk;
using namespace Lib;
using namespace Interaction::Rest;

TradingSystemAndMarketDataSourceFactoryResult CreateCexio(
    const TradingMode &mode,
    Context &context,
    std::string instanceName,
    std::string title,
    const boost::property_tree::ptree &conf) {
  auto tradingSystem = boost::make_shared<CexioTradingSystem>(
      App::GetInstance(), mode, context, instanceName, title, conf);
  return {std::move(tradingSystem),
          boost::make_shared<CexioMarketDataSource>(App::GetInstance(), context,
                                                    std::move(instanceName),
                                                    std::move(title), conf)};
}
