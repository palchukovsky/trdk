/**************************************************************************
 *   Created: 2016/09/12 21:54:46
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#include "Prec.hpp"
#include "MarketDataSourceDummy.hpp"
#include "ContextDummy.hpp"
#include "Common/ExpirationCalendar.hpp"

using namespace trdk;
using namespace trdk::Lib;
using namespace trdk::Tests;

Dummies::MarketDataSource::MarketDataSource(trdk::Context *context)
    : trdk::MarketDataSource(
          0, context ? *context : Dummies::Context::GetInstance(), "Test") {}

Dummies::MarketDataSource &Dummies::MarketDataSource::GetInstance() {
  static Dummies::MarketDataSource result;
  return result;
}

void Dummies::MarketDataSource::Connect(const trdk::Lib::IniSectionRef &) {
  throw std::logic_error("Not supported");
}

void Dummies::MarketDataSource::SubscribeToSecurities() {
  throw std::logic_error("Not supported");
}

trdk::Security &Dummies::MarketDataSource::CreateNewSecurityObject(
    const trdk::Lib::Symbol &) {
  throw std::logic_error("Not supported");
}
