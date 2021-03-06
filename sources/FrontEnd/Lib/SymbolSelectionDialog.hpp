/*******************************************************************************
 *   Created: 2018/01/31 23:47:33
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 ******************************************************************************/

#pragma once

namespace Ui {
class SymbolSelectionDialog;
}

namespace trdk {
namespace FrontEnd {

class TRDK_FRONTEND_LIB_API SymbolSelectionDialog : public QDialog {
  Q_OBJECT
 public:
  typedef QDialog Base;

  explicit SymbolSelectionDialog(Engine &, QWidget *parent);
  ~SymbolSelectionDialog();

  std::vector<QString> RequestSymbols();

 private:
  void UpdateList();

  std::vector<QString> m_symbols;
  std::unique_ptr<Ui::SymbolSelectionDialog> m_ui;
};
}  // namespace FrontEnd
}  // namespace trdk
