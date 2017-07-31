/**************************************************************************
 *   Created: 2012/09/11 18:52:01
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#pragma once

#include "Core/Context.hpp"
#include "Core/MarketDataSource.hpp"
#include "Security.hpp"

namespace trdk {
namespace Interaction {
namespace Test {

class RandomMarketDataSource : public trdk::MarketDataSource {
 public:
  typedef trdk::MarketDataSource Base;

 public:
  explicit RandomMarketDataSource(size_t index,
                                  Context &context,
                                  const std::string &instanceName,
                                  const Lib::IniSectionRef &);
  virtual ~RandomMarketDataSource() override;

 public:
  virtual void Connect(const trdk::Lib::IniSectionRef &) override;
  virtual void SubscribeToSecurities() override {}

 protected:
  virtual trdk::Security &CreateNewSecurityObject(
      const trdk::Lib::Symbol &) override;
  virtual boost::optional<trdk::Lib::ContractExpiration> FindContractExpiration(
      const trdk::Lib::Symbol &, const boost::gregorian::date &) const override;
  virtual void SwitchToContract(
      trdk::Security &security,
      const trdk::Lib::ContractExpiration &&expiration) const override;

 private:
  void NotificationThread();

 private:
  boost::thread_group m_threads;
  boost::atomic_bool m_stopFlag;

  std::vector<boost::shared_ptr<Security>> m_securityList;
};
}
}
}
