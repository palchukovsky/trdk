/**************************************************************************
 *   Created: 2013/02/02 21:02:42
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#include "Prec.hpp"
#include "Engine/Engine.hpp"

using namespace trdk::Lib;
using namespace trdk::EngineServer;

namespace fs = boost::filesystem;
namespace pt = boost::posix_time;

////////////////////////////////////////////////////////////////////////////////

namespace trdk {
namespace EngineServer {

namespace CommandLine {

namespace Commands {

const char *const debug = "debug";
const char *const debugShort = "d";

const char *const standalone = "standalone";
const char *const standaloneShort = "s";

const char *const version = "version";
const char *const versionShort = "v";

const char *const help = "help";
const char *const helpEx = "--help";
const char *const helpShort = "h";
const char *const helpShortEx = "-h";
}  // namespace Commands

namespace Options {
const char *const startDelay = "--start_delay";
}
}  // namespace CommandLine
}  // namespace EngineServer
}  // namespace trdk

//////////////////////////////////////////////////////////////////////////

namespace {

fs::path GetConfigFilePath(const char *inputValue) {
  auto result = Normalize(GetExeWorkingDir() / inputValue);
  if (fs::is_directory(result)) {
    result /= "config.json";
  } else if (!result.has_extension()) {
    result.replace_extension("json");
  }
  return result;
}
}  // namespace

//////////////////////////////////////////////////////////////////////////

namespace {

bool RunService(int argc, const char *argv[]) {
  if (argc < 3 || !strlen(argv[2])) {
    std::cerr << "No configuration file specified." << std::endl;
    return false;
  }

  pt::time_duration startDelay;
  if (argc > 3) {
    auto i = 3;
    if (strcmp(argv[i], CommandLine::Options::startDelay)) {
      std::cerr << "Unknown option \"" << argv[i] << "\"." << std::endl;
      return false;
    }
    const auto valueArgIndex = i + 1;
    if (valueArgIndex >= argc) {
      std::cerr << "Option " << CommandLine::Options::startDelay
                << " has no value." << std::endl;
      return false;
    }
    try {
      const auto value =
          boost::lexical_cast<unsigned short>(argv[valueArgIndex]);
      startDelay = pt::seconds(value);
    } catch (const boost::bad_lexical_cast &ex) {
      std::cerr << "Failed to read " << CommandLine::Options::startDelay
                << " value \"" << argv[valueArgIndex] << "\":"
                << " \"" << ex.what() << "\"." << std::endl;
      return false;
    }
  }

  std::cerr << "Service mode is not supported." << std::endl;

  return false;
}

bool DebugStrategy(int argc, const char *argv[]) {
  if (argc < 3 || !strlen(argv[2])) {
    std::cerr << "No configuration file specified." << std::endl;
    return false;
  }

  std::unique_ptr<trdk::Engine::Engine> engine;
  bool result = true;

  boost::mutex stateMutex;
  boost::condition_variable stateCondition;
  boost::optional<trdk::Context::State> state;

  try {
    engine = boost::make_unique<trdk::Engine::Engine>(
        GetConfigFilePath(argv[2]), "logs",
        [&](const trdk::Context::State &newState, const std::string *) {
          {
            const boost::mutex::scoped_lock lock(stateMutex);
            state = newState;
          }
          stateCondition.notify_all();
        },
        [](const std::string &) {}, [](const std::string &) { return false; },
        [](trdk::Engine::Context::Log &log) { log.EnableStdOut(); });
  } catch (const Exception &ex) {
    std::cerr << "Failed to start engine: \"" << ex << "\"." << std::endl;
    result = false;
  }

  if (result) {
    boost::mutex::scoped_lock lock(stateMutex);
    stateCondition.wait(lock, [&]() {
      static_assert(trdk::Context::numberOfStates == 4, "List changed.");
      return state && *state != trdk::Context::STATE_ENGINE_STARTED;
    });
  }

  return result;
}

bool ShowVersion(int /*argc*/, const char * /*argv*/ []) {
  std::cout << TRDK_NAME " " TRDK_BUILD_IDENTITY << std::endl;
  return true;
}

bool ShowHelp(int argc, const char *argv[]) {
  using namespace trdk::EngineServer::CommandLine::Commands;
  using namespace trdk::EngineServer::CommandLine::Options;

  std::cout << std::endl;

  if (!ShowVersion(argc, argv)) {
    return false;
  }

  std::cout
      << std::endl
      << "Usage: " << argv[0]
      << " command command-args  [ --options [options-args] ]" << std::endl
      << std::endl
      << "Command:" << std::endl
      << std::endl
      << "    " << standalone << " (or " << standaloneShort
      << ")"
         " \"path to configuration file or path to config.json directory\""
      << std::endl
      << std::endl
      << "    " << debug << " (or " << debugShort << ")"
      << " \"path to configuration file or path to config.json directory\""
      << std::endl
      << std::endl
      << "    " << help << " (or " << helpShort << ")" << std::endl
      << std::endl
      << "Options:" << std::endl
      << std::endl
      << "    " << startDelay << " \"number of seconds to wait before"
      << " service start\"" << std::endl
      << std::endl
      << std::endl;

  return true;
}
}  // namespace

//////////////////////////////////////////////////////////////////////////

#if !defined(BOOST_WINDOWS)
namespace {

void *WaitOsSignal(void *arg) {
  try {
    for (;;) {
      sigset_t signalSet;
      int signalNumber = 0;
      sigwait(&signalSet, &signalNumber);
      std::cout << "OS Signal " << signalNumber << " received and suppressed."
                << std::endl;
    }
  } catch (...) {
    AssertFailNoException();
    throw;
  }
}

void InstallOsSignalsHandler() {
  // Start by masking suppressed signals at the primary thread. All other
  // threads inherit this signal mask and therefore will have the same
  // signals suppressed.
  {
    sigset_t signalSet;
    sigemptyset(&signalSet);
    sigaddset(&signalSet, SIGPIPE);
    // sigaddset(&signalSet, SIGXXX1);
    // sigaddset(&signalSet, SIGXXX2);
  }

  // Install the signal mask against primary thread.
  {
    sigset_t signalSet;
    const auto status = pthread_sigmask(SIG_BLOCK, &signalSet, NULL);
    if (status != 0) {
      std::cerr << "Failed to install OS Signal Mask"
                << " (error: " << status << ")." << std::endl;
      exit(1);
    }
  }

  // Create the sigwait thread.
  {
    pthread_t signalThreadId;
    const auto status =
        pthread_create(&signalThreadId, NULL, WaitOsSignal, NULL);
    if (status != 0) {
      std::cerr << "Failed to install create thread for OS Signal Wait"
                << " (error: " << status << ")." << std::endl;
      exit(1);
    }
  }
}
}  // namespace
#else
namespace {

void InstallOsSignalsHandler() {}
}  // namespace
#endif

//////////////////////////////////////////////////////////////////////////

int main(int argc, const char *argv[]) {
  int result = 3;

  try {
    InstallOsSignalsHandler();

    boost::function<bool(int, const char *[])> func;
    if (argc > 1) {
      using namespace trdk::EngineServer::CommandLine::Commands;
      using namespace trdk::EngineServer::CommandLine::Options;

      Verify(--result >= 0);

      boost::unordered_map<std::string, decltype(func)> commands;

      Verify(commands.emplace(std::make_pair(standalone, &RunService)).second);
      Verify(commands.emplace(std::make_pair(standaloneShort, &RunService))
                 .second);

      Verify(commands.emplace(std::make_pair(debug, &DebugStrategy)).second);
      Verify(
          commands.emplace(std::make_pair(debugShort, &DebugStrategy)).second);

      Verify(commands.emplace(std::make_pair(version, &ShowVersion)).second);
      Verify(
          commands.emplace(std::make_pair(versionShort, &ShowVersion)).second);

      Verify(commands.emplace(std::make_pair(help, &ShowHelp)).second);
      Verify(commands.emplace(std::make_pair(helpShort, &ShowHelp)).second);
      Verify(commands.emplace(std::make_pair(helpEx, &ShowHelp)).second);
      Verify(commands.emplace(std::make_pair(helpShortEx, &ShowHelp)).second);

      const auto &commandPos = commands.find(argv[1]);
      if (commandPos != commands.cend()) {
        Verify(--result >= 0);
        func = commandPos->second;
      } else {
        std::cerr << "No command specified." << std::endl;
      }
    }

    if (func) {
      if (func(argc, argv)) {
        Verify(--result >= 0);
      }
    }

  } catch (...) {
    AssertFailNoException();
  }

  return result;
}
