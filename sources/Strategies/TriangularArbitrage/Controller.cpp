/*******************************************************************************
 *   Created: 2018/03/06 11:02:09
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 ******************************************************************************/

#include "Prec.hpp"
#include "Controller.hpp"
#include "Operation.hpp"
#include "Strategy.hpp"

using namespace trdk;
using namespace trdk::TradingLib;
using namespace trdk::Strategies::TriangularArbitrage;

namespace ta = trdk::Strategies::TriangularArbitrage;

void Controller::HoldPosition(Position &position) {
  Assert(position.IsFullyOpened());
  Assert(!position.IsCompleted());
  Assert(!position.HasActiveOrders());
  position.MarkAsCompleted();
}

namespace {
bool ChooseBestExchange(Position &position) {
  const auto &checker = PositionBestSecurityChecker::Create(position);
  std::vector<std::pair<const Security *, const std::string *>> checks;
  for (auto *security :
       boost::polymorphic_downcast<ta::Operation *>(&*position.GetOperation())
           ->GetLeg(position.GetSecurity())
           .GetSecurities()) {
    checks.emplace_back(security, checker->Check(*security));
  }
  if (!checker->HasSuitableSecurity()) {
    std::ostringstream checksStr;
    for (const auto &check : checks) {
      Assert(check.first);
      Assert(check.second);
      if (&check != &checks.front()) {
        checksStr << ", ";
      }
      checksStr << *check.first << ": ";
      if (check.second) {
        checksStr << *check.second;
      } else {
        checksStr << "unknown error";
      }
    }
    position.GetStrategy().GetLog().Error(
        "Failed to find suitable security for the position \"%1%/%2%\" (actual "
        "security is \"%3%\") to close the rest of the position: %4% out of "
        "%5% (%6%).",
        position.GetOperation()->GetId(),  // 1
        position.GetSubOperationId(),      // 2
        position.GetSecurity(),            // 3
        position.GetActiveQty(),           // 4
        position.GetOpenedQty(),           // 5
        checksStr.str());                  // 6
    position.MarkAsCompleted();
    return false;
  }
  position.ReplaceTradingSystem(*checker->GetSuitableSecurity(),
                                position.GetOperation()->GetTradingSystem(
                                    *checker->GetSuitableSecurity()));
  return true;
}
}  // namespace

void Controller::ClosePosition(Position &position) {
  if (!ChooseBestExchange(position)) {
    return;
  }
  Base::ClosePosition(position);
}
