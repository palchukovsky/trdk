/*******************************************************************************
 *   Created: 2018/01/21 16:12:09
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 ******************************************************************************/

#pragma once

#include "Api.h"
#include "Fwd.hpp"

namespace trdk {
namespace FrontEnd {

class TRDK_FRONTEND_LIB_API BalanceListView : public QTreeView {
  Q_OBJECT

 public:
  typedef QTreeView Base;

 public:
  explicit BalanceListView(QWidget *parent);

 private:
  void InitContextMenu();
  void CopySelectedValuesToClipboard();
  virtual void rowsInserted(const QModelIndex &, int start, int end) override;
};
}  // namespace FrontEnd
}  // namespace trdk
