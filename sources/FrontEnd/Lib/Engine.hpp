/*******************************************************************************
 *   Created: 2017/09/10 00:41:14
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 ******************************************************************************/

#pragma once

#include "Core/TradingSystem.hpp"
#include "Api.h"
#include "Fwd.hpp"

namespace trdk {
namespace FrontEnd {

class TRDK_FRONTEND_LIB_API Engine : public QObject {
  Q_OBJECT

 public:
  explicit Engine(const boost::filesystem::path &configFilePath,
                  QWidget *parent);
  ~Engine();

 public:
  const boost::filesystem::path &GetConfigFilePath() const;

  bool IsStarted() const;

  Context &GetContext();
  const FrontEnd::DropCopy &GetDropCopy() const;

  RiskControlScope &GetRiskControl(const TradingMode &);

  std::vector<boost::shared_ptr<Orm::Operation>> GetOperations() const;

#ifdef DEV_VER
  void Test();
#endif

 signals:
  void StateChange(bool isStarted);
  void Message(const QString &, bool isCritical);
  void LogRecord(const QString &);

  void OperationUpdate(const trdk::FrontEnd::Orm::Operation &);
  void OrderUpdate(const trdk::FrontEnd::Orm::Order &);

 public:
  void Start(
      const boost::function<void(const std::string &)> &progressCallback);
  void Stop();

 private:
  class Implementation;
  std::unique_ptr<Implementation> m_pimpl;
};
}  // namespace FrontEnd
}  // namespace trdk