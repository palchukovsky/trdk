/*******************************************************************************
 *   Created: 2017/10/01 18:32:48
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 ******************************************************************************/

#pragma once

#include "GeneratedFiles/ui_MultiBrokerWidget.h"
#include "StrategySettings.hpp"

namespace trdk {
namespace FrontEnd {
namespace WilliamCarry {

class MultiBrokerWidget : public QWidget {
  Q_OBJECT

 public:
  typedef QWidget Base;

 private:
  struct Locked {
    QTime time;
    Price bid;
    Price ask;
  };

 public:
  explicit MultiBrokerWidget(Lib::Engine &, QWidget *parent);
  virtual ~MultiBrokerWidget() override = default;

 public:
  TradingSystem *GetSelectedTradingSystem();

  virtual QSize sizeHint() const override;

 protected slots:
  void LockSecurity(bool lock);
  void OnStateChanged(bool isStarted);
  void UpdatePrices(const Security *);
  void ShowStrategySetupDialog();
  void ShowGeneralSetup();
  void ShowTimersSetupDialog();
  void ShowTradingSecurityList();
  void OnPosition(size_t strategy, bool isLong, bool isActive);
  virtual void resizeEvent(QResizeEvent *);

 signals:
  void PositionChanged(size_t strategy, bool isLong, bool isActive);

 private:
  void Reload();
  void ReloadSecurityList();
  void OpenPosition(size_t strategyIndex, bool isLong);
  void CloseAllPositions();

  void SetCurretPrices(const boost::posix_time::ptime &,
                       const Price &bid,
                       const Price &ask,
                       const Security *);
  void SetLockedPrices(const Locked &, const Security &);
  void ResetLockedPrices(const Security *);
  void SetTime(const QTime &, QLabel &timeControl);
  void ResetTime(QLabel &timeControl);
  void SetPrices(const Price &bid,
                 QLabel &bidControl,
                 const Price &ask,
                 QLabel &askControl,
                 const Security *) const;

  void SetTradingSecurity(Security *);

  void LoadSettings();
  void SaveSettings();

 private:
  const TradingMode m_mode;
  Ui::MultiBrokerWidget m_ui;
  Lib::Engine &m_engine;

  Security *m_currentTradingSecurity;
  std::vector<Security *> m_securityList;

  QComboBox m_tradingSecurityListWidget;

  boost::unordered_map<Security *, Locked> m_lockedSecurityList;
  bool m_ignoreLockToggling;

  boost::array<Strategy *, NUMBER_OF_STRATEGIES> m_strategies;
  std::vector<boost::signals2::scoped_connection> m_signalConnections;
  boost::array<StrategySettings, NUMBER_OF_STRATEGIES> m_settings;
  std::vector<trdk::Lib::Double> m_lots;

  QTimer m_timer;
};
}
}
}