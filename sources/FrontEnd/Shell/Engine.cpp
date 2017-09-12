/*******************************************************************************
 *   Created: 2017/09/10 00:41:29
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 ******************************************************************************/

#include "Prec.hpp"
#include "Engine.hpp"
#include "Engine/Engine.hpp"

using namespace trdk;
using namespace trdk::Lib;
using namespace trdk::FrontEnd::Shell;

namespace sh = trdk::FrontEnd::Shell;
namespace fs = boost::filesystem;
namespace pt = boost::posix_time;
namespace sig = boost::signals2;

class sh::Engine::Implementation : private boost::noncopyable {
 public:
  sh::Engine &m_self;

  const fs::path m_configFilePath;

  std::unique_ptr<trdk::Engine::Engine> m_engine;

  sig::scoped_connection m_engineLogSubscription;

 public:
  explicit Implementation(sh::Engine &self, const fs::path &path)
      : m_self(self), m_configFilePath(path) {
    // Just a smoke-check that config is an engine config:
    IniFile(m_configFilePath).ReadBoolKey("General", "is_replay_mode");
  }

  void OnContextStateChanged(const Context::State &newState,
                             const std::string *updateMessage) {
    static_assert(Context::numberOfStates == 4, "List changed.");
    switch (newState) {
      case Context::STATE_ENGINE_STARTED:
        emit m_self.StateChanged(true);
        if (updateMessage) {
          emit m_self.Message(tr("Engine started: %1")
                                  .arg(QString::fromStdString(*updateMessage)),
                              false);
        }
        break;

      case Context::STATE_DISPATCHER_TASK_STOPPED_GRACEFULLY:
      case Context::STATE_DISPATCHER_TASK_STOPPED_ERROR:
        emit m_self.StateChanged(false);
        break;

      case Context::STATE_STRATEGY_BLOCKED:
        if (!updateMessage) {
          emit m_self.Message(tr("Strategy is blocked by unknown reason."),
                              true);
        } else {
          emit m_self.Message(tr("Strategy is blocked: %1.")
                                  .arg(QString::fromStdString(*updateMessage)),
                              true);
        }
        break;
    }
  }

  void OnEngineNewLogRecord(const char *tag,
                            const pt::ptime &time,
                            const std::string *module,
                            const char *message) {
    if (!std::strcmp(tag, "Debug")) {
      return;
    }
    std::ostringstream oss;
    oss << '[' << tag << "]\t" << time;
    if (module) {
      oss << '[' << *module << "] ";
    } else {
      oss << ' ';
    }
    oss << message;
    emit m_self.LogRecord(QString::fromStdString(oss.str()));
  }
};

sh::Engine::Engine(const fs::path &path, QWidget *parent)
    : QObject(parent),
      m_pimpl(boost::make_unique<Implementation>(*this, path)) {}

sh::Engine::~Engine() {}

const fs::path &sh::Engine::GetConfigFilePath() const {
  return m_pimpl->m_configFilePath;
}

void sh::Engine::Start() {
  if (m_pimpl->m_engine) {
    throw Exception(tr("Engine already started").toLocal8Bit().constData());
  }
  m_pimpl->m_engine = boost::make_unique<trdk::Engine::Engine>(
      GetConfigFilePath(),
      boost::bind(&Implementation::OnContextStateChanged, &*m_pimpl, _1, _2),
      [this](trdk::Engine::Context::Log &log) {
        m_pimpl->m_engineLogSubscription = log.Subscribe(boost::bind(
            &Implementation::OnEngineNewLogRecord, &*m_pimpl, _1, _2, _3, _4));
      },
      boost::unordered_map<std::string, std::string>());
}

void sh::Engine::Stop() {
  if (!m_pimpl->m_engine) {
    throw Exception(tr("Engine is not started").toLocal8Bit().constData());
  }
  m_pimpl->m_engine->Stop(STOP_MODE_IMMEDIATELY);
  m_pimpl->m_engine.reset();
}