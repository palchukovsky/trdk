/**************************************************************************
 *   Created: May 20, 2012 6:14:32 PM
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot
 **************************************************************************/

#include "Prec.hpp"
#include "PyApi/Errors.hpp"

using namespace Trader;
using namespace Trader::Log;
using namespace Trader::Lib;

namespace pt = boost::posix_time;

namespace {

	const std::string debugTag = "Debug";
	const std::string infoTag = "Info";
	const std::string warnTag = "Warn";
	const std::string errorTag = "Error";

	const std::string & GetLevelTag(Level level) {
		static_assert(numberOfLevels == 4, "Log levels list changed.");
		switch (level) {
			case LEVEL_DEBUG:
				return debugTag;
			case LEVEL_INFO:
				return infoTag;
			case LEVEL_WARN:
				return warnTag;
			default:
				AssertFail("Unknown log level.");
			case LEVEL_ERROR:
				return errorTag;
		}
	}

	struct State {

		Log::Mutex mutex;
		volatile long isEnabled;

		std::ostream *log;

		State()
				: log(nullptr) {
			Interlocking::Exchange(isEnabled, false);
		}

		void Enable(std::ostream &newLog) {
			Log::Lock lock(mutex);
			const bool isStarted = !log;
			log = &newLog;
			if (isStarted) {
				AppendRecordHead(boost::get_system_time());
				*log << "Started." << std::endl;
			}
			Interlocking::Exchange(isEnabled, true);
		}

		void Disable() throw() {
			Interlocking::Exchange(isEnabled, false);
		}

		static void AppendRecordHead(const pt::ptime &time, std::ostream &os) {
#			ifdef BOOST_WINDOWS
				os << (time + Util::GetEdtDiff()) << " [" << GetCurrentThreadId() << "]: ";
#			else
				os << (time + Util::GetEdtDiff()) << " [" << pthread_self() << "]: ";
#			endif
		}

		void AppendRecordHead(const pt::ptime &time) {
			Assert(log);
			AppendRecordHead(time, *log);
		}

		void AppendLevelTag(Level level, std::ostream &os) {
			os << std::setw(6) << std::left << GetLevelTag(level);
		}

		void AppendRecordHead(
					Level level,
					const pt::ptime &time,
					std::ostream &os) {
			AppendLevelTag(level, os);
			AppendRecordHead(time, os);
		}

		void AppendRecordHead(Level level, const pt::ptime &time) {
			Assert(log);
			AppendLevelTag(level, *log);
			AppendRecordHead(time, *log);
		}


	};

	State events;
	State trading;

}

Log::Mutex & Log::GetEventsMutex() {
	return events.mutex;
}

Log::Mutex & Log::GetTradingMutex() {
	return trading.mutex;
}

bool Log::IsEventsEnabled(Level /*level*/) throw() {
	return events.isEnabled ? true : false;
}

bool Log::IsTradingEnabled() throw() {
	return trading.isEnabled ? true : false;
}

void Log::EnableEvents(std::ostream &log) {
	events.Enable(log);
}

void Log::EnableTrading(std::ostream &log) {
	log.setf(std::ios::left);
	trading.Enable(log);
}

void Log::DisableEvents() throw() {
	events.Disable();
}

void Log::DisableTrading() throw() {
	trading.Disable();
}

//////////////////////////////////////////////////////////////////////////

namespace {
	
	template<typename Str>
	void AppendEventRecordImpl(
				Level level,
				const pt::ptime &time,
				const Str &str) {
		Lock lock(events.mutex);
		events.AppendRecordHead(level, time, std::cout);
		std::cout << str << std::endl;
		if (!events.log) {
			return;
		}
 		events.AppendRecordHead(level, time);
 		*events.log << str << std::endl;
	}

}

void Log::Detail::AppendEventRecordUnsafe(
			Level level,
			const pt::ptime &time,
			const char *str) {
	AppendEventRecordImpl(level, time, str);
}

void Log::Detail::AppendEventRecordUnsafe(
			Level level,
			const pt::ptime &time,
			const std::string &str) {
	AppendEventRecordImpl(level, time, str);
}

//////////////////////////////////////////////////////////////////////////

namespace {
	
	template<typename Str>
	void AppendTradingRecordImpl(
				const pt::ptime &time,
				const std::string &tag,
				const Str &str) {
		Lock lock(trading.mutex);
		if (!trading.log) {
			return;
		}
		trading.AppendRecordHead(time);
		*trading.log << '\t' << tag << '\t' << str << std::endl;
	}
}

void Log::Detail::AppendTradingRecordUnsafe(
			const pt::ptime &time,
			const std::string &tag,
			const char *str) {
	AppendTradingRecordImpl(time, tag, str);
}

void Log::Detail::AppendTradingRecordUnsafe(
			const pt::ptime &time,
			const std::string &tag,
			const std::string &str) {
	AppendTradingRecordImpl(time, tag, str);
}

//////////////////////////////////////////////////////////////////////////

void Log::RegisterUnhandledException(
			const char *function,
			const char *file,
			long line,
			bool tradingLog)
		throw() {

	try {

		struct Logger : private boost::noncopyable {

			std::unique_ptr<boost::format> message;
			const bool tradingLog;

			Logger(bool tradingLog)
					: tradingLog(tradingLog) {
				//...//
			}

			boost::format & CreateStandard() {
				return Create(
					"Unhandled %1% exception caught"
						" in function %3%, file %4%, line %5%: \"%2%\".");
			}

			boost::format & Create(const char *format) {
				Assert(!message);
				message.reset(new boost::format(format));
				return *message;
			}

			~Logger() {
				try {
					Assert(message);
					std::cerr << message->str() << std::endl;
					Log::Error(message->str().c_str());
					if (tradingLog) {
						Log::Trading("assert", message->str().c_str());
					}
				} catch (...) {
					AssertFail("Unexpected exception");
				}
			}

		} logger(tradingLog);

		try {
			throw;
		} catch (const Trader::PyApi::ClientError &ex) {
			logger.Create("Py API error, please check your Python script: \"%1%\".") % ex.what();
		} catch (const Trader::Lib::Exception &ex) {
			logger.CreateStandard() % "LOCAL" % ex.what() % function % file % line;
		} catch (const std::exception &ex) {
			logger.CreateStandard() % "STANDART" % ex.what() % function % file % line;
		} catch (...) {
			logger.CreateStandard() % "UNKNOWN" % "" % function % file % line;
		}

	} catch (...) {
		AssertFail("Unexpected exception");
	}

}
