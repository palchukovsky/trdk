/*******************************************************************************
 *   Created: 2017/10/01 18:36:35
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 ******************************************************************************/

#include "Prec.hpp"
#include "MultiBrokerWidget.hpp"
#include "GeneralSetupDialog.hpp"
#include "MultibrokerStrategy.hpp"
#include "OperationContext.hpp"
#include "StrategySetupDialog.hpp"
#include "TimersDialog.hpp"

using namespace trdk;
using namespace trdk::Lib;
using namespace trdk::Lib::TimeMeasurement;
using namespace trdk::Strategies::WilliamCarry;
using namespace trdk::FrontEnd;
using namespace trdk::FrontEnd::Lib;
using namespace trdk::FrontEnd::Shell;
using namespace trdk::FrontEnd::WilliamCarry;

namespace pt = boost::posix_time;
namespace ids = boost::uuids;

MultiBrokerWidget::MultiBrokerWidget(Engine &engine, QWidget *parent)
    : Base(parent),
      m_mode(TRADING_MODE_LIVE),
      m_engine(engine),
      m_currentTradingSecurity(nullptr),
      m_tradingSecurityListWidget(this),
      m_ignoreLockToggling(false),
      m_settings({}),
      m_lots{1, 1, 1, 1, 1},
      m_timer(this) {
  m_ui.setupUi(this);

  m_tradingSecurityListWidget.hide();
  m_tradingSecurityListWidget.raise();

  Verify(connect(m_ui.lock, &QPushButton::toggled, this,
                 &MultiBrokerWidget::LockSecurity));

  Verify(connect(m_ui.buy1, &QPushButton::clicked,
                 [this]() { OpenPosition(0, true); }));
  Verify(connect(m_ui.sell1, &QPushButton::clicked,
                 [this]() { OpenPosition(0, false); }));
  Verify(connect(m_ui.buy2, &QPushButton::clicked,
                 [this]() { OpenPosition(1, true); }));
  Verify(connect(m_ui.sell2, &QPushButton::clicked,
                 [this]() { OpenPosition(1, false); }));
  Verify(connect(m_ui.buy3, &QPushButton::clicked,
                 [this]() { OpenPosition(2, true); }));
  Verify(connect(m_ui.sell3, &QPushButton::clicked,
                 [this]() { OpenPosition(2, false); }));
  Verify(connect(m_ui.buy4, &QPushButton::clicked,
                 [this]() { OpenPosition(3, true); }));
  Verify(connect(m_ui.sell4, &QPushButton::clicked,
                 [this]() { OpenPosition(3, false); }));
  Verify(connect(m_ui.closeAll, &QPushButton::clicked,
                 [this]() { CloseAllPositions(); }));
  Verify(connect(m_ui.currentTradingSecurity, &QPushButton::clicked, [this]() {
    for (int i = 0; i < m_securityList.size(); ++i) {
      const auto &security = *m_securityList[i];
      if (m_ui.currentTradingSecurity->text() ==
          QString::fromStdString(security.GetSymbol().GetSymbol())) {
        m_ui.securityList->setCurrentRow(i);
        return;
      }
    }
  }));
  Verify(connect(m_ui.currentTradingSecurityList, &QToolButton::clicked, this,
                 &MultiBrokerWidget::ShowTradingSecurityList));

  Verify(connect(m_ui.showStrategy, &QPushButton::clicked, this,
                 &MultiBrokerWidget::ShowStrategySetupDialog));
  Verify(connect(m_ui.showTimers, &QPushButton::clicked, this,
                 &MultiBrokerWidget::ShowTimersSetupDialog));
  Verify(connect(m_ui.showSetup, &QPushButton::clicked, this,
                 &MultiBrokerWidget::ShowGeneralSetup));

  Verify(connect(m_ui.targetList, &QListWidget::currentRowChanged,
                 [this](int index) {
                   if (index < 0) {
                     m_ui.securityList->setCurrentRow(-1);
                     m_tradingSecurityListWidget.setCurrentIndex(-1);
                     return;
                   }
                   ReloadSecurityList();
                 }));
  Verify(connect(m_ui.securityList, &QListWidget::currentRowChanged,
                 [this](int index) {
                   if (index < 0) {
                     SetTradingSecurity(nullptr);
                     return;
                   }
                   AssertGt(m_securityList.size(), index);
                   SetTradingSecurity(&*m_securityList[index]);
                 }));

  Verify(connect(
      &m_tradingSecurityListWidget,
      static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
      [this](int index) {
        if (index < 0) {
          SetTradingSecurity(nullptr);
          return;
        }
        AssertGt(m_securityList.size(), index);
        m_ui.currentTradingSecurity->setText(QString::fromStdString(
            m_securityList[index]->GetSymbol().GetSymbol()));
        m_tradingSecurityListWidget.hide();
      }));

  Verify(connect(&m_engine, &Engine::StateChanged, this,
                 &MultiBrokerWidget::OnStateChanged, Qt::QueuedConnection));

  Verify(connect(&m_engine.GetDropCopy(), &Lib::DropCopy::PriceUpdate, this,
                 &MultiBrokerWidget::UpdatePrices, Qt::QueuedConnection));

  Verify(connect(this, &MultiBrokerWidget::PositionChanged, this,
                 &MultiBrokerWidget::OnPosition, Qt::QueuedConnection));

  {
    Verify(connect(&m_timer, &QTimer::timeout,
                   [this]() { SetTime(QTime::currentTime(), *m_ui.time); }));
    m_timer.start(1000);
    SetTime(QTime::currentTime(), *m_ui.time);
  }

  LoadSettings();

  adjustSize();
}

QSize MultiBrokerWidget::sizeHint() const {
  auto result = Base::sizeHint();
  result.setWidth(static_cast<int>(result.width() * 0.7));
  return result;
}

void MultiBrokerWidget::resizeEvent(QResizeEvent *) {
  m_tradingSecurityListWidget.hide();
}

void MultiBrokerWidget::OpenPosition(size_t strategyIndex, bool isLong) {
  AssertGt(m_strategies.size(), strategyIndex);
  AssertGt(m_settings.size(), strategyIndex);
  Assert(m_strategies[strategyIndex]);
  Assert(m_currentTradingSecurity);

  const auto &delayMeasurement =
      m_engine.GetContext().StartStrategyTimeMeasurement();

  auto &settings = m_settings[strategyIndex];
  if (!settings.isEnabled) {
    return;
  }
  auto &startegy = *m_strategies[strategyIndex];

  Price price;
  {
    const auto it = m_lockedSecurityList.find(m_currentTradingSecurity);
    if (it != m_lockedSecurityList.cend()) {
      if (isLong) {
        price = it->second.bid;
      } else {
        price = it->second.ask;
      }
    } else if (isLong) {
      price = m_currentTradingSecurity->GetAskPrice();
    } else {
      price = m_currentTradingSecurity->GetBidPrice();
    }
  }

  std::vector<OperationContext> operations;
  operations.reserve(m_lots.size());
  for (size_t i = 0; i < m_lots.size(); ++i) {
    AssertGt(settings.brokers.size(), i);
    if (i >= settings.brokers.size() ||
        i >= m_engine.GetContext().GetNumberOfTradingSystems()) {
      break;
    } else if (!settings.brokers[i]) {
      continue;
    }

    const auto qty = (settings.lotMultiplier * m_lots[i]) *
                     m_currentTradingSecurity->GetLotSize();
    const auto &pip =
        1.0 /
        std::pow(10, std::max<Double>(
                         2, m_currentTradingSecurity->GetPricePrecision() - 1));

    OperationContext operation(qty, price, startegy.GetTradingSystem(i));

    if (!settings.target2) {
      const auto targetStep =
          settings.target1.pips / settings.numberOfStepsToTarget;
      for (size_t stepI = 1; stepI <= settings.numberOfStepsToTarget; ++stepI) {
        const auto pipPerStep = targetStep * stepI;
        const auto priceChange = pip * pipPerStep;
        operation.AddTakeProfitStopLimit(priceChange, settings.target1.delay);
      }
    } else {
      operation.AddTakeProfitStopLimit(pip * settings.target1.pips,
                                       settings.target1.delay);
      operation.AddTakeProfitStopLimit(pip * settings.target2->pips,
                                       settings.target2->delay);
    }

    operation.AddStopLoss(pip * settings.stopLoss1, pt::not_a_date_time);

    if (settings.stopLoss2) {
      operation.AddStopLoss(pip * settings.stopLoss2->pips,
                            settings.stopLoss2->delay);
    }
    if (settings.stopLoss3) {
      operation.AddStopLoss(pip * settings.stopLoss3->pips,
                            settings.stopLoss3->delay);
    }

    operations.emplace_back(std::move(operation));
  }

  try {
    startegy.Invoke<MultibrokerStrategy>([this, &settings, &operations, isLong,
                                          &delayMeasurement](
        MultibrokerStrategy &multibroker) {
      multibroker.OpenPosition(std::move(operations), *m_currentTradingSecurity,
                               isLong, delayMeasurement);
    });
  } catch (const std::exception &ex) {
    QMessageBox::critical(this, tr("Failed to send order"),
                          QString("%1.").arg(ex.what()), QMessageBox::Abort);
  }
}

void MultiBrokerWidget::CloseAllPositions() {
  for (;;) {
    try {
      m_engine.GetContext().CloseSrategiesPositions();
      break;
    } catch (const std::exception &ex) {
      if (QMessageBox::critical(this, tr("Failed to close all positions"),
                                QString("%1.").arg(ex.what()),
                                QMessageBox::Abort | QMessageBox::Retry) !=
          QMessageBox::Retry) {
        break;
      }
    }
  }
}

void MultiBrokerWidget::LockSecurity(bool lock) {
  if (m_ignoreLockToggling) {
    return;
  }
  if (!lock) {
    if (m_currentTradingSecurity) {
      m_lockedSecurityList.erase(m_currentTradingSecurity);
    }
    ResetLockedPrices(m_currentTradingSecurity);
    return;
  }

  Assert(m_currentTradingSecurity);
  const auto &locked = m_lockedSecurityList.emplace(std::make_pair(
      m_currentTradingSecurity,
      Locked{QTime::currentTime(), m_currentTradingSecurity->GetBidPriceValue(),
             m_currentTradingSecurity->GetAskPriceValue()}));
  Assert(locked.second);
  SetLockedPrices(locked.first->second, *m_currentTradingSecurity);
}

TradingSystem *MultiBrokerWidget::GetSelectedTradingSystem() {
  const auto &index = m_ui.targetList->currentRow();
  if (index < 0) {
    return nullptr;
  }
  return &m_engine.GetContext().GetTradingSystem(index, m_mode);
}

void MultiBrokerWidget::Reload() {
  m_signalConnections.clear();
  m_securityList.clear();
  m_ui.securityList->clear();
  m_tradingSecurityListWidget.clear();
  m_ui.targetList->clear();
  m_lockedSecurityList.clear();
  LockSecurity(false);

  for (size_t i = 0; i < m_engine.GetContext().GetNumberOfTradingSystems();
       ++i) {
    auto &tradingSystem = m_engine.GetContext().GetTradingSystem(i, m_mode);
    m_ui.targetList->addItem(
        QString::fromStdString(tradingSystem.GetInstanceName()));
  }
  ReloadSecurityList();
  if (m_ui.targetList->count() > 0) {
    m_ui.targetList->setCurrentRow(0);
  }

  m_strategies[0] = &m_engine.GetContext().GetSrategy(
      ids::string_generator()("DB005668-9EA5-42E2-88CD-8B25040CDE34"));
  m_strategies[1] = &m_engine.GetContext().GetSrategy(
      ids::string_generator()("D7E1F34F-A6A8-4656-9612-8D9F93D62760"));
  m_strategies[2] = &m_engine.GetContext().GetSrategy(
      ids::string_generator()("646CE7C2-216B-4C2F-A7B8-5B8B45B0EBF4"));
  m_strategies[3] = &m_engine.GetContext().GetSrategy(
      ids::string_generator()("A7ACF9B5-B3FB-4DD8-BA39-CE7DD04D4F2E"));

  for (size_t i = 1; i <= m_strategies.size(); ++i) {
    m_strategies[i - 1]->Invoke<MultibrokerStrategy>([this, i](
        MultibrokerStrategy &multibroker) {
      m_signalConnections.emplace_back(multibroker.SubscribeToPositionsUpdates(
          [this, i](bool isLong, bool isActive) {
            emit PositionChanged(i, isLong, isActive);
          }));
    });
  }
}

void MultiBrokerWidget::ReloadSecurityList() {
  const auto *const tradingSystem = GetSelectedTradingSystem();
  if (!tradingSystem) {
    m_ui.securityList->clear();
    m_tradingSecurityListWidget.clear();
    return;
  }

  int tradingSecurity = 0;
  int marketDataSecurity = 0;
  {
    const QSignalBlocker securityListSignalsBlocker(*m_ui.securityList);
    const QSignalBlocker tradingsecurityListSignalsBlocker(
        m_tradingSecurityListWidget);

    m_ui.securityList->clear();
    m_tradingSecurityListWidget.clear();

    for (size_t i = 0; i < m_engine.GetContext().GetNumberOfMarketDataSources();
         ++i) {
      auto &source = m_engine.GetContext().GetMarketDataSource(i);
      if (source.GetInstanceName() != tradingSystem->GetInstanceName()) {
        continue;
      }
      source.ForEachSecurity([&](Security &security) {
        m_securityList.emplace_back(&security);
        if (m_currentTradingSecurity &&
            m_currentTradingSecurity->GetSymbol() == security.GetSymbol()) {
          tradingSecurity = m_tradingSecurityListWidget.count();
        }
        if (m_currentTradingSecurity &&
            m_currentTradingSecurity->GetSymbol() == security.GetSymbol()) {
          marketDataSecurity = m_ui.securityList->count();
        }
        {
          const auto symbol =
#ifdef _DEBUG
              QString("%1 (%2)").arg(
                  QString::fromStdString(security.GetSymbol().GetSymbol()),
                  QString::fromStdString(
                      security.GetSource().GetInstanceName()))
#else
              QString::fromStdString(security.GetSymbol().GetSymbol())
#endif
              ;
          m_ui.securityList->addItem(symbol);
          m_tradingSecurityListWidget.addItem(symbol);
        }
      });
      break;
    }
  }

  m_ui.securityList->setCurrentRow(marketDataSecurity);
  m_tradingSecurityListWidget.setCurrentIndex(tradingSecurity);
  SetTradingSecurity(&*m_securityList[tradingSecurity]);
}

void MultiBrokerWidget::OnStateChanged(bool isStarted) {
  if (isStarted) {
    Reload();
  }
  m_ui.trading->setEnabled(isStarted);
}

void MultiBrokerWidget::UpdatePrices(const Security *security) {
  if (m_currentTradingSecurity != security) {
    return;
  }
  SetCurretPrices(security->GetLastMarketDataTime(),
                  security->GetBidPriceValue(), security->GetAskPriceValue(),
                  security);
}

void MultiBrokerWidget::SetCurretPrices(const pt::ptime &,
                                        const Price &bid,
                                        const Price &ask,
                                        const Security *security) {
  SetPrices(bid, *m_ui.bid, ask, *m_ui.ask, security);
}

void MultiBrokerWidget::SetLockedPrices(const Locked &locked,
                                        const Security &security) {
  SetTime(locked.time, *m_ui.lockedTime);
  SetPrices(locked.bid, *m_ui.lockedBid, locked.ask, *m_ui.lockedAsk,
            &security);
}
void MultiBrokerWidget::ResetLockedPrices(const Security *security) {
  ResetTime(*m_ui.lockedTime);
  SetPrices(std::numeric_limits<double>::quiet_NaN(), *m_ui.lockedBid,
            std::numeric_limits<double>::quiet_NaN(), *m_ui.lockedAsk,
            security);
}

void MultiBrokerWidget::SetTime(const QTime &time, QLabel &timeControl) {
  static const QString format("hh:mm:ss");
  timeControl.setText(time.toString(format));
}

void MultiBrokerWidget::ResetTime(QLabel &timeControl) {
  timeControl.setText("");
}

void MultiBrokerWidget::SetPrices(const Price &bid,
                                  QLabel &bidControl,
                                  const Price &ask,
                                  QLabel &askControl,
                                  const Security *security) const {
  const auto &precision = security ? security->GetPricePrecision() : 5;
  if (bid.IsNotNan()) {
    bidControl.setText(QString::number(bid, 'f', precision));
  } else {
    bidControl.setText(QString());
  }
  if (ask.IsNotNan()) {
    askControl.setText(QString::number(ask, 'f', precision));
  } else {
    askControl.setText(QString());
  }
}

void MultiBrokerWidget::ShowStrategySetupDialog() {
  StrategySetupDialog dialog(m_settings, this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  m_settings = dialog.GetSettings();
  SaveSettings();
}

void MultiBrokerWidget::ShowGeneralSetup() {
  GeneralSetupDialog dialog(m_lots, this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  m_lots = dialog.GetLots();
  SaveSettings();
}

void MultiBrokerWidget::ShowTimersSetupDialog() {
  TimersDialog dialog(this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  SaveSettings();
}

void MultiBrokerWidget::ShowTradingSecurityList() {
  m_tradingSecurityListWidget.move(
      m_ui.currentTradingSecurityList->geometry().topLeft());
  m_tradingSecurityListWidget.show();
  m_tradingSecurityListWidget.showPopup();
  m_tradingSecurityListWidget.setFocus();
}

void MultiBrokerWidget::SetTradingSecurity(Security *security) {
  m_currentTradingSecurity = security;

  if (m_ui.currentTradingSecurity->text().isEmpty() &&
      m_currentTradingSecurity) {
    m_ui.currentTradingSecurity->setText(
        QString::fromStdString(security->GetSymbol().GetSymbol()));
  }

  m_ui.buy1->setEnabled(m_currentTradingSecurity ? true : false);
  m_ui.sell1->setEnabled(m_currentTradingSecurity ? true : false);
  m_ui.buy2->setEnabled(m_currentTradingSecurity ? true : false);
  m_ui.sell2->setEnabled(m_currentTradingSecurity ? true : false);
  m_ui.buy3->setEnabled(m_currentTradingSecurity ? true : false);
  m_ui.sell3->setEnabled(m_currentTradingSecurity ? true : false);
  m_ui.buy4->setEnabled(m_currentTradingSecurity ? true : false);
  m_ui.sell4->setEnabled(m_currentTradingSecurity ? true : false);

  m_ui.lock->setEnabled(m_currentTradingSecurity ? true : false);

  if (!m_currentTradingSecurity) {
    m_ui.symbol->setText(QString());
    return;
  }

  m_ui.symbol->setText(QString::fromStdString(
      m_currentTradingSecurity->GetSymbol().GetSymbol()));
  UpdatePrices(m_currentTradingSecurity);

  {
    const auto &locked = m_lockedSecurityList.find(m_currentTradingSecurity);
    Assert(!m_ignoreLockToggling);
    m_ignoreLockToggling = true;
    if (locked == m_lockedSecurityList.cend()) {
      ResetLockedPrices(m_currentTradingSecurity);
      m_ui.lock->setChecked(false);
    } else {
      SetLockedPrices(locked->second, *m_currentTradingSecurity);
      m_ui.lock->setChecked(true);
    }
    Assert(m_ignoreLockToggling);
    m_ignoreLockToggling = false;
  }
}

namespace {
const size_t settingsFormatVersion = 2;
}

void MultiBrokerWidget::SaveSettings() {
  QSettings settingsStorage;
  settingsStorage.clear();
  settingsStorage.setValue("version", settingsFormatVersion);
  {
    settingsStorage.beginGroup("General");
    size_t i = 0;
    for (const auto &lots : m_lots) {
      settingsStorage.setValue(QString("brokers/%1/lots").arg(++i), lots.Get());
    }
    settingsStorage.endGroup();
  }
  {
    settingsStorage.beginGroup("Strategies");
    size_t i = 0;
    for (const auto &strategySettings : m_settings) {
      settingsStorage.beginGroup(QString("%1").arg(++i));
      settingsStorage.setValue("enabled", strategySettings.isEnabled);
      settingsStorage.setValue("lot multiplier",
                               strategySettings.lotMultiplier);
      settingsStorage.setValue("targets/1/pips", strategySettings.target1.pips);
      settingsStorage.setValue(
          "targets/1/delay",
          strategySettings.target1.delay.total_microseconds());
      settingsStorage.setValue("number of steps to target",
                               strategySettings.numberOfStepsToTarget);
      if (strategySettings.target2) {
        settingsStorage.setValue("targets/2/pips",
                                 strategySettings.target2->pips);
        settingsStorage.setValue(
            "targets/2/delay",
            strategySettings.target2->delay.total_microseconds());
      }
      settingsStorage.setValue("stop-loses/1", strategySettings.stopLoss1);
      {
        settingsStorage.setValue("stop-loses/2/enabled",
                                 strategySettings.stopLoss2 ? true : false);
        if (strategySettings.stopLoss2) {
          settingsStorage.setValue("stop-loses/2/pips",
                                   strategySettings.stopLoss2->pips);
          settingsStorage.setValue(
              "stop-loses/2/delay",
              strategySettings.stopLoss2->delay.total_microseconds());
        }
      }
      {
        settingsStorage.setValue("stop-loses/3/enabled",
                                 strategySettings.stopLoss3 ? true : false);
        if (strategySettings.stopLoss3) {
          settingsStorage.setValue("stop-loses/3/pips",
                                   strategySettings.stopLoss3->pips);

          settingsStorage.setValue(
              "stop-loses/3/delay",
              strategySettings.stopLoss3->delay.total_microseconds());
        }
      }
      for (size_t brokerI = 0; brokerI < 5; ++brokerI) {
        const bool isEnabled = strategySettings.brokers.size() > brokerI &&
                               strategySettings.brokers[brokerI];
        settingsStorage.setValue(QString("broker/%1").arg(brokerI + 1),
                                 isEnabled);
      }
      settingsStorage.endGroup();
    }
  }
}

void MultiBrokerWidget::LoadSettings() {
  QSettings settingsStorage;
  if (settingsStorage.value("version").toUInt() != settingsFormatVersion) {
    QMessageBox::warning(this, tr("Settings loading"),
                         tr("Settings storage has changed format version. "
                            "Please set settings again."),
                         QMessageBox::Ok);
    ShowGeneralSetup();
    ShowStrategySetupDialog();
    ShowTimersSetupDialog();
    return;
  }
  {
    settingsStorage.beginGroup("General");
    size_t i = 0;
    for (auto &lots : m_lots) {
      lots = settingsStorage.value(QString("brokers/%1/lots").arg(++i), 1)
                 .toDouble();
    }
    settingsStorage.endGroup();
  }
  {
    settingsStorage.beginGroup("Strategies");
    size_t i = 0;
    for (auto &strategySettings : m_settings) {
      settingsStorage.beginGroup(QString("%1").arg(++i));
      strategySettings.isEnabled =
          settingsStorage.value("enabled", false).toBool();
      strategySettings.lotMultiplier =
          settingsStorage.value("lot multiplier", 1).toUInt();
      strategySettings.target1.pips =
          settingsStorage.value("targets/1/pips", 1).toULongLong();
      strategySettings.target1.delay = pt::microseconds(
          settingsStorage.value("targets/1/delay", 1000 * 1000).toULongLong());
      strategySettings.numberOfStepsToTarget =
          settingsStorage.value("number of steps to target", 1).toUInt();
      if (settingsStorage.contains("targets/2/pips") &&
          settingsStorage.contains("targets/2/delay")) {
        strategySettings.target2 = StrategySettings::Target{
            settingsStorage.value("targets/2/pips").toULongLong(),
            pt::microseconds(
                settingsStorage.value("targets/2/delay").toULongLong())};
      }
      strategySettings.stopLoss1 =
          settingsStorage.value("stop-loses/1").toUInt();
      if (settingsStorage.value("stop-loses/2/enabled").toBool()) {
        strategySettings.stopLoss2 = StrategySettings::Target{
            settingsStorage.value("stop-loses/2/pips").toUInt(),
            pt::microseconds(
                settingsStorage.value("stop-loses/2/delay").toInt())};
      }
      if (settingsStorage.value("stop-loses/3/enabled").toBool()) {
        strategySettings.stopLoss3 = StrategySettings::Target{
            settingsStorage.value("stop-loses/3/pips").toUInt(),
            pt::microseconds(
                settingsStorage.value("stop-loses/3/delay").toInt())};
      }

      for (size_t brokerI = 0; brokerI < 5; ++brokerI) {
        const bool isEnabled =
            settingsStorage.value(QString("broker/%1").arg(brokerI + 1))
                .toBool();
        strategySettings.brokers.emplace_back(isEnabled);
      }
      settingsStorage.endGroup();
    }
    settingsStorage.endGroup();
  }
}

void MultiBrokerWidget::OnPosition(size_t strategy,
                                   bool isLong,
                                   bool isActive) {
  const auto &highlight = [isLong, isActive](QPushButton &buy,
                                             QPushButton &sell) {
    auto &button = isLong ? buy : sell;
    button.setEnabled(!isActive);
    if (isActive) {
      static const QString style(
          "background-color: rgb(0, 153, 0);\ncolor: rgb(251, 253, 254);");
      button.setStyleSheet(style);
    } else if (isLong) {
      static const QString style(
          "background-color: rgb(39, 60, 194); color: rgb(251, 253, 254); ");
      button.setStyleSheet(style);
    } else {
      static const QString style(
          "background-color: rgb(230, 59, 1);\ncolor: rgb(251, 253, 254);");
      button.setStyleSheet(style);
    }
  };
  switch (strategy) {
    case 1:
      highlight(*m_ui.buy1, *m_ui.sell1);
      break;
    case 2:
      highlight(*m_ui.buy2, *m_ui.sell2);
      break;
    case 3:
      highlight(*m_ui.buy3, *m_ui.sell3);
      break;
    case 4:
      highlight(*m_ui.buy4, *m_ui.sell4);
      break;
    default:
      AssertEq(1, strategy);
      return;
  }
}

////////////////////////////////////////////////////////////////////////////////

ModuleFactoryResult CreateEngineFrontEndWidgets(Engine &engine,
                                                QWidget *parent) {
  ModuleFactoryResult result;
  result.emplace_back("Multibroker",
                      boost::make_unique<MultiBrokerWidget>(engine, parent));
  return result;
}

////////////////////////////////////////////////////////////////////////////////