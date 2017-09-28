/*******************************************************************************
 *   Created: 2017/09/20 19:51:55
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 ******************************************************************************/

#include "Prec.hpp"
#include "Core/Settings.hpp"
#include "FixProtocolIncomingMessages.hpp"
#include "FixProtocolMarketDataSource.hpp"
#include "FixProtocolMessageHandler.hpp"
#include "FixProtocolOutgoingMessages.hpp"
#include "FixProtocolSecurity.hpp"
#include "Common/NetworkStreamClient.hpp"

using namespace trdk;
using namespace trdk::Lib;
using namespace trdk::Lib::TimeMeasurement;
using namespace trdk::Interaction::FixProtocol;

namespace pt = boost::posix_time;
namespace fix = trdk::Interaction::FixProtocol;

////////////////////////////////////////////////////////////////////////////////

namespace {

class Connection : public NetworkStreamClient, public MessageHandler {
 public:
  typedef NetworkStreamClient Base;

 public:
  explicit Connection(Client &service)
      : Base(service,
             service.GetSource().GetSettings().host,
             service.GetSource().GetSettings().port),
        m_standardOutgoingHeader(service.GetSource()),
        m_isAuthorized(false),
        m_utcDiff(service.GetSource()
                      .GetContext()
                      .GetSettings()
                      .GetTimeZone()
                      ->base_utc_offset()) {}

  virtual ~Connection() override = default;

 public:
  virtual void OnLogon(const Incoming::Logon &logon) override {
    if (m_isAuthorized) {
      ProtocolError("Received unexpected Logon-message",
                    &*logon.GetMessageBegin(), 0);
    }
    m_isAuthorized = true;
  }
  virtual void OnLogout(const Incoming::Logout &logout) override {
    GetLog().Info("%1%Logout with the reason: \"%2%\".",
                  GetService().GetLogTag(),  // 1
                  logout.ReadText());        // 3
  }
  virtual void OnHeartbeat(const Incoming::Heartbeat &) {
    Send(Outgoing::Heartbeat(m_standardOutgoingHeader).Export(SOH));
  }
  virtual void OnTestRequest(const Incoming::TestRequest &testRequest) {
    Send(
        Outgoing::Heartbeat(testRequest, m_standardOutgoingHeader).Export(SOH));
  }

  virtual void OnMarketDataSnapshotFullRefresh(
      const Incoming::MarketDataSnapshotFullRefresh &snapshot,
      const Milestones &delayMeasurement) override {
    const auto &time = snapshot.GetTime() + m_utcDiff;
    auto &security = snapshot.ReadSymbol(GetSource());
    bool hasChanges = false;
    snapshot.ReadEachMarketDataEntity(
        [&time, &security, &hasChanges, &delayMeasurement](
            Level1TickValue &&tick, bool isLast) {
          hasChanges = security.SetLevel1(time, std::move(tick), isLast,
                                          hasChanges, delayMeasurement);
        });
  }
  virtual void OnMarketDataIncrementalRefresh(
      const Incoming::MarketDataIncrementalRefresh &,
      const Milestones &) override {
    throw MethodDoesNotImplementedError(
        "Incoming::MarketDataIncrementalRefresh");
  }

 public:
  void RequestMarketData(const fix::Security &security) {
    GetLog().Info("%1%Sending Market Data Request for \"%2%\" (%3%)...",
                  GetService().GetLogTag(),  // 1
                  security,                  // 2
                  security.GetFixId());      // 3
    Outgoing::MarketDataRequest request(security, m_standardOutgoingHeader);
    Send(request.Export(SOH));
  }

 protected:
  fix::MarketDataSource &GetSource() { return GetService().GetSource(); }
  const fix::MarketDataSource &GetSource() const {
    return const_cast<Connection *>(this)->GetSource();
  }
  const Context &GetContext() const { return GetSource().GetContext(); }
  Context &GetContext() { return GetSource().GetContext(); }
  fix::MarketDataSource::Log &GetLog() const { return GetSource().GetLog(); }

 protected:
  virtual Milestones StartMessageMeasurement() const override {
    return GetContext().StartStrategyTimeMeasurement();
  }
  virtual pt::ptime GetCurrentTime() const override {
    return GetSource().GetContext().GetCurrentTime();
  }
  virtual void LogDebug(const std::string &message) const override {
    GetLog().Debug(message.c_str());
  }
  virtual void LogInfo(const std::string &message) const override {
    GetLog().Info(message.c_str());
  }
  virtual void LogWarn(const std::string &message) const override {
    GetLog().Warn(message.c_str());
  }
  virtual void LogError(const std::string &message) const override {
    GetLog().Error(message.c_str());
  }
  virtual Client &GetService() override {
    return *boost::polymorphic_downcast<Client *>(&Base::GetService());
  }
  virtual const Client &GetService() const override {
    return *boost::polymorphic_downcast<const Client *>(&Base::GetService());
  }

 protected:
  virtual void OnStart() override {
    Assert(!m_isAuthorized);
    m_isAuthorized = false;
    SendSynchronously(Outgoing::Logon(m_standardOutgoingHeader).Export(SOH),
                      "Logon");
    const auto &responceContent = ReceiveSynchronously("Logon", 256);
    Incoming::Factory::Create(responceContent.cbegin(), responceContent.cend(),
                              *GetSource().GetSettings().policy)
        ->Handle(*this, Milestones());
    if (!m_isAuthorized) {
      ConnectError("Failed to authorize");
    }
  }

  //! Find message end by reverse iterators.
  /** Called under lock.
    * @param[in] bufferBegin     Buffer begin.
    * @param[in] transferedBegin Last operation transfered begin.
    * @param[in] bufferEnd       Buffer end.
    * @return Last byte of a message, or bufferEnd if the range
    *         doesn't include message end.
    */
  virtual Buffer::const_iterator FindLastMessageLastByte(
      const Buffer::const_iterator &bufferBegin,
      const Buffer::const_iterator &transferedBegin,
      const Buffer::const_iterator &bufferEnd) const override {
    const Buffer::const_reverse_iterator bufferREnd(bufferBegin);
    const Buffer::const_reverse_iterator transferedREnd(transferedBegin);
    Buffer::const_reverse_iterator rbegin(bufferEnd);
    const Buffer::const_reverse_iterator rend(bufferBegin);

    rbegin = std::find(rbegin, transferedREnd, SOH);
    if (rbegin == transferedREnd) {
      return bufferEnd;
    }
    ++rbegin;

    for (;;) {
      const auto checkSumFieldBegin = std::find(rbegin, bufferREnd, SOH);
      if (checkSumFieldBegin == bufferREnd) {
        return bufferEnd;
      }

      const auto &fieldLen = -std::distance(checkSumFieldBegin, rbegin);

      // |10=
      static const int32_t checkSumTagMatch = 1026568449;
      if (fieldLen >= sizeof(checkSumTagMatch) - 1 &&
          reinterpret_cast<decltype(checkSumTagMatch) &>(*checkSumFieldBegin) ==
              checkSumTagMatch) {
        return rbegin.base();
      }

      if (checkSumFieldBegin < transferedREnd) {
        return bufferEnd;
      }

      rbegin = checkSumFieldBegin;
    }
  }

  //! Handles messages in the buffer.
  /** Called under lock. This range has one or more messages.
    */
  virtual void HandleNewMessages(const pt::ptime &,
                                 const Buffer::const_iterator &begin,
                                 const Buffer::const_iterator &end,
                                 const Milestones &delayMeasurement) override {
    const auto &bufferEnd = std::next(end);
    for (auto messageBegin = begin; messageBegin < bufferEnd;) {
      const auto &message = Incoming::Factory::Create(
          messageBegin, bufferEnd, *GetSource().GetSettings().policy);
      message->Handle(*this, delayMeasurement);
      messageBegin = message->GetMessageEnd();
      Assert(messageBegin <= bufferEnd);
    }
  }

 private:
  Outgoing::StandardHeader m_standardOutgoingHeader;
  bool m_isAuthorized;
  const pt::time_duration m_utcDiff;
};
}

////////////////////////////////////////////////////////////////////////////////

Client::Client(const std::string &name, fix::MarketDataSource &source)
    : Base(name), m_source(source) {}

Client::~Client() noexcept {
  try {
    Stop();
  } catch (...) {
    AssertFailNoException();
    terminate();
  }
}

pt::ptime Client::GetCurrentTime() const {
  return GetSource().GetContext().GetCurrentTime();
}

std::unique_ptr<NetworkStreamClient> Client::CreateClient() {
  return boost::make_unique<Connection>(*this);
}

void Client::LogDebug(const char *message) const {
  GetSource().GetLog().Debug(message);
}

void Client::LogInfo(const std::string &message) const {
  GetSource().GetLog().Info(message.c_str());
}

void Client::LogError(const std::string &message) const {
  GetSource().GetLog().Error(message.c_str());
}

void Client::OnConnectionRestored() { GetSource().ResubscribeToSecurities(); }

void Client::OnStopByError(const std::string &) {}

void Client::RequestMarketData(const fix::Security &security) {
  InvokeClient<Connection>(
      boost::bind(&Connection::RequestMarketData, _1, boost::cref(security)));
}

////////////////////////////////////////////////////////////////////////////////