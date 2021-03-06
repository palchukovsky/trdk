/*******************************************************************************
 *   Created: 2018/01/24 23:17:47
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

class TRDK_FRONTEND_LIB_API OperationListView : public QTreeView {
  Q_OBJECT

 public:
  typedef QTreeView Base;

  explicit OperationListView(QWidget* parent);
  ~OperationListView() override = default;

 private slots:
  void CopySelectedValuesToClipboard();

 protected:
  void rowsInserted(const QModelIndex&, int start, int end) override;

 private:
  void InitContextMenu();
  void FollowNewRecords(bool isEnabled);

  bool m_isFollowingEnabled;
  QAction* m_followNewOperationsAction;
  bool m_isExpandingEnabled;
  size_t m_numberOfResizesForOperations;
  size_t m_numberOfResizesForOrder;
};
}  // namespace FrontEnd
}  // namespace trdk
