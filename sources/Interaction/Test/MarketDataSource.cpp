/**************************************************************************
 *   Created: 2016/10/15 14:10:46
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#include "Prec.hpp"
#include "MarketDataSource.hpp"
#include "Core/Settings.hpp"
#include "Core/TradingLog.hpp"

using namespace trdk;
using namespace Lib;
using namespace Interaction;
using namespace Test;
namespace ptr = boost::property_tree;

Test::MarketDataSource::MarketDataSource(Context &context,
                                         std::string instanceName,
                                         std::string title,
                                         const ptr::ptree &)
    : Base(context, std::move(instanceName), std::move(title)),
      m_stopFlag(false) {
  if (!GetContext().GetSettings().IsReplayMode()) {
    throw Exception("Failed to start without Replay Mode");
  }
}

void Test::MarketDataSource::Stop() {
  Assert(!m_stopFlag);
  m_stopFlag = true;
  m_threads.join_all();
  // Each object, that implements CreateNewSecurityObject should wait for
  // log flushing before destroying objects:
  GetTradingLog().WaitForFlush();
}

void Test::MarketDataSource::Connect() {}

void Test::MarketDataSource::SubscribeToSecurities() {
  AssertEq(0, m_threads.size());
  if (m_threads.size()) {
    return;
  }
  m_threads.create_thread([this]() {
    try {
      GetLog().Debug("Started notification thread.");
      Run();
      GetContext().RaiseStateUpdate(
          Context::STATE_DISPATCHER_TASK_STOPPED_GRACEFULLY,
          "Notification task is completed");
      GetLog().Debug("Notification thread is completed.");
    } catch (...) {
      AssertFailNoException();
      throw;
    }
  });
}

const boost::unordered_set<std::string>
    &Test::MarketDataSource::GetSymbolListHint() const {
  static boost::unordered_set<std::string> result;
  return result;
}
