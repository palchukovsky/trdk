/*******************************************************************************
 *   Created: 2017/10/16 22:11:08
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 ******************************************************************************/

#pragma once

#include "Prec.hpp"
#include "App.hpp"
#include "PollingTask.hpp"
#include "Request.hpp"
#include "Security.hpp"

using namespace trdk;
using namespace trdk::Lib;
using namespace trdk::Lib::TimeMeasurement;
using namespace trdk::Interaction;
using namespace trdk::Interaction::Rest;

namespace pc = Poco;
namespace net = pc::Net;
namespace pt = boost::posix_time;
namespace ptr = boost::property_tree;

////////////////////////////////////////////////////////////////////////////////

namespace {

struct Settings {
  std::string apiKey;
  std::string apiSecret;
  pt::time_duration pollingInterval;

  explicit Settings(const IniSectionRef &conf, ModuleEventsLog &log)
      : apiKey(conf.ReadKey("api_key")),
        apiSecret(conf.ReadKey("api_secret")),
        pollingInterval(pt::milliseconds(
            conf.ReadTypedKey<long>("polling_interval_milliseconds"))) {
    Log(log);
    Validate();
  }

  void Log(ModuleEventsLog &log) {
    log.Info("API key: \"%1%\". API secret: %2%. Polling Interval: %3%.",
             apiKey,                                    // 1
             apiSecret.empty() ? "not set" : "is set",  // 2
             pollingInterval);                          // 3
  }

  void Validate() {}
};
}

////////////////////////////////////////////////////////////////////////////////

namespace {

class CcexRequest : public Request {
 public:
  typedef Request Base;

 public:
  explicit CcexRequest(const std::string &uri,
                       const std::string &message,
                       const std::string &uriParams = std::string())
      : Base(uri, message, net::HTTPRequest::HTTP_GET, uriParams) {}

  virtual ~CcexRequest() override = default;

 public:
  virtual boost::tuple<pt::ptime, ptr::ptree, Milestones> Send(
      net::HTTPClientSession &session, const Context &context) override {
    auto result = Base::Send(session, context);
    auto &responseTree = boost::get<1>(result);

    const auto &status = responseTree.get_optional<std::string>("success");
    if (!status || *status != "true") {
      const auto &message = responseTree.get_optional<std::string>("message");
      std::ostringstream error;
      error << "The server returned an error in response to the request \""
            << GetName() << "\" (" << GetRequest().getURI() << "): ";
      if (message) {
        error << "\"" << *message << "\"";
      } else {
        error << "Unknown error";
      }
      error << " (status: ";
      if (status) {
        error << "\"" << *status << "\"";
      } else {
        error << "unknown";
      }
      error << ")";
      throw Exception(error.str().c_str());
    }

    const auto &contentTree = responseTree.get_child_optional("result");
    if (!contentTree) {
      boost::format error(
          "The server did not return response to the request \"%1%\"");
      error % GetName();
      throw Exception(error.str().c_str());
    }

    return boost::make_tuple(std::move(boost::get<0>(result)),
                             std::move(*contentTree),
                             std::move(boost::get<2>(result)));
  }
};

class CcexPublicRequest : public CcexRequest {
 public:
  typedef CcexRequest Base;

 public:
  explicit CcexPublicRequest(const std::string &message,
                             const std::string &uriParams = std::string())
      : Base("/t/api_pub.html", message, uriParams) {}

  virtual ~CcexPublicRequest() override = default;
};

class CcexPrivateRequest : public CcexRequest {
 public:
  typedef CcexRequest Base;

 public:
  explicit CcexPrivateRequest(const std::string &message,
                              const Settings &settings,
                              const std::string &uriParams = std::string())
      : Base("/t/api.html",
             message,
             AppendUriParams("apikey=" + settings.apiKey, uriParams)),
        m_apiSecret(settings.apiSecret) {}

  virtual ~CcexPrivateRequest() override = default;

 protected:
  virtual void PreprareRequest(const net::HTTPClientSession &session,
                               net::HTTPRequest &request) const override {
    using namespace trdk::Lib::Crypto;
    const auto &digest =
        CalcHmacSha512Digest((session.secure() ? "https://" : "http://") +
                                 session.getHost() + GetRequest().getURI(),
                             m_apiSecret);
    request.set("apisign", EncodeToHex(&digest[0], digest.size()));
  }

 private:
  const std::string m_apiSecret;
};

std::string NormilizeSymbol(const std::string &source) {
  auto resutl = boost::replace_first_copy(source, "_", "-");
  boost::to_lower(resutl);
  return resutl;
}
}

////////////////////////////////////////////////////////////////////////////////

namespace {
class CcexExchange : public TradingSystem, public MarketDataSource {
 private:
  typedef boost::mutex SecuritiesMutex;
  typedef SecuritiesMutex::scoped_lock SecuritiesLock;

  struct SecuritySubscribtion {
    boost::shared_ptr<Rest::Security> security;
    boost::shared_ptr<CcexRequest> request;
    bool isSubscribed;
  };

 public:
  explicit CcexExchange(const App &,
                        const TradingMode &mode,
                        size_t tradingSystemIndex,
                        size_t marketDataSourceIndex,
                        Context &context,
                        const std::string &instanceName,
                        const IniSectionRef &conf)
      : TradingSystem(mode, tradingSystemIndex, context, instanceName),
        MarketDataSource(marketDataSourceIndex, context, instanceName),
        m_settings(conf, GetTsLog()),
        m_isConnected(false),
        m_marketDataSession("c-cex.com"),
        m_tradingSession(m_marketDataSession.getHost()) {
    m_marketDataSession.setKeepAlive(true);
    m_tradingSession.setKeepAlive(true);
  }

  virtual ~CcexExchange() override = default;

 public:
  using trdk::TradingSystem::GetContext;

  TradingSystem::Log &GetTsLog() const noexcept {
    return TradingSystem::GetLog();
  }

  MarketDataSource::Log &GetMdsLog() const noexcept {
    return MarketDataSource::GetLog();
  }

 public:
  virtual bool IsConnected() const override { return m_isConnected; }

  //! Makes connection with Market Data Source.
  virtual void Connect(const IniSectionRef &conf) override {
    // Implementation for trdk::MarketDataSource.
    if (IsConnected()) {
      return;
    }
    GetMdsLog().Info("Creating connection...");
    CreateConnection(conf);
  }

  virtual void SubscribeToSecurities() override {
    {
      const SecuritiesLock lock(m_securitiesMutex);
      for (auto &subscribtion : m_securities) {
        if (subscribtion.second.isSubscribed) {
          continue;
        }
        GetMdsLog().Info("Starting Market Data subscribtion for \"%1%\"...",
                         *subscribtion.second.security);
        subscribtion.second.isSubscribed = true;
      }
    }

    if (m_pollingTask) {
      return;
    }
    m_pollingTask = boost::make_unique<PollingTask>(
        [this]() {
          const SecuritiesLock lock(m_securitiesMutex);
          for (const auto &subscribtion : m_securities) {
            if (!subscribtion.second.isSubscribed) {
              continue;
            }
            auto &security = *subscribtion.second.security;
            auto &request = *subscribtion.second.request;
            const auto &response =
                request.Send(m_marketDataSession, GetContext());
            const auto &time = boost::get<0>(response);
            const auto &delayMeasurement = boost::get<2>(response);
            try {
              for (const auto &updateRecord :
                   boost::get<1>(response).get_child("buy")) {
                const auto &update = updateRecord.second;
                security.SetLevel1(
                    time, Level1TickValue::Create<LEVEL1_TICK_BID_PRICE>(
                              update.get<double>("Rate")),
                    Level1TickValue::Create<LEVEL1_TICK_BID_QTY>(
                        update.get<double>("Quantity")),
                    delayMeasurement);
              }
              for (const auto &updateRecord :
                   boost::get<1>(response).get_child("sell")) {
                const auto &update = updateRecord.second;
                security.SetLevel1(
                    time, Level1TickValue::Create<LEVEL1_TICK_ASK_PRICE>(
                              update.get<double>("Rate")),
                    Level1TickValue::Create<LEVEL1_TICK_ASK_QTY>(
                        update.get<double>("Quantity")),
                    delayMeasurement);
              }
            } catch (const std::exception &ex) {
              boost::format error(
                  "Failed to read order book state for \"%1%\": \"%2%\"");
              error % security  // 1
                  % ex.what();  // 2
              throw MarketDataSource::Error(error.str().c_str());
            }
          }
        },
        m_settings.pollingInterval, GetMdsLog());
  }

 protected:
  virtual void CreateConnection(const IniSectionRef &) override {
    m_isConnected = true;
  }

  virtual trdk::Security &CreateNewSecurityObject(
      const Symbol &symbol) override {
    {
      const SecuritiesLock lock(m_securitiesMutex);
      const auto &it = m_securities.find(symbol);
      if (it != m_securities.cend()) {
        return *it->second.security;
      }
    }

    const auto result = boost::make_shared<Rest::Security>(
        GetContext(), symbol, *this,
        Rest::Security::SupportedLevel1Types()
            .set(LEVEL1_TICK_BID_PRICE)
            .set(LEVEL1_TICK_BID_QTY)
            .set(LEVEL1_TICK_ASK_PRICE)
            .set(LEVEL1_TICK_ASK_QTY));
    {
      const auto request = boost::make_shared<CcexPublicRequest>(
          "orderbook",
          "a=getorderbook&market=" +
              NormilizeSymbol(result->GetSymbol().GetSymbol()) +
              "&type=both&depth=1");

      const SecuritiesLock lock(m_securitiesMutex);
      m_securities.emplace(symbol, SecuritySubscribtion{result, request})
          .first->second.security;
    }
    return *result;
  }

  virtual OrderId SendOrderTransaction(trdk::Security &security,
                                       const Currency &currency,
                                       const Qty &qty,
                                       const boost::optional<Price> &price,
                                       const OrderParams &params,
                                       const OrderSide &side,
                                       const TimeInForce &tif) override {
    static_assert(numberOfTimeInForces == 5, "List changed.");
    switch (tif) {
      case TIME_IN_FORCE_IOC:
        return SendOrderTransactionAndEmulateIoc(security, currency, qty, price,
                                                 params, side);
      case TIME_IN_FORCE_GTC:
        break;
      default:
        throw TradingSystem::Error("Order time-in-force type is not supported");
    }
    if (currency != security.GetSymbol().GetCurrency()) {
      throw TradingSystem::Error(
          "Trading system supports only security quote currency");
    }
    if (!price) {
      throw TradingSystem::Error("Market order is not supported");
    }

    boost::format requestParams("a=%1%&market=%2%&quantity=%3$.8f&rate=%4$.8f");
    requestParams % (side == ORDER_SIDE_SELL ? "selllimit" : "buylimit")  // 1
        % NormilizeSymbol(security.GetSymbol().GetSymbol())               // 2
        % qty                                                             // 3
        % *price;                                                         // 4
    GetTsLog().Debug(requestParams.str().c_str());
    CcexPrivateRequest request("order", m_settings, requestParams.str());
    const auto &result = request.Send(m_tradingSession, GetContext());
#ifdef DEV_VER
    {
      std::stringstream ss;
      boost::property_tree::json_parser::write_json(ss, boost::get<1>(result));
      GetTsLog().Debug(ss.str().c_str());
    }
#endif
    try {
      return boost::get<1>(result).get<OrderId>("uuid");
    } catch (const std::exception &ex) {
      boost::format error("Failed to read order transaction reply: \"%1%\"");
      error % ex.what();
      throw TradingSystem::Error(error.str().c_str());
    }

    return 0;
  }

  virtual void SendCancelOrder(const OrderId &) override {
    throw MethodIsNotImplementedException("Methods is not supported");
  }

 private:
  Settings m_settings;

  bool m_isConnected;
  net::HTTPSClientSession m_marketDataSession;
  net::HTTPSClientSession m_tradingSession;

  SecuritiesMutex m_securitiesMutex;
  boost::unordered_map<Lib::Symbol, SecuritySubscribtion> m_securities;

  std::unique_ptr<PollingTask> m_pollingTask;
};
}

////////////////////////////////////////////////////////////////////////////////

TradingSystemAndMarketDataSourceFactoryResult CreateCcex(
    const TradingMode &mode,
    size_t tradingSystemIndex,
    size_t marketDataSourceIndex,
    Context &context,
    const std::string &instanceName,
    const IniSectionRef &configuration) {
  const auto &result = boost::make_shared<CcexExchange>(
      App::GetInstance(), mode, tradingSystemIndex, marketDataSourceIndex,
      context, instanceName, configuration);
  return {result, result};
}

////////////////////////////////////////////////////////////////////////////////
