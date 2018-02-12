/*******************************************************************************
 *   Created: 2018/01/23 11:21:51
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 ******************************************************************************/

#pragma once

#include "Core/PnlContainer.hpp"

namespace trdk {
namespace TradingLib {

class PnlOneSymbolContainer : public trdk::PnlContainer {
 public:
  PnlOneSymbolContainer();
  virtual ~PnlOneSymbolContainer() override;

 public:
  virtual bool Update(const trdk::Security &,
                      const trdk::OrderSide &,
                      const trdk::Qty &,
                      const trdk::Price &,
                      const trdk::Volume &commission) override;
  virtual boost::tribool IsProfit() const override;
  virtual const trdk::Pnl::Data &GetData() const override;

 private:
  class Implementation;
  std::unique_ptr<Implementation> m_pimpl;
};
}  // namespace TradingLib
}  // namespace trdk
