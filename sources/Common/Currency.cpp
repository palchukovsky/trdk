/**************************************************************************
 *   Created: 2014/08/17 16:50:35
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#include "Prec.hpp"
#include "Currency.hpp"
#include "Exception.hpp"

using namespace trdk;
using namespace trdk::Lib;

namespace trdk {
namespace Lib {
const char *ConvertToIsoPch(const Currency &);
}
}  // namespace trdk

namespace {
namespace Iso4217 {
const std::string usd = ConvertToIsoPch(CURRENCY_USD);
const std::string eur = ConvertToIsoPch(CURRENCY_EUR);
const std::string jpy = ConvertToIsoPch(CURRENCY_JPY);
const std::string rub = ConvertToIsoPch(CURRENCY_RUB);
const std::string gbp = ConvertToIsoPch(CURRENCY_GBP);
const std::string chf = ConvertToIsoPch(CURRENCY_CHF);
const std::string aud = ConvertToIsoPch(CURRENCY_AUD);
const std::string inr = ConvertToIsoPch(CURRENCY_INR);
const std::string btc = ConvertToIsoPch(CURRENCY_BTC);
const std::string eth = ConvertToIsoPch(CURRENCY_ETH);
const std::string ltc = ConvertToIsoPch(CURRENCY_LTC);
const std::string usdt = ConvertToIsoPch(CURRENCY_USDT);
const std::string eurt = ConvertToIsoPch(CURRENCY_EURT);
const std::string xrp = ConvertToIsoPch(CURRENCY_XRP);
}  // namespace Iso4217
}  // namespace

const std::string &Lib::ConvertToIso(const Currency &currency) {
  using namespace Iso4217;
  static_assert(numberOfCurrencies == 14, "Currency list changed.");
  switch (currency) {
    case CURRENCY_USD:
      return usd;
    case CURRENCY_EUR:
      return eur;
    case CURRENCY_JPY:
      return jpy;
    case CURRENCY_RUB:
      return rub;
    case CURRENCY_GBP:
      return gbp;
    case CURRENCY_CHF:
      return chf;
    case CURRENCY_AUD:
      return aud;
    case CURRENCY_INR:
      return inr;
    case CURRENCY_BTC:
      return btc;
    case CURRENCY_ETH:
      return eth;
    case CURRENCY_LTC:
      return ltc;
    case CURRENCY_USDT:
      return usdt;
    case CURRENCY_EURT:
      return eurt;
    case CURRENCY_XRP:
      return xrp;
    default:
      AssertEq(CURRENCY_USD, currency);
      throw Exception("Internal error: Unknown currency ID");
  }
}

const char *Lib::ConvertToIsoPch(const Currency &currency) {
  using namespace Iso4217;
  static_assert(numberOfCurrencies == 14, "Currency list changed.");
  switch (currency) {
    case CURRENCY_USD:
      return "USD";
    case CURRENCY_EUR:
      return "EUR";
    case CURRENCY_JPY:
      return "JPY";
    case CURRENCY_RUB:
      return "RUB";
    case CURRENCY_GBP:
      return "GBP";
    case CURRENCY_CHF:
      return "CHF";
    case CURRENCY_AUD:
      return "AUD";
    case CURRENCY_INR:
      return "INR";
    case CURRENCY_BTC:
      return "BTC";
    case CURRENCY_ETH:
      return "ETH";
    case CURRENCY_LTC:
      return "LTC";
    case CURRENCY_USDT:
      return "USDT";
    case CURRENCY_EURT:
      return "EURT";
    case CURRENCY_XRP:
      return "XRP";
    default:
      AssertEq(CURRENCY_USD, currency);
      throw Exception("Internal error: Unknown currency ID");
  }
}

Currency Lib::ConvertCurrencyFromIso(const std::string &code) {
  using namespace Iso4217;
  static_assert(numberOfCurrencies == 14, "Currency list changed.");
  if (boost::iequals(code, usd)) {
    return CURRENCY_USD;
  } else if (boost::iequals(code, eur)) {
    return CURRENCY_EUR;
  } else if (boost::iequals(code, jpy)) {
    return CURRENCY_JPY;
  } else if (boost::iequals(code, rub)) {
    return CURRENCY_RUB;
  } else if (boost::iequals(code, gbp)) {
    return CURRENCY_GBP;
  } else if (boost::iequals(code, chf)) {
    return CURRENCY_CHF;
  } else if (boost::iequals(code, aud)) {
    return CURRENCY_AUD;
  } else if (boost::iequals(code, inr)) {
    return CURRENCY_INR;
  } else if (boost::iequals(code, btc)) {
    return CURRENCY_BTC;
  } else if (boost::iequals(code, eth)) {
    return CURRENCY_ETH;
  } else if (boost::iequals(code, ltc)) {
    return CURRENCY_LTC;
  } else if (boost::iequals(code, usdt)) {
    return CURRENCY_USDT;
  } else if (boost::iequals(code, eurt)) {
    return CURRENCY_EURT;
  } else if (boost::iequals(code, xrp)) {
    return CURRENCY_XRP;
  } else {
    boost::format message("Currency code \"%1%\" is unknown");
    message % code;
    throw Exception(message.str().c_str());
  }
}
