/*******************************************************************************
 *   Created: 2017/10/15 21:06:03
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 ******************************************************************************/

#pragma warning(push)
#pragma warning(disable : 4127)
#include <QtWidgets>
#pragma warning(pop)

#include "Common/Common.hpp"
#include "Lib/Common.hpp"
#include "Core/Balances.hpp"
#include "Core/Context.hpp"
#include "Core/MarketDataSource.hpp"
#include "Core/Security.hpp"
#include "Core/Settings.hpp"
#include "Core/Timer.hpp"
#include "Core/TradingSystem.hpp"
#include "Engine/Fwd.hpp"
#include "Lib/Util.hpp"
#include "Fwd.hpp"
#include <boost/algorithm/string/predicate.hpp>
#include <boost/property_tree/json_parser.hpp>
