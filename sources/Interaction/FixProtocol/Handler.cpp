/*******************************************************************************
 *   Created: 2017/10/01 05:23:43
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 ******************************************************************************/

#include "Prec.hpp"
#include "Handler.hpp"
#include "IncomingMessages.hpp"
#include "OutgoingMessages.hpp"
#include "Settings.hpp"

using namespace trdk;
using namespace Lib;
using namespace TimeMeasurement;
using namespace Interaction::FixProtocol;
namespace fix = Interaction::FixProtocol;
namespace in = Incoming;
namespace out = Outgoing;
namespace pt = boost::posix_time;
namespace ptr = boost::property_tree;

namespace {
typedef NetworkStreamClient::ProtocolError ProtocolError;

fix::Settings LoadSettings(const ptr::ptree &conf,
                           const trdk::Settings &settings,
                           ModuleEventsLog &log) {
  fix::Settings result(conf, settings);
  result.Log(log);
  result.Validate();
  return result;
}
}  // namespace

class Handler::Implementation : private boost::noncopyable {
 public:
  const fix::Settings m_settings;
  bool m_isAuthorized;
  out::StandardHeader m_standardHeader;

 public:
  explicit Implementation(const Context &context,
                          const ptr::ptree &conf,
                          ModuleEventsLog &log)
      : m_settings(LoadSettings(conf, context.GetSettings(), log)),
        m_isAuthorized(false),
        m_standardHeader(m_settings) {}
};

Handler::Handler(const Context &context,
                 const ptr::ptree &conf,
                 ModuleEventsLog &log)
    : m_pimpl(boost::make_unique<Implementation>(context, conf, log)) {}

Handler::~Handler() = default;

bool Handler::IsAuthorized() const { return m_pimpl->m_isAuthorized; }

const fix::Settings &Handler::GetSettings() const {
  return m_pimpl->m_settings;
}

out::StandardHeader &Handler::GetStandardOutgoingHeader() {
  return m_pimpl->m_standardHeader;
}

void Handler::OnLogon(const in::Logon &logon, NetworkStreamClient &) {
  if (m_pimpl->m_isAuthorized) {
    ProtocolError("Received unexpected Logon-message",
                  &*logon.GetMessageBegin(), 0);
  }
  m_pimpl->m_isAuthorized = true;
}

void Handler::OnLogout(const in::Logout &logout, NetworkStreamClient &client) {
  GetLog().Info("%1%Logout with the reason: \"%2%\".",
                client.GetLogTag(),  // 1
                logout.ReadText());  // 3
}

void Handler::OnHeartbeat(const in::Heartbeat &, NetworkStreamClient &client) {
  client.Send(out::Heartbeat(GetStandardOutgoingHeader()).Export(SOH));
}

void Handler::OnTestRequest(const in::TestRequest &testRequest,
                            NetworkStreamClient &client) {
  client.Send(Outgoing::Heartbeat(testRequest, GetStandardOutgoingHeader())
                  .Export(SOH));
}

void Handler::OnResendRequest(const in::ResendRequest &message,
                              NetworkStreamClient &client) {
  GetLog().Error("%1%Resend Request received.", client.GetLogTag());
  throw ProtocolError("Resend request received", &*message.GetMessageBegin(),
                      0);
}

void Handler::OnReject(const in::Reject &reject, NetworkStreamClient &client) {
  GetLog().Error("%1%Reject received for message %2%: \"%3%\".",
                 client.GetLogTag(),      // 1
                 reject.ReadRefSeqNum(),  // 2
                 reject.ReadText());      // 3
  throw ProtocolError("Reject received", &*reject.GetMessageBegin(), 0);
}

void Handler::OnMarketDataRequestReject(
    const in::MarketDataRequestReject &reject, NetworkStreamClient &client) {
  GetLog().Error("%1%Market Data Request Reject received: \"%2%\".",
                 client.GetLogTag(),  // 1
                 reject.ReadText());  // 2
  throw ProtocolError("Market Data Request Reject received",
                      &*reject.GetMessageBegin(), 0);
}

void Handler::OnMarketDataSnapshotFullRefresh(
    const in::MarketDataSnapshotFullRefresh &message,
    NetworkStreamClient &,
    const Milestones &) {
  ProtocolError(
      "Received unsupported message Market Data Snapshot Full Refresh",
      &*message.GetMessageBegin(), 0);
}

void Handler::OnMarketDataIncrementalRefresh(
    const in::MarketDataIncrementalRefresh &message,
    NetworkStreamClient &,
    const Milestones &) {
  ProtocolError("Received unsupported message Market Data Incremental Refresh",
                &*message.GetMessageBegin(), 0);
}

void Handler::OnBusinessMessageReject(const in::BusinessMessageReject &message,
                                      NetworkStreamClient &,
                                      const Milestones &) {
  ProtocolError("Received unsupported message Business Message Reject",
                &*message.GetMessageBegin(), 0);
}

void Handler::OnExecutionReport(const in::ExecutionReport &message,
                                NetworkStreamClient &,
                                const Milestones &) {
  ProtocolError("Received unsupported message Execution Report",
                &*message.GetMessageBegin(), 0);
}

void Handler::OnOrderCancelReject(const in::OrderCancelReject &message,
                                  NetworkStreamClient &,
                                  const Milestones &) {
  ProtocolError("Received unsupported message Order Cancel Reject",
                &*message.GetMessageBegin(), 0);
}
