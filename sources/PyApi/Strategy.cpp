/**************************************************************************
 *   Created: 2012/08/06 13:10:45
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot
 **************************************************************************/

#include "Prec.hpp"
#include "Strategy.hpp"
#include "Core/StrategyPositionReporter.hpp"
#include "Core/PositionBundle.hpp"

using namespace Trader::Lib;
using namespace Trader::PyApi;
using namespace Trader::PyApi::Detail;

namespace py = boost::python;

namespace {

	struct Params : private boost::noncopyable {
		
		const std::string &tag;
		boost::shared_ptr<Trader::Security> &security;
		const Trader::Lib::IniFileSectionRef &ini;
		std::unique_ptr<Script> &script;
	
		explicit Params(
					const std::string &tag,
					boost::shared_ptr<Trader::Security> &security,
					const Trader::Lib::IniFileSectionRef &ini,
					std::unique_ptr<Script> &script)
				: tag(tag),
				security(security),
				ini(ini),
				script(script) {
			//...//
		}

	};

}

#ifdef BOOST_WINDOWS
#	pragma warning(push)
#	pragma warning(disable: 4355)
#endif

Strategy::Strategy(uintmax_t params)
		: Trader::Strategy(
			reinterpret_cast<Params *>(params)->tag,
			reinterpret_cast<Params *>(params)->security),
		Import::Strategy(static_cast<Trader::Strategy &>(*this)),
		m_script(reinterpret_cast<Params *>(params)->script.release()) {
	DoSettingsUpdate(reinterpret_cast<Params *>(params)->ini);
}

#ifdef BOOST_WINDOWS
#	pragma warning(pop)
#endif

Strategy::~Strategy() {
	AssertFail("Strategy::~Strategy");
}

boost::shared_ptr<Trader::Strategy> Strategy::CreateClientInstance(
			const std::string &tag,
			boost::shared_ptr<Trader::Security> security,
			const Trader::Lib::IniFileSectionRef &ini) {
	std::unique_ptr<Script> script(LoadScript(ini));
	auto clientClass = GetPyClass(
		*script,
		ini,
		"Failed to find Trader.Strategy implementation");
	const Params params(tag, security, ini, script);
	try {
		auto pyObject = clientClass(reinterpret_cast<uintmax_t>(&params));
		Strategy &strategy = py::extract<Strategy &>(pyObject);
		strategy.m_self = pyObject;
		const py::str namePyObject = strategy.CallGetNamePyMethod();
		strategy.m_name = py::extract<std::string>(namePyObject);
		Assert(!strategy.m_name.empty());
		return strategy.shared_from_this();
	} catch (const py::error_already_set &) {
		RethrowPythonClientException(
			"Failed to create instance of Trader.Strategy");
		throw;
	}
}

void Strategy::NotifyServiceStart(const Trader::Service &service) {
	try {
// 		const Service *const pyService
// 			= dynamic_cast<const Service *>(&service);
// 		if (pyService) {
// 			PyNotifyServiceStart(*pyService);
// 		} else {
		py::object object;
		if (dynamic_cast<const Trader::Services::BarService *>(&service)) {
			//! @todo fixme
			auto *serviceExport = new Export::Services::BarService(
				*boost::polymorphic_downcast<
						const Trader::Services::BarService *>(
					&service));
			object = py::object(boost::cref(*serviceExport));
		}
		if (object) {
			m_pyCache.push_back(object);
			CallNotifyServiceStartPyMethod(object);
		} else {
			Trader::Strategy::NotifyServiceStart(service);
		}
	} catch (const py::error_already_set &) {
		RethrowPythonClientException(
			"Failed to call method Trader.Strategy.notifyServiceStart");
	}
}

boost::shared_ptr<PositionBandle> Strategy::TryToOpenPositions() {
	boost::shared_ptr<PositionBandle> result;
	try {
		auto pyPosition = CallTryToOpenPositionsPyMethod();
		if (!pyPosition) {
			return result;
		}
		Import::Position &position = py::extract<Import::Position &>(pyPosition);
		position.Bind(pyPosition);
		result.reset(new PositionBandle);
		result->Get().push_back(position.GetPosition().shared_from_this());
	} catch (const py::error_already_set &) {
		RethrowPythonClientException(
			"Failed to call method Trader.Strategy.tryToOpenPositions");
		throw;
	}
	return result;
}

void Strategy::TryToClosePositions(PositionBandle &positions) {
	foreach (auto p, positions.Get()) {
		if (!p->IsOpened() || p->IsClosed() || p->IsCanceled()) {
 			continue;
 		}
		Assert(dynamic_cast<Import::Position *>(&*p));
		Assert(
			dynamic_cast<LongPosition *>(&*p)
			|| dynamic_cast<ShortPosition *>(&*p));
		CallTryToClosePositionsPyMethod(dynamic_cast<Import::Position &>(*p));
	}
}

void Strategy::ReportDecision(const Trader::Position &position) const {
	Log::Trading(
		GetTag().c_str(),
		"%1% %2% open-try cur-ask-bid=%3%/%4% limit-used=%5% qty=%6%",
		position.GetSecurity().GetSymbol(),
		position.GetTypeStr(),
		position.GetSecurity().GetAskPrice(1),
		position.GetSecurity().GetBidPrice(1),
		position.GetSecurity().DescalePrice(position.GetOpenStartPrice()),
		position.GetPlanedQty());
}

std::auto_ptr<Trader::PositionReporter> Strategy::CreatePositionReporter() const {
	typedef Trader::StrategyPositionReporter<Strategy> Reporter;
	std::auto_ptr<Reporter> result(new Reporter);
	result->Init(*this);
	return std::auto_ptr<Trader::PositionReporter>(result);
}

void Strategy::UpdateAlogImplSettings(const IniFileSectionRef &ini) {
	DoSettingsUpdate(ini);
}

void Strategy::DoSettingsUpdate(const IniFileSectionRef &ini) {
	UpdateAlgoSettings(*this, ini);
}

py::str Strategy::CallGetNamePyMethod() const {
	const auto f = get_override("getName");
	if (f) {
		try {
			return f();
		} catch (const py::error_already_set &) {
			RethrowPythonClientException(
				"Failed to call method Trader.Strategy.getName");
			throw;
		}
	} else {
		return Import::Strategy::CallGetNamePyMethod();
	}
}

void Strategy::CallNotifyServiceStartPyMethod(const py::object &service) {
	Assert(service);
	const auto f = get_override("notifyServiceStart");
	if (f) {
		try {
			f(service);
		} catch (const py::error_already_set &) {
			RethrowPythonClientException(
				"Failed to call method Trader.Strategy.notifyServiceStart");
			throw;
		}
	} else {
		Import::Strategy::CallNotifyServiceStartPyMethod(service);
	}
}

py::object Strategy::CallTryToOpenPositionsPyMethod() {
	const auto f = get_override("tryToOpenPositions");
	if (f) {
		try {
			return f();
		} catch (const py::error_already_set &) {
			RethrowPythonClientException(
				"Failed to call method Trader.Strategy.tryToOpenPositions");
			throw;
		}
	} else {
		return Import::Strategy::CallTryToOpenPositionsPyMethod();
	}
}

void Strategy::CallTryToClosePositionsPyMethod(const py::object &positions) {
	Assert(positions);
	const auto f = get_override("tryToClosePositions");
	if (f) {
		try {
			f(positions);
		} catch (const py::error_already_set &) {
			RethrowPythonClientException(
				"Failed to call method Trader.Strategy.tryToClosePositions");
			throw;
		}
	} else {
		Import::Strategy::CallTryToClosePositionsPyMethod(positions);
	}
}

//////////////////////////////////////////////////////////////////////////

#ifdef BOOST_WINDOWS
	boost::shared_ptr<Trader::Strategy> CreateStrategy(
				const std::string &tag,
				boost::shared_ptr<Trader::Security> security,
				const IniFileSectionRef &ini,
				boost::shared_ptr<const Trader::Settings>) {
		return Strategy::CreateClientInstance(tag, security, ini);
	}
#else
	extern "C" boost::shared_ptr<Trader::Strategy> CreateStrategy(
				const std::string &tag,
				boost::shared_ptr<Trader::Security> security,
				const IniFileSectionRef &ini,
				boost::shared_ptr<const Trader::Settings>) {
		return Strategy::CreateClientInstance(tag, security, ini);
	}
#endif

//////////////////////////////////////////////////////////////////////////
