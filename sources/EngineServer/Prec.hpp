/**************************************************************************
 *   Created: 2013/02/08 13:59:27
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#pragma once

#include "Common/Assert.hpp"

#include "Engine/Fwd.hpp"
#include "Fwd.hpp"

#include "Common/Common.hpp"

#ifndef TRDK_AUTOBAHN_DISABLED
#	include <autobahn/autobahn.hpp>
#endif

#include <fstream>

#include "Common/DisableBoostWarningsBegin.h"
#	include <boost/unordered_map.hpp>
#	include <boost/filesystem.hpp>
#	include <boost/thread/mutex.hpp>
#	ifndef TRDK_AUTOBAHN_DISABLED
#		include <boost/thread/future.hpp>
#	endif
#	include <boost/algorithm/string.hpp>
#	include <boost/asio.hpp>
#	include <boost/enable_shared_from_this.hpp>
#	include <boost/regex.hpp>
#	include <boost/multi_index_container.hpp>
#	include <boost/multi_index/member.hpp>
#	include <boost/multi_index/hashed_index.hpp>
#	include <boost/multi_index/composite_key.hpp>
#	include <boost/multi_index/ordered_index.hpp>
#	include <boost/uuid/uuid_generators.hpp>
#include "Common/DisableBoostWarningsEnd.h"

#include "Common/Assert.hpp"

#ifdef SendMessage
#	undef SendMessage
#endif
