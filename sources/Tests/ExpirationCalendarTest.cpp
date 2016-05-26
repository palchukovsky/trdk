/**************************************************************************
 *   Created: 2016/05/02 13:14:42
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/


#include "Prec.hpp"
#include "Common/ExpirationCalendar.hpp"

namespace lib = trdk::Lib;
namespace fs = boost::filesystem;
namespace pt = boost::posix_time;
namespace gr = boost::gregorian;

////////////////////////////////////////////////////////////////////////////////

namespace {

	fs::path CreateCsvOkSource() {
		
		const fs::path filePath
			= lib::GetExeWorkingDir() / "ExpirationCalendarOkTestSource.csv";
		std::ofstream file(filePath.string().c_str(), std::ios::trunc);
		if (!file) {
			throw std::exception("Filed to open CSV file for test source");
		}
		
		std::vector<boost::tuple<const char *, int>>  source = {
			
			{"XXX1", 20160520},
			
			{"XXX2", 20151110},
			{"XXX2", 20151225},
			{"XXX2", 20160118},
			{"XXX2", 20160219},
			{"XXX2", 20160317},
			{"XXX2", 20160420},
			{"XXX2", 20160521},
			
			{"XXX3", 20160420},
		
		};
		std::random_shuffle(source.begin(), source.end());

		for (const auto &line: source) {
			file
				<< boost::get<0>(line)
				<< ',' << boost::get<1>(line)
				<< std::endl;
		}
	
		return filePath;
	
	}

}

////////////////////////////////////////////////////////////////////////////////

namespace trdk { namespace Tests {

	class ExpirationCalendarTest : public testing::Test {

	protected:

		typedef trdk::Lib::ExpirationCalendar Calendar;
		typedef trdk::Lib::ContractExpiration Expiration;

	public:

		static void SetUpTestCase() {
			m_csvSourceFilePath = CreateCsvOkSource();
		}

	protected:

		static fs::path m_csvSourceFilePath;

	};

	fs::path ExpirationCalendarTest::m_csvSourceFilePath;

} }

////////////////////////////////////////////////////////////////////////////////

namespace trdk { namespace Tests {

	
	TEST_F(ExpirationCalendarTest, Find) {

		Calendar calendar;
		calendar.ReloadCsv(m_csvSourceFilePath);

		// {"XXX1", 20160520},
		{
			auto it = calendar.Find(
				lib::Symbol("XXX1/USD::FUT"),
				pt::ptime(gr::date(2016, gr::Apr, 6)));
			ASSERT_TRUE(it);
			EXPECT_EQ(Expiration::CODE_JUNE, it->code);
			EXPECT_EQ(2016, it->year);
			EXPECT_EQ(gr::date(2016, gr::May, 20), it->expirationDate);
			EXPECT_FALSE(++it);
		}
		{
			auto it = calendar.Find(
				lib::Symbol("XXX1/USD::FUT"),
				pt::ptime(gr::date(2016, gr::May, 20)));
			ASSERT_TRUE(it);
			EXPECT_EQ(Expiration::CODE_JUNE, it->code);
			EXPECT_EQ(2016, it->year);
			EXPECT_EQ(gr::date(2016, gr::May, 20), it->expirationDate);
			EXPECT_FALSE(++it);
		}

		// {"XXX2", 20151110},
		{
			auto it = calendar.Find(
				lib::Symbol("XXX2/USD::FUT"),
				pt::ptime(gr::date(2015, gr::Sep, 25)));
			ASSERT_TRUE(it);
			EXPECT_EQ(Expiration::CODE_DECEMBER, it->code);
			EXPECT_EQ(2015, it->year);
			EXPECT_EQ(gr::date(2015, gr::Nov, 10), it->expirationDate);
			EXPECT_TRUE(++it);
		}

		{
			EXPECT_FALSE(
				calendar.Find(
					lib::Symbol("XXX2/USD::FUT"),
					pt::ptime(gr::date(2016, gr::May, 22))));
		}

		// {"XXX2", 20160219},
		{
			auto it = calendar.Find(
				lib::Symbol("XXX2/USD::FUT"),
				pt::ptime(gr::date(2016, gr::Feb, 1)));
			ASSERT_TRUE(it);
			EXPECT_EQ(Expiration::CODE_MARCH, it->code);
			EXPECT_EQ(2016, it->year);
			EXPECT_EQ(gr::date(2016, gr::Feb, 19), it->expirationDate);
			EXPECT_TRUE(++it);
		}

		{
		
			// {"XXX2", 20151110},
			auto it = calendar.Find(
				lib::Symbol("XXX2/USD::FUT"),
				pt::ptime(gr::date(2015, gr::Sep, 26)));
			ASSERT_TRUE(it);
			EXPECT_EQ(Expiration::CODE_DECEMBER, it->code);
			EXPECT_EQ(2015, it->year);
			EXPECT_EQ(gr::date(2015, gr::Nov, 10), it->expirationDate);
			
			// {"XXX2", 20151225},
			ASSERT_TRUE(++it);
			EXPECT_EQ(Expiration::CODE_JANUARY, it->code);
			EXPECT_EQ(2016, it->year);
			EXPECT_EQ(gr::date(2015, gr::Dec, 25), it->expirationDate);

			// {"XXX2", 20160118},
			ASSERT_TRUE(++it);
			EXPECT_EQ(Expiration::CODE_FEBRUARY, it->code);
			EXPECT_EQ(2016, it->year);
			EXPECT_EQ(gr::date(2016, gr::Jan, 18), it->expirationDate);
		
			// {"XXX2", 20160219},
			ASSERT_TRUE(++it);
			EXPECT_EQ(Expiration::CODE_MARCH, it->code);
			EXPECT_EQ(2016, it->year);
			EXPECT_EQ(gr::date(2016, gr::Feb, 19), it->expirationDate);

			// {"XXX2", 20160317},
			ASSERT_TRUE(++it);
			EXPECT_EQ(Expiration::CODE_APRIL, it->code);
			EXPECT_EQ(2016, it->year);
			EXPECT_EQ(gr::date(2016, gr::Mar, 17), it->expirationDate);

			// {"XXX2", 20160420},
			ASSERT_TRUE(++it);
			EXPECT_EQ(Expiration::CODE_MAY, it->code);
			EXPECT_EQ(2016, it->year);
			EXPECT_EQ(gr::date(2016, gr::Apr, 20), it->expirationDate);

			// {"XXX2", 20160521},
			ASSERT_TRUE(++it);
			EXPECT_EQ(Expiration::CODE_JUNE, it->code);
			EXPECT_EQ(2016, it->year);
			EXPECT_EQ(gr::date(2016, gr::May, 21), it->expirationDate);

			ASSERT_FALSE(++it);

		}

	}

	TEST_F(ExpirationCalendarTest, WrongInstrumnetType) {

		Calendar calendar;
		calendar.ReloadCsv(m_csvSourceFilePath);
		
		for (int i = 0; i < lib::numberOfSecurityTypes; ++i) {
			
			if (
					i == lib::SECURITY_TYPE_FUTURES
					//! @todo not supported yet
					|| i == lib::SECURITY_TYPE_FUTURES_OPTIONS
					//! @todo not supported yet
					|| i == lib::SECURITY_TYPE_FOR_FUTURES_OPTIONS) {
				continue;
			}

			const auto &securityType = reinterpret_cast<lib::SecurityType &>(i);

			const lib::Symbol symbol(
				"XXX1/USD::" + lib::ConvertToString(securityType));
			boost::format error(
				"No info about expiration for \"%1%\" - wrong security type");
			error % symbol;
		
			try {
				calendar.Find(
					symbol,
					pt::ptime(gr::date(2016, gr::May, 22)));
				EXPECT_TRUE(false) << "Exception expected.";
			} catch (const std::exception &ex) {
				EXPECT_EQ(error.str(), ex.what());
			}
		
		}

	}

	TEST_F(ExpirationCalendarTest, ErrorNoFile) {
		fs::path wrongPath = lib::GetExeWorkingDir() / "NoSuchFile.csv";
		boost::format error("Filed to open CSV-file %1% with expiration info");
		error % wrongPath;
		try {
			Calendar().ReloadCsv(wrongPath);
			EXPECT_TRUE(false) << "Exception expected.";
		} catch (const std::exception &ex) {
			EXPECT_EQ(error.str(), ex.what());
		}
	}

	TEST_F(ExpirationCalendarTest, Stat) {

		Calendar calendar;
		calendar.ReloadCsv(m_csvSourceFilePath);
		const Calendar::Stat &stat = calendar.CalcStat();

		EXPECT_EQ(3, stat.numberOfSymbols);
		EXPECT_EQ(9, stat.numberOfExpirations);

	}

	TEST_F(ExpirationCalendarTest, Iterator) {
		
		Calendar calendar;
		calendar.ReloadCsv(m_csvSourceFilePath);

		// {"XXX2", 20151110},
		auto it1 = calendar.Find(
			lib::Symbol("XXX2/USD::FUT"),
			pt::ptime(gr::date(2015, gr::Sep, 26)));
		ASSERT_TRUE(it1);
		EXPECT_EQ(gr::date(2015, gr::Nov, 10), it1->expirationDate);

		EXPECT_EQ(gr::date(2015, gr::Dec, 25), std::next(it1)->expirationDate);
		EXPECT_EQ(
			gr::date(2015, gr::Nov, 10),
			std::prev(std::next(it1))->expirationDate);

		auto it2 = ++it1;
		ASSERT_TRUE(it2);
		EXPECT_EQ(gr::date(2015, gr::Dec, 25), it1->expirationDate);
		EXPECT_EQ(gr::date(2015, gr::Dec, 25), it2->expirationDate);

		ASSERT_TRUE(++it2);
		EXPECT_EQ(gr::date(2015, gr::Dec, 25), it1->expirationDate);
		EXPECT_EQ(gr::date(2016, gr::Jan, 18), it2->expirationDate);

		{
			auto it3 = it2++;
			EXPECT_EQ(gr::date(2015, gr::Dec, 25), it1->expirationDate);
			EXPECT_EQ(gr::date(2016, gr::Feb, 19), it2->expirationDate);
			EXPECT_EQ(gr::date(2016, gr::Jan, 18), it3->expirationDate);
		}

		{
			auto it4 = --it2;
			EXPECT_EQ(gr::date(2015, gr::Dec, 25), it1->expirationDate);
			EXPECT_EQ(gr::date(2016, gr::Jan, 18), it2->expirationDate);
			EXPECT_EQ(gr::date(2016, gr::Jan, 18), it4->expirationDate);
		}

		{
			auto it5 = it2--;
			EXPECT_EQ(gr::date(2015, gr::Dec, 25), it1->expirationDate);
			EXPECT_EQ(gr::date(2015, gr::Dec, 25), it2->expirationDate);
			EXPECT_EQ(gr::date(2016, gr::Jan, 18), it5->expirationDate);
		}

		{
			ASSERT_TRUE(--it2);
			auto it5 = --it2;
			EXPECT_FALSE(it2);
			EXPECT_FALSE(it5);
		}

	}

} }

////////////////////////////////////////////////////////////////////////////////
