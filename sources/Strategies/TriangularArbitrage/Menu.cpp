/*******************************************************************************
 *   Created: 2018/03/06 11:07:17
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 ******************************************************************************/

#include "Prec.hpp"
#include "LegSetSelectionDialog.hpp"
#include "StrategyWindow.hpp"

using namespace trdk::FrontEnd;
using namespace trdk::Strategies::TriangularArbitrage;

StrategyMenuActionList CreateMenuActions(Engine &engine) {
  return {1,
          {QObject::tr("&Triangular Arbitrage..."),
           [&engine](QWidget *parent) -> StrategyWidgetList {
             StrategyWidgetList result;
             for (const auto &legSet :
                  LegSetSelectionDialog(engine, parent).RequestLegSet()) {
               result.emplace_back(
                   boost::make_unique<StrategyWindow>(engine, legSet, parent));
             }
             return result;
           }}};
}
