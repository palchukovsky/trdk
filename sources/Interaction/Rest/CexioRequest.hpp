/*******************************************************************************
 *   Created: 2018/01/18 01:02:16
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 ******************************************************************************/

#pragma once

#include "FloodControl.hpp"
#include "Request.hpp"

namespace trdk {
namespace Interaction {
namespace Rest {

////////////////////////////////////////////////////////////////////////////////

class CexioRequest : public Request {
 public:
  typedef Request Base;

  explicit CexioRequest(const std::string &name,
                        const std::string &method,
                        const std::string &params,
                        const Context &,
                        ModuleEventsLog &,
                        ModuleTradingLog * = nullptr);
  ~CexioRequest() override = default;

  Response Send(std::unique_ptr<Poco::Net::HTTPSClientSession> &) override;

 protected:
  FloodControl &GetFloodControl() const override;
  void WriteUri(std::string uri, Poco::Net::HTTPRequest &) const override;
};

////////////////////////////////////////////////////////////////////////////////

class CexioPublicRequest : public CexioRequest {
 public:
  explicit CexioPublicRequest(const std::string &name,
                              const std::string &params,
                              const Context &context,
                              ModuleEventsLog &log)
      : CexioRequest(
            name, Poco::Net::HTTPRequest::HTTP_GET, params, context, log) {}
  ~CexioPublicRequest() override = default;

 protected:
  bool IsPriority() const override { return false; }
};

////////////////////////////////////////////////////////////////////////////////
}  // namespace Rest
}  // namespace Interaction
}  // namespace trdk
