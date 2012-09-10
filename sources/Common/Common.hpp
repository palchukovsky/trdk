/**************************************************************************
 *   Created: 2012/07/09 14:52:57
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot
 **************************************************************************/

#pragma once

#include "Common/DisableBoostWarningsBegin.h"
#	include <boost/thread/thread_time.hpp>
#include "Common/DisableBoostWarningsEnd.h"

#include "Common/Interlocking.hpp"
#include "Common/Util.hpp"
#include "Common/Interlocking.hpp"
#include "Common/Defaults.hpp"
#include "Common/UseUnused.hpp"
#include "Common/SignalConnection.hpp"
#include "Common/Foreach.hpp"
#include "Common/IniFile.hpp"
#include "Common/FileSystemChangeNotificator.hpp"
#include "Common/Dll.hpp"
#include "Common/Error.hpp"
#include "Common/Exception.hpp"

#include "Core/Log.hpp"
