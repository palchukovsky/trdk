/*******************************************************************************
 *   Created: 2018/02/04 23:07:38
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 ******************************************************************************/

#include "Prec.hpp"
#include "Controller.hpp"
#include "Strategy.hpp"

using namespace trdk;
using namespace trdk::Lib::TimeMeasurement;
using namespace trdk::TradingLib;
using namespace trdk::Strategies::PingPong;

Controller::Controller()
    : m_isOpeningEnabled(false), m_isClosingEnabled(false) {}

bool Controller::IsOpeningEnabled() const { return m_isOpeningEnabled; }
void Controller::EnableOpening(bool isEnabled) {
  m_isOpeningEnabled = isEnabled;
}

bool Controller::IsClosingEnabled() const { return m_isClosingEnabled; }
void Controller::EnableClosing(bool isEnabled) {
  m_isClosingEnabled = isEnabled;
}

Position *Controller::OpenPosition(
    const boost::shared_ptr<Operation> &operation,
    int64_t subOperationId,
    Security &security,
    bool isLong,
    const Qty &qty,
    const Milestones &delayMeasurement) {
  if (!m_isOpeningEnabled) {
    return nullptr;
  }
  if (!BestSecurityCheckerForOrder::Create(operation->GetStrategy(), isLong,
                                           qty, false)
           ->Check(security)) {
    operation->GetStrategy().GetLog().Warn(
        "%1% is not suitable target to open %2%-position with qty %3%.",
        security,                   // 1
        isLong ? "long" : "short",  // 2
        qty);                       // 3
    const auto &tradingSystem =
        operation->GetStrategy()
            .GetTradingSystem(security.GetSource().GetIndex())
            .GetInstanceName();
    boost::polymorphic_downcast<Strategy *>(&operation->GetStrategy())
        ->RaiseEvent("Got signal from " + tradingSystem + " to open \"" +
                     std::string(isLong ? "long" : "short") +
                     "\", but funds insufficient or order does not meet "
                     "exchange requirements.");
    return nullptr;
  }
  return Base::OpenPosition(operation, subOperationId, security, isLong, qty,
                            delayMeasurement);
}

bool Controller::ClosePosition(Position &position, const CloseReason &reason) {
  if (!m_isClosingEnabled) {
    return false;
  }
  return Base::ClosePosition(position, reason);
}

void Controller::ClosePosition(Position &position) {
  if (!m_isClosingEnabled) {
    return;
  }
  if (!BestSecurityCheckerForPosition::Create(position, false)
           ->Check(position.GetSecurity())) {
    position.GetStrategy().GetLog().Error(
        "%1% is not suitable target to close position %2%/%3%.",
        position.GetSecurity(),            // 1
        position.GetOperation()->GetId(),  // 2
        position.GetSubOperationId());     // 3
    position.MarkAsCompleted();
    boost::polymorphic_downcast<Strategy *>(&position.GetStrategy())
        ->RaiseEvent(
            "Got signal from " + position.GetTradingSystem().GetInstanceName() +
            " to close \"" + std::string(position.IsLong() ? "long" : "short") +
            "\", but funds insufficient or order does not meet exchange "
            "requirements.");
    return;
  }
  Base::ClosePosition(position);
}
