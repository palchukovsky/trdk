/**************************************************************************
 *   Created: 2012/07/13 20:03:36
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot
 **************************************************************************/

#pragma once

#include "Api.h"

class IniFile;
class Security;

class TRADER_CORE_API Settings : private boost::noncopyable {

public:

	typedef boost::posix_time::ptime Time;

public:

	struct Values {

		Time tradeSessionStartTime;
		Time tradeSessionEndTime;

		boost::uint64_t algoUpdatePeriodMilliseconds;

		size_t algoThreadsCount;

		std::string tsIpAddress;

	};

public:
	
	explicit Settings(
			const IniFile &,
			const std::string &section,
			const Time &now,
			bool isReplayMode);

public:

	void Update(const IniFile &, const std::string &section);

private:

	void UpdateDynamic(const IniFile &, const std::string &section);
	void UpdateStatic(const IniFile &, const std::string &section);

public:

	bool IsReplayMode() const throw() {
		return m_isReplayMode;
	}

	const Time & GetStartTime() const;
	const Time & GetCurrentTradeSessionStartTime() const;
	const Time & GetCurrentTradeSessionEndime() const;

	size_t GetAlgoThreadsCount() const;

	boost::uint64_t GetUpdatePeriodMilliseconds() const;

	boost::uint32_t GetLevel2PeriodSeconds() const;
	bool IsLevel2SnapshotPrintEnabled() const;
	boost::uint16_t GetLevel2SnapshotPrintTimeSeconds() const;

	bool IsValidPrice(const Security &) const;

	const std::string & GetTradeSystemIpAddress() const;

private:

	const Time m_startTime;

	Values m_values;

	volatile long m_level2PeriodSeconds;
	volatile long m_level2SnapshotPrintTimeSeconds;
	volatile LONGLONG m_minPrice;

	const bool m_isReplayMode;

};
