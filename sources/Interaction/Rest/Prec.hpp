/*******************************************************************************
 *   Created: 2017/10/09 15:09:35
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 ******************************************************************************/

#pragma once

#include "Fwd.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/circular_buffer.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/unordered_set.hpp>
#include <Poco/Net/AcceptCertificateHandler.h>
#include <Poco/Net/HTTPMessage.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/Net/HTTPSClientSession.h>
#include <Poco/Net/InvalidCertificateHandler.h>
#include <Poco/Net/NetException.h>
#include <Poco/Net/PrivateKeyPassphraseHandler.h>
#include <Poco/Net/SSLManager.h>
#include <Poco/StreamCopier.h>
#include <Poco/URI.h>

#include "Common/Common.hpp"
#include "Common/Crypto.hpp"

#include "TradingLib/Util.hpp"
#include "Core/BalancesContainer.hpp"
#include "Core/Context.hpp"
#include "Core/EventsLog.hpp"
#include "Core/MarketDataSource.hpp"
#include "Core/Security.hpp"
#include "Core/Settings.hpp"
#include "Core/Timer.hpp"
#include "Core/TradingLog.hpp"
#include "Core/TradingSystem.hpp"
#include "Core/TransactionContext.hpp"

#include "App.hpp"