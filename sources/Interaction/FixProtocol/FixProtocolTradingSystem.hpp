/*******************************************************************************
 *   Created: 2017/10/01 04:14:20
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 ******************************************************************************/

#pragma once

#include "Core/TradingSystem.hpp"
#include "FixProtocolClient.hpp"
#include "FixProtocolHandler.hpp"

namespace trdk {
namespace Interaction {
namespace FixProtocol {

class TradingSystem : public trdk::TradingSystem, public Handler {
 public:
  typedef trdk::TradingSystem Base;

 public:
  explicit TradingSystem(const TradingMode &,
                         size_t index,
                         Context &,
                         const std::string &instanceName,
                         const Lib::IniSectionRef &);
  virtual ~TradingSystem() override = default;

 public:
  Context &GetContext() override;
  const Context &GetContext() const override;

  virtual ModuleEventsLog &GetLog() const override;

 public:
  virtual bool IsConnected() const override;

 public:
  virtual void OnConnectionRestored() override;

 protected:
  virtual void CreateConnection(const trdk::Lib::IniSectionRef &) override;

  virtual OrderId SendSellAtMarketPrice(trdk::Security &,
                                        const trdk::Lib::Currency &,
                                        const trdk::Qty &,
                                        const trdk::OrderParams &,
                                        const OrderStatusUpdateSlot &) override;
  virtual OrderId SendSell(trdk::Security &,
                           const trdk::Lib::Currency &,
                           const trdk::Qty &,
                           const trdk::Price &,
                           const trdk::OrderParams &,
                           const OrderStatusUpdateSlot &&) override;
  virtual OrderId SendSellImmediatelyOrCancel(
      trdk::Security &,
      const trdk::Lib::Currency &,
      const trdk::Qty &,
      const trdk::Price &,
      const trdk::OrderParams &,
      const OrderStatusUpdateSlot &) override;
  virtual OrderId SendSellAtMarketPriceImmediatelyOrCancel(
      trdk::Security &,
      const trdk::Lib::Currency &,
      const trdk::Qty &,
      const trdk::OrderParams &,
      const OrderStatusUpdateSlot &) override;

  virtual OrderId SendBuyAtMarketPrice(trdk::Security &,
                                       const trdk::Lib::Currency &,
                                       const trdk::Qty &,
                                       const trdk::OrderParams &,
                                       const OrderStatusUpdateSlot &) override;
  virtual OrderId SendBuy(trdk::Security &,
                          const trdk::Lib::Currency &,
                          const trdk::Qty &,
                          const trdk::Price &,
                          const trdk::OrderParams &,
                          const OrderStatusUpdateSlot &&) override;
  virtual OrderId SendBuyImmediatelyOrCancel(
      trdk::Security &,
      const trdk::Lib::Currency &,
      const trdk::Qty &,
      const trdk::Price &,
      const trdk::OrderParams &,
      const OrderStatusUpdateSlot &) override;
  virtual OrderId SendBuyAtMarketPriceImmediatelyOrCancel(
      trdk::Security &,
      const trdk::Lib::Currency &,
      const trdk::Qty &,
      const trdk::OrderParams &,
      const OrderStatusUpdateSlot &) override;

  virtual void SendCancelOrder(const OrderId &) override;

 private:
  Client m_client;
};
}
}
}