/**************************************************************************
 *   Created: 2016/08/24 19:07:42
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#pragma once

#include "Exception.hpp"
#include "Fwd.hpp"

namespace trdk {
namespace Lib {

class NetworkStreamClientService : private boost::noncopyable {
 public:
  class Exception : public trdk::Lib::Exception {
   public:
    explicit Exception(const char *what) throw();
  };

 public:
  explicit NetworkStreamClientService(bool isSecure);
  explicit NetworkStreamClientService(const std::string &logTag, bool isSecure);
  virtual ~NetworkStreamClientService();

 public:
  const std::string &GetLogTag() const;

  //! Connects.
  /** @throw NetworkStreamClientService::Exception  If connection will fail.
    */
  void Connect();

  bool IsConnected() const;

  //! Stops all.
  void Stop();

  std::unique_ptr<trdk::Lib::NetworkClientServiceIo> CreateIo();

 public:
  void OnDisconnect();

  void OnClientDestroy();

 protected:
  virtual boost::posix_time::ptime GetCurrentTime() const = 0;

  virtual std::unique_ptr<trdk::Lib::NetworkStreamClient> CreateClient() = 0;

  virtual void LogDebug(const char *) const = 0;
  virtual void LogInfo(const std::string &) const = 0;
  virtual void LogError(const std::string &) const = 0;

  virtual void OnConnectionRestored() = 0;
  virtual void OnStopByError(const std::string &message) = 0;

 protected:
  template <typename Client>
  void InvokeClient(const boost::function<void(Client &)> &callback) {
    InvokeClient([callback](trdk::Lib::NetworkStreamClient &client) {
      callback(*boost::polymorphic_downcast<Client *>(&client));
    });
  }

  void InvokeClient(
      const boost::function<void(trdk::Lib::NetworkStreamClient &)> &);

 private:
  class Implementation;
  std::unique_ptr<Implementation> m_pimpl;
};
}
}