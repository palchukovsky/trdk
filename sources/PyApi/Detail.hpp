/**************************************************************************
 *   Created: 2012/11/05 14:20:32
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#pragma once

#include "Core/SettingsReport.hpp"
#include "Errors.hpp"

namespace trdk { namespace PyApi { namespace Detail {

	inline void RethrowPythonClientException(const char *action) {

		boost::format exceptionWhat("%1%:%2%");

		try {

			namespace py = boost::python;

			struct Cleanup {
				~Cleanup() {
					PyErr_Clear();
				}
			};

			PyObject *exc;
			PyObject *val;
			PyObject *tb;
			PyErr_Fetch(&exc, &val, &tb);
			PyErr_NormalizeException(&exc, &val, &tb);
			py::handle<> hexc(exc);
			py::handle<> hval(py::allow_null(val));
			py::handle<> htb(py::allow_null(tb));
			std::ostringstream errorText;
			if (!hval) {
				std::string text = py::extract<std::string>(py::str(hexc));
				boost::trim(text);
				errorText << " \"" << text << "\"";
			} else {
				py::object traceback(py::import("traceback"));
				py::object formatException(
					traceback.attr("format_exception"));
				py::list formattedList(formatException(hexc, hval, htb));
				for(	auto count = 0
						; count < py::len(formattedList)
						; ++count) {
					errorText
						<< std::endl
						<< std::string(
							py::extract<std::string>(
								formattedList[count].slice(0, -1)));
				}
				errorText << std::endl;
			}

			exceptionWhat % action % errorText.str();

		} catch (...) {
			AssertFailNoException();
			throw;
		}

		throw ScriptError(exceptionWhat.str().c_str());

	}

	inline std::string GetModuleClassName(
				const trdk::Lib::IniFileSectionRef &conf) {
		try {
			return conf.ReadKey("class");
		} catch (const trdk::Lib::IniFile::KeyNotExistsError &) {
			const auto dotPos = conf.GetName().find('.');
			AssertNe(std::string::npos, dotPos);
			if (std::string::npos == dotPos) {
				return conf.GetName();
			}
			return conf.GetName().substr(dotPos + 1);
		}
	}

	template<typename Module>
	void UpdateAlgoSettings(
				Module &algo,
				const trdk::Lib::IniFileSectionRef &conf) {
		SettingsReport::Report report;
		SettingsReport::Append("tag", algo.GetTag(), report);
		SettingsReport::Append(
			"script_file_path",
			conf.ReadKey("script_file_path"),
			report);
		SettingsReport::Append("class", GetModuleClassName(conf), report);
		algo.ReportSettings(report);
	}

	//////////////////////////////////////////////////////////////////////////

} } }
