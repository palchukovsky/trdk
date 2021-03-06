/*******************************************************************************
 *   Created: 2018/01/28 05:05:52
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 ******************************************************************************/

#include "Prec.hpp"
#include "BalanceItem.hpp"
#include <utility>

using namespace trdk;
using namespace FrontEnd::Detail;

////////////////////////////////////////////////////////////////////////////////

BalanceRecord::BalanceRecord(const std::string &symbol,
                             const Volume &available,
                             const Volume &locked,
                             const bool isUsed)
    : symbol(QString::fromStdString(symbol)),
      available(available),
      locked(locked),
      time(QDateTime::currentDateTime().time()),
      isUsed(isUsed) {}

void BalanceRecord::Update(const Volume &newAvailable,
                           const Volume &newLocked) {
  available = newAvailable;
  locked = newLocked;
  time = QDateTime::currentDateTime().time();
}

Volume BalanceRecord::GetTotal() const { return available + locked; }

////////////////////////////////////////////////////////////////////////////////

void BalanceItem::AppendChild(boost::shared_ptr<BalanceItem> child) {
  Assert(!child->m_parent);
  AssertEq(-1, child->m_row);
  m_childItems.push_back(std::move(child));
  m_childItems.back()->m_parent = this;
  m_childItems.back()->m_row = static_cast<int>(m_childItems.size()) - 1;
}

int BalanceItem::GetRow() const {
  AssertLe(0, m_row);
  return m_row;
}

int BalanceItem::GetNumberOfChildren() const {
  return static_cast<int>(m_childItems.size());
}

BalanceItem *BalanceItem::GetChild(const int row) {
  AssertGt(static_cast<int>(m_childItems.size()), row);
  if (static_cast<int>(m_childItems.size()) <= row) {
    return nullptr;
  }
  return &*m_childItems[row];
}

BalanceItem *BalanceItem::GetParent() { return m_parent; }
const BalanceItem *BalanceItem::GetParent() const { return m_parent; }

bool BalanceItem::HasEmpty() const {
  for (const auto &child : m_childItems) {
    if (child->HasEmpty()) {
      return true;
    }
  }
  return false;
}

bool BalanceItem::HasLocked() const {
  for (const auto &child : m_childItems) {
    if (child->HasLocked()) {
      return true;
    }
  }
  return false;
}

bool BalanceItem::IsUsed() const {
  for (const auto &child : m_childItems) {
    if (child->IsUsed()) {
      return true;
    }
  }
  return false;
}

////////////////////////////////////////////////////////////////////////////////

BalanceTradingSystemItem::BalanceTradingSystemItem(
    const TradingSystem &tradingSystem)
    : m_tradingSystem(tradingSystem),
      m_data(QString::fromStdString(m_tradingSystem.GetTitle())) {}

const TradingSystem &BalanceTradingSystemItem::GetTradingSystem() const {
  return m_tradingSystem;
}

QVariant BalanceTradingSystemItem::GetData(const int column) const {
  static_assert(numberOfBalanceColumns == 6, "List changed.");
  switch (column) {
    case BALANCE_COLUMN_EXCHANGE_OR_SYMBOL:
      return m_data;
    default:
      return {};
  }
}

////////////////////////////////////////////////////////////////////////////////

BalanceDataItem::BalanceDataItem(boost::shared_ptr<BalanceRecord> data)
    : m_data(std::move(data)) {}

BalanceRecord &BalanceDataItem::GetRecord() { return *m_data; }
const BalanceRecord &BalanceDataItem::GetRecord() const { return *m_data; }

QVariant BalanceDataItem::GetData(const int column) const {
  static_assert(numberOfBalanceColumns == 6, "List changed.");
  switch (column) {
    case BALANCE_COLUMN_EXCHANGE_OR_SYMBOL:
      return m_data->symbol;
    case BALANCE_COLUMN_AVAILABLE:
      return m_data->available;
    case BALANCE_COLUMN_LOCKED:
      return m_data->locked;
    case BALANCE_COLUMN_TOTAL:
      return m_data->GetTotal().Get();
    case BALANCE_COLUMN_UPDATE_TIME:
      return m_data->time;
    case BALANCE_COLUMN_USED:
      if (m_data->isUsed) {
        return QObject::tr("yes");
      }
      return {};
    default:
      return {};
  }
}

bool BalanceDataItem::HasEmpty() const {
  return (IsUsed() && m_data->GetTotal() < 0.0001) || Base::HasEmpty();
}

bool BalanceDataItem::HasLocked() const {
  return (IsUsed() && m_data->locked) || Base::HasLocked();
}

bool BalanceDataItem::IsUsed() const {
  return m_data->isUsed || Base::IsUsed();
}

////////////////////////////////////////////////////////////////////////////////
