/*******************************************************************************
 *   Created: 2017/11/01 21:44:28
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 ******************************************************************************/

#include "Prec.hpp"
#include "SortFilterProxyModel.hpp"

using namespace trdk::Lib;
using namespace trdk::FrontEnd;

bool SortFilterProxyModel::lessThan(const QModelIndex& left,
                                    const QModelIndex& right) const {
  const auto& leftData = sourceModel()->data(left);
  const auto& rightData = sourceModel()->data(right);
  if (leftData.isNull() || rightData.isNull()) {
    return (leftData.isNull() ? 0 : 1) < (rightData.isNull() ? 0 : 1);
  }
  switch (leftData.type()) {
    case QVariant::DateTime:
      return leftData.toDateTime() < rightData.toDateTime();
    case QVariant::String:
      return QString::localeAwareCompare(leftData.toString(),
                                         rightData.toString()) < 0;
    case QVariant::Bool:
      return leftData.toBool() < rightData.toBool();
    case QVariant::Int:
      return leftData.toInt() < rightData.toInt();
    case QVariant::UInt:
      return leftData.toUInt() < rightData.toUInt();
    case QVariant::LongLong:
      return leftData.toLongLong() < rightData.toLongLong();
    case QVariant::ULongLong:
      return leftData.toULongLong() < rightData.toULongLong();
    case QVariant::Double:
      return Double(leftData.toDouble()) < Double(rightData.toDouble());
    default:
      return false;
  }
}