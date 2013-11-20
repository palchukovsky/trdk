/**************************************************************************
 *   Created: 2013/11/12 06:55:47
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#include "Prec.hpp"
#include "Services/MovingAverageService.hpp"
#include "Context.hpp"

namespace lib = trdk::Lib;
namespace svc = trdk::Services;

////////////////////////////////////////////////////////////////////////////////

namespace {

	const double source[110][3] = {
		/*	Val			SMA				EMA	 */
		{	1642.81	,	0.00	,	0.00	},
		{	1626.13	,	0.00	,	0.00	},
		{	1612.52	,	0.00	,	0.00	},
		{	1636.36	,	0.00	,	0.00	},
		{	1626.73	,	0.00	,	0.00	},
		{	1639.04	,	0.00	,	0.00	},
		{	1651.81	,	0.00	,	0.00	},
		{	1628.93	,	0.00	,	0.00	},
		{	1588.19	,	0.00	,	0.00	},
		{	1592.43	,	1624.50	,	1618.67	},
		{	1573.09	,	1617.52	,	1610.38	},
		{	1588.03	,	1613.71	,	1606.32	},
		{	1603.26	,	1612.79	,	1605.76	},
		{	1613.2	,	1610.47	,	1607.11	},
		{	1606.28	,	1608.43	,	1606.96	},
		{	1614.96	,	1606.02	,	1608.42	},
		{	1614.08	,	1602.25	,	1609.45	},
		{	1615.41	,	1600.89	,	1610.53	},
		{	1631.89	,	1605.26	,	1614.41	},
		{	1640.46	,	1610.07	,	1619.15	},
		{	1652.32	,	1617.99	,	1625.18	},
		{	1652.62	,	1624.45	,	1630.17	},
		{	1675.02	,	1631.62	,	1638.32	},
		{	1680.19	,	1638.32	,	1645.94	},
		{	1682.5	,	1645.95	,	1652.58	},
		{	0	,	0.00	,	0.00	},
		{	1676.26	,	1652.08	,	1656.89	},
		{	1680.91	,	1658.76	,	1661.26	},
		{	1689.37	,	1666.15	,	1666.37	},
		{	1692.09	,	1672.17	,	1671.04	},
		{	1695.53	,	1677.68	,	1675.50	},
		{	1692.39	,	1681.69	,	1678.57	},
		{	1685.94	,	1685.02	,	1679.91	},
		{	1690.25	,	1686.54	,	1681.79	},
		{	1691.65	,	1687.69	,	1683.58	},
		{	1685.33	,	1687.97	,	1683.90	},
		{	1685.96	,	1688.94	,	1684.27	},
		{	1685.73	,	1689.42	,	1684.54	},
		{	1706.87	,	1691.17	,	1688.60	},
		{	1709.67	,	1692.93	,	1692.43	},
		{	1707.14	,	1694.09	,	1695.10	},
		{	1697.37	,	1694.59	,	1695.52	},
		{	1690.91	,	1695.09	,	1694.68	},
		{	1697.48	,	1695.81	,	1695.19	},
		{	1691.42	,	1695.79	,	1694.50	},
		{	1689.47	,	1696.20	,	1693.59	},
		{	1694.16	,	1697.02	,	1693.69	},
		{	1685.39	,	1696.99	,	1692.18	},
		{	1661.32	,	1692.43	,	1686.57	},
		{	1655.83	,	1687.05	,	1680.98	},
		{	1646.06	,	1680.94	,	1674.63	},
		{	1652.35	,	1676.44	,	1670.58	},
		{	1642.8	,	1671.63	,	1665.53	},
		{	1656.96	,	1667.58	,	1663.97	},
		{	1663.5	,	1664.78	,	1663.89	},
		{	1656.78	,	1661.52	,	1662.59	},
		{	1630.48	,	1655.15	,	1656.76	},
		{	1634.96	,	1650.10	,	1652.79	},
		{	1638.17	,	1647.79	,	1650.13	},
		{	1632.97	,	1645.50	,	1647.01	},
		{	1639.77	,	1644.87	,	1645.70	},
		{	1653.08	,	1644.95	,	1647.04	},
		{	1655.08	,	1646.18	,	1648.50	},
		{	1655.17	,	1646.00	,	1649.71	},
		{	1671.71	,	1646.82	,	1653.71	},
		{	1683.99	,	1649.54	,	1659.22	},
		{	1689.13	,	1655.40	,	1664.66	},
		{	1683.42	,	1660.25	,	1668.07	},
		{	1687.99	,	1665.23	,	1671.69	},
		{	1697.6	,	1671.69	,	1676.40	},
		{	1704.76	,	1678.19	,	1681.56	},
		{	1725.52	,	1685.44	,	1689.55	},
		{	1722.34	,	1692.16	,	1695.51	},
		{	1709.91	,	1697.64	,	1698.13	},
		{	1701.84	,	1700.65	,	1698.80	},
		{	1697.42	,	1701.99	,	1698.55	},
		{	1692.77	,	1702.36	,	1697.50	},
		{	1698.67	,	1703.88	,	1697.71	},
		{	1691.75	,	1704.26	,	1696.63	},
		{	1681.55	,	1702.65	,	1693.89	},
		{	1695	,	1701.68	,	1694.09	},
		{	1693.87	,	1698.51	,	1694.05	},
		{	1678.66	,	1694.14	,	1691.25	},
		{	1690.5	,	1692.20	,	1691.12	},
		{	1676.12	,	1689.63	,	1688.39	},
		{	1655.45	,	1685.43	,	1682.40	},
		{	1656.4	,	1681.80	,	1677.67	},
		{	1692.56	,	1681.19	,	1680.38	},
		{	1703.2	,	1682.33	,	1684.53	},
		{	1710.14	,	1685.19	,	1689.19	},
		{	1698.06	,	1685.50	,	1690.80	},
		{	1721.54	,	1688.26	,	1696.39	},
		{	1733.15	,	1693.71	,	1703.07	},
		{	1744.5	,	1699.11	,	1710.60	},
		{	1744.66	,	1705.97	,	1716.80	},
		{	1754.67	,	1715.89	,	1723.68	},
		{	1746.38	,	1724.89	,	1727.81	},
		{	1752.07	,	1730.84	,	1732.22	},
		{	1759.77	,	1736.49	,	1737.23	},
		{	1762.11	,	1741.69	,	1741.75	},
		{	1771.95	,	1749.08	,	1747.24	},
		{	1763.31	,	1753.26	,	1750.16	},
		{	1756.54	,	1755.60	,	1751.32	},
		{	1761.64	,	1757.31	,	1753.20	},
		{	1767.93	,	1759.64	,	1755.88	},
		{	1762.97	,	1760.47	,	1757.17	},
		{	1770.49	,	1762.88	,	1759.59	},
		{	1747.15	,	1762.39	,	1757.33	},
		{	1770.61	,	1763.47	,	1759.74	},
		{	1771.89	,	1764.45	,	1761.95	},
	};

	struct SmaTrait {
		static const char * GetType() {
			return "simple";
		}
		static size_t GetColumn() {
			return 1;
		}
	};

	struct EmaTrait {
		static const char * GetType() {
			return "exponential";
		}
		static size_t GetColumn() {
			return 2;
		}
	};

}

////////////////////////////////////////////////////////////////////////////////

namespace trdk { namespace Testing {

	template<typename Policy>
	class MovingAverageServiceTypedTest : public testing::Test {
		//...//
	};
	TYPED_TEST_CASE_P(MovingAverageServiceTypedTest);

} }

////////////////////////////////////////////////////////////////////////////////

using namespace trdk::Testing;

TYPED_TEST_P(MovingAverageServiceTypedTest, RealTimeWithHistory) {
	
	typedef typename TypeParam Policy;

	boost::format settingsString(
		"[Section]\n"
			"type = %1%\n"
			"history = yes\n"
			"period = 10");
	settingsString % Policy::GetType();
	const lib::IniString settings(settingsString.str());
	Context context;

	svc::MovingAverageService service(
		context,
		"Tag",
		lib::IniSectionRef(settings, "Section"));

	for (size_t i = 0; i < _countof(source); ++i) {
		svc::BarService::Bar bar;
		bar.closeTradePrice = lib::Scale(source[i][0], 100);
		ASSERT_EQ(
				!lib::IsZero(source[i][Policy::GetColumn()]),
				service.OnNewBar(bar))
			<< "i = " << i << ";"
			<< " bar.closeTradePrice = " << bar.closeTradePrice << ";";
		if (!lib::IsZero(source[i][Policy::GetColumn()])) {
			EXPECT_EQ(
					source[i][Policy::GetColumn()],
					lib::Descale(service.GetLastPoint().value, 100))
				<< "i = " << i << ";"
				<< " bar.closeTradePrice = " << bar.closeTradePrice << ";"
				<< " service.GetLastPoint().value = "
					<< service.GetLastPoint().value << ";";
		}
	}

	ASSERT_NO_THROW(service.GetHistorySize());
	ASSERT_EQ(_countof(source) - 10, service.GetHistorySize());
	EXPECT_THROW(
		service.GetHistoryPoint(_countof(source) - 10),
		svc::MovingAverageService::ValueDoesNotExistError);
	EXPECT_THROW(
		service.GetHistoryPointByReversedIndex(_countof(source) - 10),
		svc::MovingAverageService::ValueDoesNotExistError);

	size_t offset = 9;
	for (size_t i = 0; i < service.GetHistorySize(); ++i) {
		auto pos = i + offset;
		if (trdk::Lib::IsZero(source[pos][Policy::GetColumn()])) {
			++offset;
			++pos;
		}
		ASSERT_EQ(
				source[pos][Policy::GetColumn()],
				lib::Descale(service.GetHistoryPoint(i).value, 100))
			<< "i = " << i << ";"
			<< " pos = " << pos << ";";
	}
	offset = 0;
	for (size_t i = 0; i < service.GetHistorySize(); ++i) {
		auto pos = _countof(source) - 1 - i - offset;
		if (trdk::Lib::IsZero(source[pos][Policy::GetColumn()])) {
			++offset;
			--pos;
		}
		ASSERT_EQ(
				source[pos][Policy::GetColumn()],
				lib::Descale(
					service.GetHistoryPointByReversedIndex(i).value,
					100))
			<< "i = " << i << ";"
			<< " pos = " << pos << ";";
	}

}

TYPED_TEST_P(MovingAverageServiceTypedTest, RealTimeWithoutHistory) {

	typedef typename TypeParam Policy;

	boost::format settingsString(
		"[Section]\n"
			"type = %1%\n"
			"period = 10");
	settingsString % Policy::GetType();
	const lib::IniString settings(settingsString.str());
	Context context;

	svc::MovingAverageService service(
		context,
		"Tag",
		lib::IniSectionRef(settings, "Section"));

	for (size_t i = 0; i < _countof(source); ++i) {
		svc::BarService::Bar bar;
		bar.closeTradePrice = lib::Scale(source[i][0], 100);
		ASSERT_EQ(
				!lib::IsZero(source[i][Policy::GetColumn()]),
				service.OnNewBar(bar))
			<< "i = " << i << ";"
			<< " bar.closeTradePrice = " << bar.closeTradePrice << ";";
		if (!lib::IsZero(source[i][Policy::GetColumn()])) {
			EXPECT_EQ(
					source[i][Policy::GetColumn()],
					lib::Descale(service.GetLastPoint().value, 100))
				<< "i = " << i << ";"
				<< " bar.closeTradePrice = " << bar.closeTradePrice << ";"
				<< " service.GetLastPoint().value = "
					<< service.GetLastPoint().value << ";";
		}
	}

	EXPECT_THROW(
		service.GetHistorySize(),
		svc::MovingAverageService::HasNotHistory);
	EXPECT_THROW(
		service.GetHistoryPoint(0),
		svc::MovingAverageService::HasNotHistory);
	EXPECT_THROW(
		service.GetHistoryPointByReversedIndex(0),
		svc::MovingAverageService::HasNotHistory);

}

////////////////////////////////////////////////////////////////////////////////


REGISTER_TYPED_TEST_CASE_P(
	MovingAverageServiceTypedTest,
	RealTimeWithHistory,
	RealTimeWithoutHistory);

namespace trdk { namespace Testing {

	typedef ::testing::Types<SmaTrait, EmaTrait>
		MovingAverageServiceTestPolicies;

	INSTANTIATE_TYPED_TEST_CASE_P(
		MovingAverageService,
		MovingAverageServiceTypedTest,
		MovingAverageServiceTestPolicies);

} }

////////////////////////////////////////////////////////////////////////////////
