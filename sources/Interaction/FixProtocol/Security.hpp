/*******************************************************************************
 *   Created: 2017/09/20 20:26:02
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 ******************************************************************************/

#pragma once

namespace trdk {
namespace Interaction {
namespace FixProtocol {

class Security : public trdk::Security {
 public:
  typedef trdk::Security Base;

 public:
  explicit Security(Context &,
                    const Lib::Symbol &,
                    FixProtocol::MarketDataSource &,
                    const SupportedLevel1Types &);

 public:
  const size_t GetFixId() const { return m_fixId; }
  const std::string &GetFixIdCode() const { return m_fixIdCode; }

 public:
  using Base::AddLevel1Tick;
  using Base::SetOnline;
  using Base::SetTradingSessionState;

 protected:
  const size_t m_fixId;
  const std::string m_fixIdCode;
};
}
}
}