/**************************************************************************
 *   Created: 2013/02/03 11:26:52
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#pragma once

#include "Engine/Context.hpp"
#include "Core/TradingLog.hpp"

namespace trdk { namespace EngineServer {

	class Engine : private boost::noncopyable {

	public:

		explicit Engine(
				const boost::filesystem::path &,
				const trdk::Engine::Context::StateUpdateSlot &contextStateUpdateSlot,
				const boost::function<void(trdk::Engine::Context::Log &)> &onLogStart,
				const boost::unordered_map<std::string, std::string> &params);
		explicit Engine(
				const boost::filesystem::path &,
				const trdk::Engine::Context::StateUpdateSlot &contextStateUpdateSlot,
				DropCopy &dropCopy,
				const boost::function<void(trdk::Engine::Context::Log &)> &onLogStart,
				const boost::unordered_map<std::string, std::string> &params);
		~Engine();

	public:

		trdk::Context & GetContext();

		void Stop(const trdk::StopMode &);

		void ClosePositions();

	private:

		void Run(
				const boost::filesystem::path &,
				const trdk::Context::StateUpdateSlot &,
				DropCopy *dropCopy,
				const boost::function<void(trdk::Engine::Context::Log &)> &onLogStart,
				const boost::unordered_map<std::string, std::string> &params);

		void VerifyModules() const;

	private:

		std::ofstream m_eventsLogFile;
		std::unique_ptr<trdk::Engine::Context::Log> m_eventsLog;

		std::ofstream m_tradingLogFile;
		std::unique_ptr<trdk::Engine::Context::TradingLog> m_tradingLog;

		std::unique_ptr<trdk::Engine::Context> m_context;

	};

} }
