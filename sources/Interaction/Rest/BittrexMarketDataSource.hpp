/*******************************************************************************
 *   Created: 2017/11/16 12:04:46
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 ******************************************************************************/

#pragma once

#include "BittrexUtil.hpp"
#include "Settings.hpp"

namespace trdk {
namespace Interaction {
namespace Rest {

class BittrexMarketDataSource : public MarketDataSource {
 public:
  typedef MarketDataSource Base;

  explicit BittrexMarketDataSource(const App &,
                                   Context &context,
                                   std::string instanceName,
                                   std::string title,
                                   const boost::property_tree::ptree &);
  ~BittrexMarketDataSource() override;

  void Connect() override;

  void SubscribeToSecurities() override;

 protected:
  trdk::Security &CreateNewSecurityObject(const Lib::Symbol &) override;

 private:
  void UpdatePrices();
  void UpdatePrices(const boost::posix_time::ptime &,
                    const boost::property_tree::ptree &,
                    Rest::Security &,
                    const Lib::TimeMeasurement::Milestones &);

  const Settings m_settings;
  boost::unordered_map<std::string, BittrexProduct> m_products;
  boost::mutex m_securitiesLock;
  std::vector<std::pair<boost::shared_ptr<Rest::Security>,
                        std::unique_ptr<BittrexPublicRequest>>>
      m_securities;
  std::unique_ptr<Poco::Net::HTTPSClientSession> m_session;
  std::unique_ptr<PollingTask> m_pollingTask;
};

}  // namespace Rest
}  // namespace Interaction
}  // namespace trdk
