/*******************************************************************************
 *   Created: 2017/07/22 21:44:42
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 ******************************************************************************/

#include "Prec.hpp"
#include "Services/MovingAverageServiceMock.hpp"
#include "MrigeshKejriwalStrategy.hpp"
#include "Tests/MockContext.hpp"
#include "Tests/MockSecurity.hpp"
#include "Tests/MockService.hpp"

using namespace testing;
using namespace trdk::Tests;

namespace mk = trdk::Strategies::MrigeshKejriwal;
namespace svc = trdk::Services;
namespace tms = trdk::Lib::TimeMeasurement;
namespace lib = trdk::Lib;
namespace pt = boost::posix_time;

namespace {
class TrendMock : public mk::Trend {
 public:
  virtual ~TrendMock() {}

 public:
  MOCK_METHOD2(Update,
               bool(const trdk::Price &,
                    const svc::MovingAverageService::Point &));
  MOCK_CONST_METHOD0(IsRising, const boost::tribool &());
};
}

TEST(MrigeshKejriwal, Setup) {
  lib::IniString settings(
      "[Section]\n"
      "id = {00000000-0000-0000-0000-000000000000}\n"
      "title = Test\n"
      "is_enabled = true\n"
      "trading_mode = paper\n"
      "qty=99\n");
  const auto &currentTime = pt::microsec_clock::local_time();

  MockContext context;
  EXPECT_CALL(context, GetDropCopy()).WillRepeatedly(Return(nullptr));
  EXPECT_CALL(context, GetCurrentTime())
      .Times(1)
      .WillRepeatedly(Return(currentTime));

  mk::Strategy strategy(context, "test",
                        lib::IniSectionRef(settings, "Section"));

  const lib::Symbol tradingSymbol("XXX*/USD::FUT");
  MockSecurity tradingSecurity;
  tradingSecurity.SetSymbolToMock(tradingSymbol);
  {
    tradingSecurity.SetRequest(trdk::Security::Request());
    strategy.RegisterSource(tradingSecurity);
    EXPECT_EQ(currentTime - pt::hours(3),
              tradingSecurity.GetRequest().GetTime());
    EXPECT_EQ(0, tradingSecurity.GetRequest().GetNumberOfTicks());
  }
  const lib::Symbol tradingSymbol2("XXX2*/USD::FUT");
  MockSecurity tradingSecurity2;
  tradingSecurity2.SetSymbolToMock(tradingSymbol2);
  EXPECT_THROW(strategy.RegisterSource(tradingSecurity2), lib::Exception);
  EXPECT_TRUE(tradingSecurity2.GetRequest().GetTime().is_not_a_date_time());
  EXPECT_EQ(0, tradingSecurity2.GetRequest().GetNumberOfTicks());

  const lib::Symbol spotSymbol("XXX/USD::INDEX");
  MockSecurity spotSecurity;
  spotSecurity.SetSymbolToMock(spotSymbol);
  {
    strategy.RegisterSource(spotSecurity);
    EXPECT_TRUE(spotSecurity.GetRequest().GetTime().is_not_a_date_time());
    EXPECT_EQ(0, spotSecurity.GetRequest().GetNumberOfTicks());
  }
  const lib::Symbol spotSymbol2("XXX2/USD::INDEX");
  MockSecurity spotSecurity2;
  spotSecurity2.SetSymbolToMock(spotSymbol2);
  EXPECT_THROW(strategy.RegisterSource(spotSecurity2), lib::Exception);
  EXPECT_TRUE(spotSecurity2.GetRequest().GetTime().is_not_a_date_time());
  EXPECT_EQ(0, spotSecurity2.GetRequest().GetNumberOfTicks());

  MockService unknownService;
  EXPECT_NO_THROW(strategy.OnServiceStart(unknownService));
  MovingAverageServiceMock maService;
  EXPECT_NO_THROW(strategy.OnServiceStart(maService));
  EXPECT_THROW(strategy.OnServiceStart(maService), trdk::Lib::Exception);
  EXPECT_NO_THROW(strategy.OnServiceStart(unknownService));
}

TEST(MrigeshKejriwal, DISABLED_Position) {
  const lib::IniString settings(
      "[Section]\n"
      "id = {00000000-0000-0000-0000-000000000000}\n"
      "title = Test\n"
      "is_enabled = true\n"
      "trading_mode = paper\n"
      "qty=99\n");
  const auto &currentTime = pt::microsec_clock::local_time();

  MockContext context;
  EXPECT_CALL(context, GetDropCopy()).WillRepeatedly(Return(nullptr));
  EXPECT_CALL(context, GetCurrentTime()).WillRepeatedly(Return(currentTime));

  auto trend = boost::make_shared<TrendMock>();

  mk::Strategy strategy(context, "test",
                        lib::IniSectionRef(settings, "Section"), trend);

  const lib::Symbol tradingSymbol("XXX*/USD::FUT");
  MockSecurity tradingSecurity;
  tradingSecurity.SetSymbolToMock(tradingSymbol);
  strategy.RegisterSource(tradingSecurity);

  const lib::Symbol spotSymbol("XXX/USD::INDEX");
  MockSecurity spotSecurity;
  spotSecurity.SetSymbolToMock(spotSymbol);
  strategy.RegisterSource(spotSecurity);

  MovingAverageServiceMock ma;
  strategy.OnServiceStart(ma);

  {
    EXPECT_CALL(ma, GetLastPoint()).Times(0);
    EXPECT_CALL(ma, IsEmpty()).Times(0);
    EXPECT_CALL(*trend, Update(_, _)).Times(0);
    EXPECT_CALL(*trend, IsRising()).Times(0);
    strategy.RaiseServiceDataUpdateEvent(ma, tms::Milestones());
    strategy.RaiseServiceDataUpdateEvent(ma, tms::Milestones());
  }

  {
    EXPECT_CALL(ma, GetLastPoint()).Times(0);
    EXPECT_CALL(ma, IsEmpty()).Times(0);
    EXPECT_CALL(*trend, Update(_, _)).Times(0);
    EXPECT_CALL(*trend, IsRising()).Times(0);
    strategy.RaiseLevel1UpdateEvent(tradingSecurity, tms::Milestones());
  }

  {
    EXPECT_CALL(ma, GetLastPoint()).Times(0);
    EXPECT_CALL(ma, IsEmpty()).WillRepeatedly(Return(true));
    EXPECT_CALL(*trend, Update(_, _)).Times(0);
    EXPECT_CALL(*trend, IsRising()).Times(0);
    strategy.RaiseLevel1UpdateEvent(spotSecurity, tms::Milestones());
  }

  const svc::MovingAverageService::Point point = {
      currentTime + pt::seconds(123), 1.11, 2.22};
  EXPECT_CALL(ma, GetLastPoint()).WillRepeatedly(ReturnRef(point));
  EXPECT_CALL(ma, IsEmpty()).WillRepeatedly(Return(false));

  const trdk::Price lastPrice(987.65);
  EXPECT_CALL(spotSecurity, GetLastPrice()).WillRepeatedly(Return(lastPrice));

  {
    EXPECT_CALL(*trend, Update(lastPrice, point)).WillOnce(Return(false));
    EXPECT_CALL(*trend, IsRising()).Times(0);
    strategy.RaiseLevel1UpdateEvent(spotSecurity, tms::Milestones());
  }

  EXPECT_CALL(*trend, Update(lastPrice, point))
      .Times(2)
      .WillRepeatedly(Return(true));

  //   TradingSystemMock tradingSystem;
  //   EXPECT_CALL(context, GetTradingSystem(0, "paper"))
  //       .Times(2)
  //       .WillRepeatedly(ReturnRef(tradingSystem));

  {
    const boost::tribool isRising(true);
    EXPECT_CALL(*trend, IsRising()).WillRepeatedly(ReturnRef(isRising));
    const trdk::ScaledPrice askPrice = 87654;
    EXPECT_CALL(tradingSecurity, GetBidPriceScaled()).Times(0);
    EXPECT_CALL(tradingSecurity, GetAskPriceScaled())
        .WillRepeatedly(Return(askPrice));
    strategy.RaiseLevel1UpdateEvent(spotSecurity, tms::Milestones());
  }

  {
    const boost::tribool isRising(false);
    const trdk::ScaledPrice bidPrice = 65478;
    EXPECT_CALL(tradingSecurity, GetBidPriceScaled())
        .Times(1)
        .WillRepeatedly(Return(bidPrice));
    EXPECT_CALL(tradingSecurity, GetAskPriceScaled()).Times(0);
    EXPECT_CALL(*trend, IsRising()).WillRepeatedly(ReturnRef(isRising));
    strategy.RaiseLevel1UpdateEvent(spotSecurity, tms::Milestones());
  }
}

TEST(MrigeshKejriwal, Trend) {
  mk::Trend trend;
  EXPECT_TRUE(boost::indeterminate(trend.IsRising()));
  {
    // Waiting for 1st signal.
    const svc::MovingAverageService::Point point = {
        pt::second_clock::local_time(), 10, 20};
    ASSERT_FALSE(trend.Update(5, point));
    EXPECT_EQ(boost::tribool(false), trend.IsRising());
  }
  {
    const svc::MovingAverageService::Point point = {
        pt::second_clock::local_time(), 10, 20};
    ASSERT_FALSE(trend.Update(10, point));
    EXPECT_EQ(boost::tribool(false), trend.IsRising());
  }
  {
    const svc::MovingAverageService::Point point = {
        pt::second_clock::local_time(), 1000, 30};
    ASSERT_FALSE(trend.Update(20, point));
    EXPECT_EQ(boost::tribool(false), trend.IsRising());
  }
  {
    const svc::MovingAverageService::Point point = {
        pt::second_clock::local_time(), 10000, 30};
    ASSERT_TRUE(trend.Update(40, point));
    EXPECT_EQ(boost::tribool(true), trend.IsRising());
  }
  {
    const svc::MovingAverageService::Point point = {
        pt::second_clock::local_time(), 10000, 50};
    ASSERT_FALSE(trend.Update(51, point));
    EXPECT_EQ(boost::tribool(true), trend.IsRising());
  }
  {
    const svc::MovingAverageService::Point point = {
        pt::second_clock::local_time(), 10000, 50};
    ASSERT_FALSE(trend.Update(51, point));
    EXPECT_EQ(boost::tribool(true), trend.IsRising());
  }
  {
    const svc::MovingAverageService::Point point = {
        pt::second_clock::local_time(), 10000, 50};
    ASSERT_FALSE(trend.Update(50, point));
    EXPECT_EQ(boost::tribool(true), trend.IsRising());
  }
  {
    const svc::MovingAverageService::Point point = {
        pt::second_clock::local_time(), 10000, 50};
    ASSERT_TRUE(trend.Update(49, point));
    EXPECT_EQ(boost::tribool(false), trend.IsRising());
  }
  {
    const svc::MovingAverageService::Point point = {
        pt::second_clock::local_time(), 10000, 50};
    ASSERT_FALSE(trend.Update(50, point));
    EXPECT_EQ(boost::tribool(false), trend.IsRising());
  }
  {
    const svc::MovingAverageService::Point point = {
        pt::second_clock::local_time(), 10000, 50};
    ASSERT_TRUE(trend.Update(51, point));
    EXPECT_EQ(boost::tribool(true), trend.IsRising());
  }
  {
    const svc::MovingAverageService::Point point = {
        pt::second_clock::local_time(), 10000, 50};
    ASSERT_TRUE(trend.Update(49, point));
    EXPECT_EQ(boost::tribool(false), trend.IsRising());
  }
}
