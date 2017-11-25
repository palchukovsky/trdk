/*******************************************************************************
 *   Created: 2017/10/16 00:42:57
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 ******************************************************************************/

#pragma once

#include "Advice.hpp"
#include "TargetActionsWidget.hpp"
#include "TargetSideWidget.hpp"
#include "TargetTitleWidget.hpp"
#include "ui_StrategyWindow.h"

namespace trdk {
namespace Strategies {
namespace ArbitrageAdvisor {

class StrategyWindow : public QMainWindow {
  Q_OBJECT

 public:
  typedef QMainWindow Base;

 private:
  struct TargetWidgets;

  struct Target {
    TradingMode tradingMode;
    Security *security;
    TargetWidgets *widgets;

    const Security *GetSecurityPtr() const { return security; }
    const std::string &GetSymbol() const;
    const trdk::TradingSystem *GetTradingSystem() const;
  };

  struct BySecurity {};
  struct BySymbol {};

  typedef boost::multi_index_container<
      Target,
      boost::multi_index::indexed_by<
          boost::multi_index::hashed_unique<
              boost::multi_index::tag<BySecurity>,
              boost::multi_index::const_mem_fun<Target,
                                                const Security *,
                                                &Target::GetSecurityPtr>>,
          boost::multi_index::hashed_unique<
              boost::multi_index::tag<BySymbol>,
              boost::multi_index::composite_key<
                  Target,
                  boost::multi_index::const_mem_fun<Target,
                                                    const TradingSystem *,
                                                    &Target::GetTradingSystem>,
                  boost::multi_index::const_mem_fun<Target,
                                                    const std::string &,
                                                    &Target::GetSymbol>>>>>
      TargetList;

  struct TargetWidgets {
    TargetTitleWidget title;
    TargetBidWidget bid;
    TargetAskWidget ask;
    TargetActionsWidget actions;

    explicit TargetWidgets(const QString &title, QWidget *parent)
        : title(parent), bid(parent), ask(parent), actions(parent) {
      this->title.SetTitle(title);
    }

    template <typename Source>
    void Update(const Source &source) {
      title.Update(source);
      bid.Update(source);
      ask.Update(source);
    }
  };

 public:
  explicit StrategyWindow(FrontEnd::Lib::Engine &,
                          const boost::optional<QString> &defaultSymbol,
                          QWidget *parent);
  virtual ~StrategyWindow() override;

 public:
  virtual QSize sizeHint() const override;

 protected:
  virtual void closeEvent(QCloseEvent *) override;

 private slots:
  void TakeAdvice(const trdk::Strategies::ArbitrageAdvisor::Advice &);
  void OnCurrentSymbolChange(int symbolIndex);

  void ToggleAutoTrading(bool activate);
  void DeactivateAutoTrading();
  void UpdateAutoTradingLevel(double level);

  void UpdateAdviceLevel(double level);

 signals:
  void Advice(const trdk::Strategies::ArbitrageAdvisor::Advice &);

 private:
  void ConnectSignals();
  void LoadSymbolList();
  void InitBySelectedSymbol();

  void SendOrder(const OrderSide &, TradingSystem *);

  bool IsAutoTradingActivated() const;

  void AddTargetWidgets(TargetWidgets &);

 private:
  Ui::StrategyWindow m_ui;
  boost::unordered_map<TradingSystem *, std::unique_ptr<TargetWidgets>>
      m_targetWidgets;
  FrontEnd::Lib::PriceAdapter<QLabel> m_bestSpreadAbsValue;

  FrontEnd::Lib::Engine &m_engine;
  const TradingMode m_tradingMode;
  const size_t m_instanceNumber;
  int m_symbolIndex;

  Strategy *m_strategy;
  boost::signals2::scoped_connection m_adviceConnection;

  TargetList m_targets;
  TradingSystem *m_bestBuyTradingSystem;
  TradingSystem *m_bestSellTradingSystem;
};
}
}
}