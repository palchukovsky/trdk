/**************************************************************************
 *   Created: 2016/04/06 21:04:39
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#include "Prec.hpp"
#include "SecurityType.hpp"
#include "Exception.hpp"

using namespace trdk;
using namespace trdk::Lib;

namespace trdk {
namespace Lib {
const char *ConvertToPch(const SecurityType &);
}
}

namespace {
const std::string stk = ConvertToPch(SECURITY_TYPE_STOCK);
const std::string fut = ConvertToPch(SECURITY_TYPE_FUTURES);
const std::string fop = ConvertToPch(SECURITY_TYPE_FUTURES_OPTIONS);
const std::string for_ = ConvertToPch(SECURITY_TYPE_FOR);
const std::string forFop = ConvertToPch(SECURITY_TYPE_FOR_FUTURES_OPTIONS);
const std::string opt = ConvertToPch(SECURITY_TYPE_OPTIONS);
const std::string index = ConvertToPch(SECURITY_TYPE_INDEX);
const std::string crypto = ConvertToPch(SECURITY_TYPE_CRYPTO);
}

const char *Lib::ConvertToPch(const SecurityType &source) {
  static_assert(numberOfSecurityTypes == 8, "List changed.");
  switch (source) {
    case SECURITY_TYPE_STOCK:
      return "STK";
    case SECURITY_TYPE_FUTURES:
      return "FUT";
    case SECURITY_TYPE_FUTURES_OPTIONS:
      return "FOP";
    case SECURITY_TYPE_FOR:
      return "FOR";
    case SECURITY_TYPE_FOR_FUTURES_OPTIONS:
      return "FORFOP";
    case SECURITY_TYPE_OPTIONS:
      return "OPT";
    case SECURITY_TYPE_INDEX:
      return "INDEX";
    case SECURITY_TYPE_CRYPTO:
      return "CRYPTO";
    default:
      AssertEq(SECURITY_TYPE_STOCK, source);
      throw Exception("Internal error: Unknown security type ID");
  }
}

const std::string &Lib::ConvertToString(const SecurityType &source) {
  static_assert(numberOfSecurityTypes == 8, "List changed.");
  switch (source) {
    case SECURITY_TYPE_STOCK:
      return stk;
    case SECURITY_TYPE_FUTURES:
      return fut;
    case SECURITY_TYPE_FUTURES_OPTIONS:
      return fop;
    case SECURITY_TYPE_FOR:
      return for_;
    case SECURITY_TYPE_FOR_FUTURES_OPTIONS:
      return forFop;
    case SECURITY_TYPE_OPTIONS:
      return opt;
    case SECURITY_TYPE_INDEX:
      return index;
    case SECURITY_TYPE_CRYPTO:
      return crypto;
    default:
      AssertEq(SECURITY_TYPE_STOCK, source);
      throw Exception("Internal error: Unknown security type ID");
  }
}

SecurityType Lib::ConvertSecurityTypeFromString(const std::string &source) {
  static_assert(numberOfSecurityTypes == 8, "List changed.");
  if (boost::iequals(source, stk)) {
    return SECURITY_TYPE_STOCK;
  } else if (boost::iequals(source, fut)) {
    return SECURITY_TYPE_FUTURES;
  } else if (boost::iequals(source, fop)) {
    return SECURITY_TYPE_FUTURES_OPTIONS;
  } else if (boost::iequals(source, for_)) {
    return SECURITY_TYPE_FOR;
  } else if (boost::iequals(source, forFop)) {
    return SECURITY_TYPE_FOR_FUTURES_OPTIONS;
  } else if (boost::iequals(source, opt)) {
    return SECURITY_TYPE_OPTIONS;
  } else if (boost::iequals(source, index)) {
    return SECURITY_TYPE_INDEX;
  } else if (boost::iequals(source, crypto)) {
    return SECURITY_TYPE_CRYPTO;
  } else {
    boost::format message("Security type code \"%1%\" is unknown");
    message % source;
    throw Exception(message.str().c_str());
  }
}
