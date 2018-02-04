/*******************************************************************************
 *   Created: 2018/01/31 21:37:53
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 ******************************************************************************/

#include "Prec.hpp"
#include "TrendRepeatingStrategyWindow.hpp"
#include "TrendRepeatingStrategy.hpp"

using namespace trdk;
using namespace trdk::Lib;
using namespace trdk::FrontEnd::Lib;
using namespace trdk::Strategies::MarketMaker;

TrendRepeatingStrategyWindow::TrendRepeatingStrategyWindow(
    Engine &engine, const QString &symbol, QWidget *parent)
    : Base(parent), m_engine(engine), m_strategy(CreateStrategy(symbol)) {
  m_ui.setupUi(this);

  setWindowTitle(symbol + " " + tr("Market Making by Trend Strategy") + " - " +
                 QCoreApplication::applicationName());

  m_ui.symbol->setText(symbol);

  m_ui.isPositionsOpeningEnabled->setChecked(m_strategy.IsTradingEnabled());
  m_ui.isPositionsClosingEnabled->setChecked(
      m_strategy.IsActivePositionsControlEnabled());

  m_ui.positionSize->setValue(m_strategy.GetPositionSize());

  m_ui.takeProfit->setValue(m_strategy.GetTakeProfit());
  m_ui.stopLoss->setValue(m_strategy.GetStopLoss() * 100);

  m_ui.fastMaPeriods->setValue(
      static_cast<int>(m_strategy.GetNumberOfFastMaPeriods()));
  m_ui.slowMaPeriods->setValue(
      static_cast<int>(m_strategy.GetNumberOfSlowMaPeriods()));

  ConnectSignals();
}

TrendRepeatingStrategyWindow::~TrendRepeatingStrategyWindow() {
  try {
    if (!m_strategy.IsBlocked()) {
      m_strategy.EnableTrading(false);
      m_strategy.EnableActivePositionsControl(false);
    }
  } catch (...) {
    AssertFailNoException();
    terminate();
  }
}

void TrendRepeatingStrategyWindow::OnBlocked(const QString &reason) {
  {
    const QSignalBlocker blocker(*m_ui.isPositionsOpeningEnabled);
    m_ui.isPositionsOpeningEnabled->setChecked(false);
  }
  {
    const QSignalBlocker blocker(*m_ui.isPositionsClosingEnabled);
    m_ui.isPositionsClosingEnabled->setChecked(false);
  }
  {
    m_ui.controlGroup->setEnabled(false);
    m_ui.positionGroup->setEnabled(false);
    m_ui.trendDetectionGroup->setEnabled(false);
  }
  ShowBlockedStrategyMessage(reason, this);
}

void TrendRepeatingStrategyWindow::OnStrategyEvent(const QString &message) {
  m_ui.log->appendPlainText(QString("%1: %2").arg(
      QDateTime::currentDateTime().time().toString(), message));
}

void TrendRepeatingStrategyWindow::ConnectSignals() {
  Verify(
      connect(m_ui.isPositionsOpeningEnabled, &QCheckBox::toggled,
              [this](bool isEnabled) {
                m_strategy.EnableTrading(isEnabled);
                {
                  const QSignalBlocker blocker(m_ui.isPositionsOpeningEnabled);
                  m_ui.isPositionsOpeningEnabled->setChecked(
                      m_strategy.IsTradingEnabled());
                }
                {
                  const QSignalBlocker blocker(m_ui.isPositionsClosingEnabled);
                  m_ui.isPositionsClosingEnabled->setChecked(
                      m_strategy.IsActivePositionsControlEnabled());
                }
                m_ui.isPositionsClosingEnabled->setEnabled(
                    !m_ui.isPositionsOpeningEnabled->isChecked());
              }));
  Verify(connect(m_ui.isPositionsClosingEnabled, &QCheckBox::toggled,
                 [this](bool isEnabled) {
                   m_strategy.EnableActivePositionsControl(isEnabled);
                   {
                     const QSignalBlocker blocker(
                         m_ui.isPositionsClosingEnabled);
                     m_ui.isPositionsClosingEnabled->setChecked(
                         m_strategy.IsActivePositionsControlEnabled());
                   }
                 }));

  Verify(connect(m_ui.positionSize,
                 static_cast<void (QDoubleSpinBox::*)(double)>(
                     &QDoubleSpinBox::valueChanged),
                 [this](double value) {
                   m_strategy.SetPositionSize(value);
                   {
                     const QSignalBlocker blocker(*m_ui.positionSize);
                     m_ui.positionSize->setValue(m_strategy.GetPositionSize());
                   }
                 }));
  Verify(connect(m_ui.takeProfit,
                 static_cast<void (QDoubleSpinBox::*)(double)>(
                     &QDoubleSpinBox::valueChanged),
                 [this](double value) {
                   m_strategy.SetTakeProfit(value);
                   {
                     const QSignalBlocker blocker(*m_ui.takeProfit);
                     m_ui.takeProfit->setValue(m_strategy.GetTakeProfit());
                   }
                 }));
  Verify(connect(m_ui.stopLoss,
                 static_cast<void (QDoubleSpinBox::*)(double)>(
                     &QDoubleSpinBox::valueChanged),
                 [this](double value) {
                   m_strategy.SetStopLoss(value / 100);
                   {
                     const QSignalBlocker blocker(*m_ui.stopLoss);
                     m_ui.stopLoss->setValue(m_strategy.GetStopLoss() * 100);
                   }
                 }));

  Verify(connect(this, &TrendRepeatingStrategyWindow::Blocked, this,
                 &TrendRepeatingStrategyWindow::OnBlocked,
                 Qt::QueuedConnection));
  Verify(connect(this, &TrendRepeatingStrategyWindow::StrategyEvent, this,
                 &TrendRepeatingStrategyWindow::OnStrategyEvent,
                 Qt::QueuedConnection));
}

TrendRepeatingStrategy &TrendRepeatingStrategyWindow::CreateStrategy(
    const QString &symbol) {
  static boost::uuids::random_generator generateStrategyId;
  const auto &strategyId = generateStrategyId();
  {
    static size_t instanceNumber = 0;
    const IniFile conf(m_engine.GetConfigFilePath());
    const IniSectionRef defaults(conf, "Defaults");
    std::ostringstream os;
    os << "[Strategy.TrendRepeatingMarketMaker/" << symbol.toStdString() << '/'
       << ++instanceNumber << "]" << std::endl
       << "module = MarketMaker" << std::endl
       << "factory = CreateMaTrendRepeatingStrategy" << std::endl
       << "id = " << strategyId << std::endl
       << "is_enabled = true" << std::endl
       << "trading_mode = live" << std::endl
       << "title = " << symbol.toStdString() << " Market Making by Trend"
       << std::endl
       << "requires = Level 1 Updates[" << symbol.toStdString() << "]"
       << std::endl;
    m_engine.GetContext().Add(IniString(os.str()));
  }

  auto &result = *boost::polymorphic_downcast<TrendRepeatingStrategy *>(
      &m_engine.GetContext().GetSrategy(strategyId));

  result.Invoke<TrendRepeatingStrategy>([this](
                                            TrendRepeatingStrategy &strategy) {
    m_blockConnection =
        strategy.SubscribeToBlocking([this](const std::string *reasonSource) {
          QString reason;
          if (reasonSource) {
            reason = QString::fromStdString(*reasonSource);
          }
          emit Blocked(reason);
        });
    m_eventsConnection =
        strategy.SubscribeToEvents([this](const std::string &message) {
          emit StrategyEvent(QString::fromStdString(message));
        });
  });

  return result;
}
