/**************************************************************************
 *   Created: 2013/05/01 16:43:56
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#pragma once

#include "Core/Security.hpp"

namespace trdk {
namespace Interaction {
namespace InteractiveBrokers {

class Security : public trdk::Security {
 public:
  typedef trdk::Security Base;

 public:
  explicit Security(Context &,
                    const Lib::Symbol &,
                    MarketDataSource &,
                    bool isTestSource);

 public:
  bool IsTestSource() const { return m_isTestSource; }

  using Base::IsLevel1Required;
  using Base::IsLevel1UpdatesRequired;
  using Base::IsLevel1TicksRequired;
  using Base::IsTradesRequired;
  using Base::IsBrokerPositionRequired;
  using Base::IsBarsRequired;
  using Base::AddBar;
  using Base::SetBrokerPosition;
  using Base::SetExpiration;
  using Base::SetOnline;
  using Base::SetTradingSessionState;

  void AddLevel1Tick(const boost::posix_time::ptime &time,
                     const Level1TickValue &tick,
                     const Lib::TimeMeasurement::Milestones &timeMeasurement) {
    CheckLastTrade(time, tick, timeMeasurement);
    Base::AddLevel1Tick(time, tick, timeMeasurement);
  }
  void AddLevel1Tick(const boost::posix_time::ptime &time,
                     const Level1TickValue &tick1,
                     const Level1TickValue &tick2,
                     const Lib::TimeMeasurement::Milestones &timeMeasurement) {
    CheckLastTrade(time, tick1, timeMeasurement);
    CheckLastTrade(time, tick2, timeMeasurement);
    Base::AddLevel1Tick(time, tick1, tick2, timeMeasurement);
  }
  void AddLevel1Tick(const boost::posix_time::ptime &time,
                     const Level1TickValue &tick1,
                     const Level1TickValue &tick2,
                     const Level1TickValue &tick3,
                     const Level1TickValue &tick4,
                     const Lib::TimeMeasurement::Milestones &timeMeasurement) {
    CheckLastTrade(time, tick1, timeMeasurement);
    CheckLastTrade(time, tick2, timeMeasurement);
    CheckLastTrade(time, tick3, timeMeasurement);
    CheckLastTrade(time, tick4, timeMeasurement);
    Base::AddLevel1Tick(time, tick1, tick2, tick3, tick4, timeMeasurement);
  }

 private:
  void CheckLastTrade(const boost::posix_time::ptime &time,
                      const Level1TickValue &tick,
                      const Lib::TimeMeasurement::Milestones &timeMeasurement) {
    if (!m_isTestSource || !IsTradesRequired()) {
      return;
    }
    // real time bar not available from TWS demo account:
    switch (tick.GetType()) {
      case LEVEL1_TICK_LAST_PRICE: {
        Qty lastQty = GetLastQty();
        if (lastQty == 0) {
          if (!m_firstTradeTick) {
            m_firstTradeTick = tick;
            break;
          } else if (m_firstTradeTick->GetType() == tick.GetType()) {
            break;
          } else {
            AssertEq(LEVEL1_TICK_LAST_QTY, m_firstTradeTick->GetType());
            lastQty = m_firstTradeTick->GetValue();
          }
        }
        AddTrade(time, Price(tick.GetValue()), lastQty, timeMeasurement, false);
      } break;
      case LEVEL1_TICK_LAST_QTY: {
        auto lastPrice = GetLastPrice();
        if (!lastPrice) {
          if (!m_firstTradeTick) {
            m_firstTradeTick = tick;
            break;
          } else if (m_firstTradeTick->GetType() == tick.GetType()) {
            break;
          } else {
            AssertEq(LEVEL1_TICK_LAST_PRICE, m_firstTradeTick->GetType());
            lastPrice = Price(m_firstTradeTick->GetValue());
          }
        }
        AddTrade(time, lastPrice, tick.GetValue(), timeMeasurement, false);
      } break;
      default:
        break;
    }
  }

 private:
  const bool m_isTestSource;
  boost::optional<Level1TickValue> m_firstTradeTick;
};
}
}
}
