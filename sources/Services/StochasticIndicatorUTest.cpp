/**************************************************************************
 *   Created: 2017/01/09 23:49:01
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#include "Prec.hpp"
#include "Core/ContextMock.hpp"
#include "Core/MarketDataSourceMock.hpp"
#include "Core/SecurityMock.hpp"
#include "Services/BarServiceMock.hpp"
#include "StochasticIndicator.hpp"

using namespace testing;
using namespace trdk::Tests;

namespace lib = trdk::Lib;
namespace is = trdk::Services::Indicators;
namespace pt = boost::posix_time;

namespace {

const double source[][6] = {
    //  Open     High      Low    Close             %K             %D
    {1555.25, 1565.55, 1548.19, 1562.50, 0.00000000000, 0.00000000000},
    {1562.50, 1579.58, 1562.50, 1578.78, 0.00000000000, 0.00000000000},
    {1578.78, 1583.00, 1575.80, 1578.79, 0.00000000000, 0.00000000000},
    {1578.93, 1592.64, 1578.93, 1585.16, 0.00000000000, 0.00000000000},
    {1585.16, 1585.78, 1577.56, 1582.24, 0.00000000000, 0.00000000000},
    {1582.34, 1596.65, 1582.34, 1593.61, 0.00000000000, 0.00000000000},
    {1593.58, 1597.57, 1586.50, 1597.57, 0.00000000000, 0.00000000000},
    {1597.55, 1597.55, 1581.28, 1582.70, 0.00000000000, 0.00000000000},
    {1582.77, 1598.60, 1582.77, 1597.59, 0.00000000000, 0.00000000000},
    {1597.60, 1618.46, 1597.60, 1614.42, 0.00000000000, 0.00000000000},
    {1614.40, 1619.77, 1614.21, 1617.50, 0.00000000000, 0.00000000000},
    {1617.55, 1626.03, 1616.64, 1625.96, 0.00000000000, 0.00000000000},
    {1625.95, 1632.78, 1622.70, 1632.69, 0.00000000000, 0.00000000000},
    {1632.69, 1635.01, 1623.09, 1626.67, 90.39391845197, 0.00000000000},
    {1626.69, 1633.70, 1623.71, 1633.70, 98.19335264102, 0.00000000000},
    {1632.10, 1636.00, 1626.74, 1633.77, 96.29568106312, 94.96098405204},
    {1633.75, 1651.10, 1633.75, 1650.34, 98.96654881697, 97.81852750704},
    {1649.13, 1661.49, 1646.68, 1658.78, 96.77111878947, 97.34444955652},
    {1658.07, 1660.51, 1648.60, 1650.47, 86.26106470515, 93.99957743720},
    {1652.45, 1667.47, 1652.45, 1667.47, 100.00000000000, 94.34406116487},
    {1665.71, 1672.84, 1663.52, 1666.29, 92.84622105723, 93.03576192079},
    {1666.20, 1674.93, 1662.67, 1669.16, 93.73914930556, 95.52845678760},
    {1669.39, 1687.18, 1648.86, 1655.35, 64.46751507033, 83.68429514437},
    {1651.62, 1655.50, 1635.53, 1650.51, 49.74647115253, 69.31771184280},
    {1646.67, 1649.78, 1636.88, 1649.60, 46.72526226255, 53.64641616180},
    {1652.63, 1674.21, 1652.63, 1660.06, 57.94044665012, 51.47072668840},
    {1656.57, 1656.57, 1640.05, 1648.36, 39.42892806990, 48.03154566086},
    {1649.14, 1661.91, 1648.61, 1654.41, 48.36930833465, 48.57956101822},
    {1652.13, 1658.99, 1630.74, 1630.74, 6.61813368630, 31.47212336362},
    {1631.71, 1640.42, 1622.72, 1640.42, 27.45888923363, 27.48211041819},
    {1640.73, 1646.53, 1623.62, 1631.38, 13.43468817872, 15.83723703288},
    {1629.05, 1629.31, 1607.09, 1608.90, 2.25995754776, 14.38451165337},
    {1609.29, 1622.56, 1598.23, 1622.56, 27.35244519393, 14.34903030680},
    {1625.27, 1644.40, 1625.27, 1643.38, 50.75885328836, 26.79041867668},
    {1644.67, 1648.69, 1639.26, 1642.81, 50.11804384486, 42.74311410905},
    {1638.64, 1640.13, 1622.92, 1626.13, 31.36593591906, 44.08094435076},
    {1629.94, 1637.71, 1610.92, 1612.52, 18.80758094235, 33.43052023542},
    {1612.15, 1639.25, 1608.07, 1636.36, 50.18425901553, 33.45259195898},
    {1635.52, 1640.80, 1623.96, 1626.73, 37.50987101869, 35.50057032552},
    {1630.64, 1646.50, 1630.34, 1639.04, 64.08605527638, 50.59339510353},
    {1639.77, 1654.19, 1639.77, 1651.81, 84.13944723618, 61.91179117708},
    {1651.83, 1652.45, 1628.91, 1628.93, 50.52666227781, 66.25072159679},
    {1624.62, 1624.62, 1584.32, 1588.19, 5.53885787892, 46.73498913097},
    {1588.62, 1599.19, 1577.70, 1592.43, 19.25741927049, 25.10764647574},
    {1588.77, 1588.77, 1560.33, 1573.09, 13.59471553377, 12.79699756106},
    {1577.52, 1593.79, 1577.09, 1588.03, 29.51203920733, 20.78805800387},
    {1592.27, 1606.83, 1592.27, 1603.26, 45.73833368847, 29.61502947653},
    {1606.44, 1620.07, 1606.44, 1613.20, 56.32857447262, 43.85964912281},
    {1611.12, 1615.94, 1601.06, 1606.28, 48.95589175368, 50.34093330492},
    {1609.78, 1626.61, 1609.78, 1614.96, 58.20370764969, 54.49605795866},
    {1614.29, 1624.26, 1606.77, 1614.08, 57.26614106115, 54.80858015484},
    {1611.48, 1618.97, 1604.57, 1615.41, 58.68314510974, 58.05099794019},
    {1618.65, 1632.07, 1614.71, 1631.89, 76.24121031323, 64.06349882804},
    {1634.20, 1644.68, 1634.20, 1640.46, 85.37183038568, 73.43206193622},
    {1642.89, 1654.18, 1642.89, 1652.32, 98.01811401172, 86.54371823688},
    {1651.56, 1657.92, 1647.66, 1652.62, 94.56911568808, 92.65302002849},
    {1657.41, 1676.63, 1657.41, 1675.02, 98.61564918315, 97.06762629432},
    {1675.26, 1680.19, 1672.33, 1680.19, 100.00000000000, 97.72825495708},
    {1679.59, 1684.51, 1677.89, 1682.50, 98.12884006703, 98.91482975006},
    {1682.70, 1683.73, 1671.84, 1676.26, 91.05594102342, 96.39492703015},
    {1677.91, 1684.75, 1677.91, 1680.91, 95.41163818855, 94.86547309300},
    {1681.05, 1693.12, 1681.05, 1689.37, 95.92656962850, 94.13138294682},
    {1686.15, 1692.09, 1684.08, 1692.09, 98.83681535855, 96.72500772520},
    {1694.41, 1697.61, 1690.67, 1695.53, 97.76440240757, 97.50926246487},
    {1696.63, 1698.78, 1691.13, 1692.39, 93.21728054347, 96.60616610320},
    {1696.06, 1698.38, 1682.57, 1685.94, 84.72701320328, 91.90289871811},
    {1685.21, 1690.94, 1680.07, 1690.25, 86.79157633942, 88.24529002872},
    {1687.31, 1691.85, 1676.03, 1691.65, 87.24279835391, 86.25379596554},
    {1690.32, 1690.92, 1681.86, 1685.33, 73.68935837246, 82.57457768860},
    {1687.92, 1693.19, 1682.42, 1685.96, 69.01136088953, 76.64783920530},
    {1687.76, 1698.43, 1684.94, 1685.73, 51.55902004454, 64.75324643551},
    {1689.42, 1707.85, 1689.42, 1706.87, 97.27853374063, 72.61630489157},
    {1706.10, 1709.67, 1700.68, 1709.67, 100.00000000000, 82.94585126172},
    {1708.01, 1709.24, 1703.55, 1707.14, 92.47919143876, 96.58590839313},
    {1705.79, 1705.79, 1693.29, 1697.37, 63.43638525565, 85.30519223147},
    {1695.30, 1695.30, 1684.91, 1690.91, 44.23305588585, 66.71621086009},
    {1693.35, 1700.18, 1688.38, 1697.48, 63.76337693222, 57.14427269124},
    {1696.10, 1699.42, 1686.02, 1691.42, 45.74910820452, 51.24851367420},
    {1688.37, 1691.49, 1683.35, 1689.47, 39.95243757432, 49.82164090369},
    {1690.65, 1696.81, 1682.62, 1694.16, 53.89417360285, 46.53190646056},
    {1693.88, 1695.52, 1684.83, 1685.39, 27.82401902497, 40.55687673405},
    {1679.61, 1679.61, 1658.59, 1661.32, 5.34455755677, 29.02091672820},
    {1661.22, 1663.60, 1652.61, 1655.83, 5.64318261479, 12.93725306551},
    {1655.25, 1659.18, 1645.84, 1646.06, 0.34466551778, 3.77746856312},
    {1646.81, 1658.92, 1646.08, 1652.35, 10.19896600345, 5.39560471201},
    {1650.66, 1656.99, 1639.43, 1642.80, 4.79783599089, 5.11382250404},
    {1645.03, 1659.55, 1645.03, 1656.96, 25.11101561381, 13.36927253605},
    {1659.92, 1664.85, 1654.81, 1663.50, 36.27185051236, 22.06023403902},
    {1664.29, 1669.51, 1656.02, 1656.78, 28.55967078189, 29.98084563602},
    {1652.54, 1652.54, 1629.05, 1630.48, 2.01040348657, 22.28064159361},
    {1630.25, 1641.18, 1627.47, 1634.96, 10.41000694927, 13.66002707258},
    {1633.50, 1646.41, 1630.88, 1638.17, 15.43120853764, 9.28387299116},
    {1638.89, 1640.08, 1628.05, 1632.97, 7.93192962215, 11.25771503635},
    {1635.95, 1651.35, 1633.41, 1639.77, 18.07494489346, 13.81269435108},
    {1640.72, 1655.72, 1637.41, 1653.08, 49.11775987725, 25.04154479762},
    {1653.28, 1659.17, 1653.07, 1655.08, 65.67554709800, 44.28941728957},
    {1657.44, 1664.83, 1640.62, 1655.17, 65.88962892483, 60.22764530003},
    {1656.85, 1672.40, 1656.85, 1671.71, 98.46427776541, 76.67648459608},
    {1675.11, 1684.09, 1675.11, 1683.99, 99.82338396326, 88.05909688450},
    {1681.04, 1689.13, 1678.70, 1689.13, 100.00000000000, 99.42922057623},
    {1689.21, 1689.97, 1681.96, 1683.42, 89.52000000000, 96.44779465442},
    {1685.04, 1688.73, 1682.22, 1687.99, 96.83200000000, 95.45066666667},
    {1691.70, 1704.95, 1691.70, 1697.60, 90.51368094992, 92.28856031664},
    {1697.73, 1705.52, 1697.73, 1704.76, 99.02626521461, 95.45731538818},
    {1705.74, 1729.44, 1700.35, 1725.52, 96.13374100010, 95.22456238821},
    {1727.34, 1729.86, 1720.20, 1722.34, 92.61369217169, 95.92456612880},
    {1722.44, 1725.23, 1708.89, 1709.91, 79.31570762053, 89.35438026411},
};

class Indicator : public is::Stochastic {
 public:
  explicit Indicator(trdk::Context &context,
                     const std::string &instanceName,
                     const lib::IniSectionRef &conf)
      : Stochastic(context, instanceName, conf) {}

 public:
  using Stochastic::OnServiceDataUpdate;
};
}

TEST(StochasticIndicator, General) {
  const lib::IniString settings(
      "[Section]\n"
      "id = {00000000-0000-0000-0000-000000000000}\n"
      "period_k = 14\n"
      "period_k_smooth = 1\n"
      "period_d = 3\n"
      "log = none");

  Mocks::Context context;
  Mocks::MarketDataSource marketDataSource;
  MockSecurity security("TEST_SCALE2*/USD::FUT");
  Mocks::BarService bars;
  EXPECT_CALL(bars, GetSecurity()).WillRepeatedly(ReturnRef(security));

  Indicator indicator(context, "Test", lib::IniSectionRef(settings, "Section"));

  pt::ptime time = pt::microsec_clock::local_time();
  for (const auto &row : source) {
    time += pt::seconds(123);

    Mocks::BarService::Bar bar;
    bar.time = time;
    bar.openTradePrice = trdk::ScaledPrice(lib::Scale(row[0], 100));
    bar.highTradePrice = trdk::ScaledPrice(lib::Scale(row[1], 100));
    bar.lowTradePrice = trdk::ScaledPrice(lib::Scale(row[2], 100));
    bar.closeTradePrice = trdk::ScaledPrice(lib::Scale(row[3], 100));
    EXPECT_CALL(bars, GetLastBar()).Times(1).WillOnce(Return(bar));

    ASSERT_EQ(!lib::IsZero(row[5]),
              indicator.OnServiceDataUpdate(
                  bars, lib::TimeMeasurement::Milestones()));
    if (lib::IsZero(row[5])) {
      EXPECT_TRUE(indicator.IsEmpty());
      EXPECT_THROW(indicator.GetLastPoint(), Indicator::ValueDoesNotExistError);
      continue;
    }
    ASSERT_FALSE(indicator.IsEmpty());

    const auto &point = indicator.GetLastPoint();

    EXPECT_EQ(time, point.source.time);
    EXPECT_DOUBLE_EQ(row[0], point.source.open);
    EXPECT_DOUBLE_EQ(row[1], point.source.high);
    EXPECT_DOUBLE_EQ(row[2], point.source.low);
    EXPECT_DOUBLE_EQ(row[3], point.source.close);

    EXPECT_NEAR(row[4], point.k, 0.00000000001);
    EXPECT_NEAR(row[5], point.d, 0.00000000001);
  }
}
