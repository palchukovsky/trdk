/*******************************************************************************
 *   Created: 2017/09/29 09:41:07
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 ******************************************************************************/

#pragma once

#include "Core/DropCopy.hpp"
#include "Api.h"

namespace trdk {
namespace FrontEnd {
namespace Lib {

class TRDK_FRONTEND_LIB_API DropCopy : public QObject, public trdk::DropCopy {
  Q_OBJECT

 public:
  explicit DropCopy(QObject *parent);
  virtual ~DropCopy() override = default;

 signals:
  void PriceUpdate(const Security *);

  void FreeOrderSubmit(const trdk::OrderId &,
                       const boost::posix_time::ptime &,
                       const trdk::Security *,
                       const trdk::Lib::Currency &,
                       const trdk::TradingSystem *,
                       const trdk::OrderSide &,
                       const trdk::Qty &,
                       const boost::optional<trdk::Price> &,
                       const trdk::TimeInForce &);
  void OperationOrderSubmit(const boost::uuids::uuid &operationId,
                            int64_t subOperationId,
                            const trdk::OrderId &,
                            const boost::posix_time::ptime &,
                            const trdk::Security *,
                            const trdk::Lib::Currency &,
                            const trdk::TradingSystem *,
                            const trdk::OrderSide &,
                            const trdk::Qty &,
                            const boost::optional<trdk::Price> &,
                            const trdk::TimeInForce &);
  void FreeOrderSubmitError(const boost::posix_time::ptime &,
                            const trdk::Security *,
                            const trdk::Lib::Currency &,
                            const trdk::TradingSystem *,
                            const trdk::OrderSide &,
                            const trdk::Qty &,
                            const boost::optional<trdk::Price> &,
                            const trdk::TimeInForce &,
                            const QString &error);
  void OperationOrderSubmitError(const boost::uuids::uuid &operationId,
                                 int64_t subOperationId,
                                 const boost::posix_time::ptime &,
                                 const trdk::Security *,
                                 const trdk::Lib::Currency &,
                                 const trdk::TradingSystem *,
                                 const trdk::OrderSide &,
                                 const trdk::Qty &,
                                 const boost::optional<trdk::Price> &,
                                 const trdk::TimeInForce &,
                                 const QString &error);
  void OrderUpdate(const trdk::OrderId &,
                   const trdk::TradingSystem *,
                   const boost::posix_time::ptime &,
                   const trdk::OrderStatus &,
                   const trdk::Qty &remainingQty);
  void Order(const trdk::OrderId &,
             const trdk::TradingSystem *,
             const std::string &symbol,
             const trdk::OrderStatus &,
             const trdk::Qty &qty,
             const trdk::Qty &remainingQty,
             const boost::optional<trdk::Price> &,
             const trdk::OrderSide &,
             const trdk::TimeInForce &,
             const boost::posix_time::ptime &openTime,
             const boost::posix_time::ptime &updateTime);

  void BalanceUpdate(const trdk::TradingSystem *,
                     const std::string &symbol,
                     const trdk::Volume &available,
                     const trdk::Volume &locked);

  void OperationStart(const boost::uuids::uuid &,
                      const boost::posix_time::ptime &,
                      const trdk::Strategy *);
  void OperationUpdate(const boost::uuids::uuid &, const trdk::Pnl::Data &);
  void OperationEnd(const boost::uuids::uuid &,
                    const boost::posix_time::ptime &,
                    const boost::shared_ptr<const trdk::Pnl> &);

 public:
  //! Tries to flush buffered Drop Copy data.
  /** The method doesn't guarantee to store all records, it just initiates
   * new send attempt. Synchronous. Can be interrupted from another
   * thread.
   */
  virtual void Flush() override;

  //! Dumps all buffer data and removes it from buffer.
  virtual void Dump() override;

 public:
  virtual trdk::DropCopyStrategyInstanceId RegisterStrategyInstance(
      const trdk::Strategy &) override;
  virtual trdk::DropCopyStrategyInstanceId ContinueStrategyInstance(
      const trdk::Strategy &, const boost::posix_time::ptime &) override;
  virtual trdk::DropCopyDataSourceInstanceId RegisterDataSourceInstance(
      const trdk::Strategy &,
      const boost::uuids::uuid &type,
      const boost::uuids::uuid &id) override;

  virtual void CopySubmittedOrder(const trdk::OrderId &,
                                  const boost::posix_time::ptime &,
                                  const trdk::Security &,
                                  const trdk::Lib::Currency &,
                                  const trdk::TradingSystem &,
                                  const trdk::OrderSide &,
                                  const trdk::Qty &,
                                  const boost::optional<trdk::Price> &,
                                  const trdk::TimeInForce &) override;
  virtual void CopySubmittedOrder(const trdk::OrderId &,
                                  const boost::posix_time::ptime &,
                                  const trdk::Position &,
                                  const trdk::OrderSide &,
                                  const trdk::Qty &,
                                  const boost::optional<trdk::Price> &,
                                  const trdk::TimeInForce &) override;
  virtual void CopyOrderSubmitError(const boost::posix_time::ptime &,
                                    const trdk::Security &,
                                    const trdk::Lib::Currency &,
                                    const trdk::TradingSystem &,
                                    const trdk::OrderSide &,
                                    const trdk::Qty &,
                                    const boost::optional<trdk::Price> &,
                                    const trdk::TimeInForce &,
                                    const std::string &error) override;
  virtual void CopyOrderSubmitError(const boost::posix_time::ptime &,
                                    const trdk::Position &,
                                    const trdk::OrderSide &,
                                    const trdk::Qty &,
                                    const boost::optional<trdk::Price> &,
                                    const trdk::TimeInForce &,
                                    const std::string &error) override;
  virtual void CopyOrderStatus(const trdk::OrderId &,
                               const trdk::TradingSystem &,
                               const boost::posix_time::ptime &,
                               const trdk::OrderStatus &,
                               const trdk::Qty &remainingQty) override;
  virtual void CopyOrder(const trdk::OrderId &,
                         const trdk::TradingSystem &,
                         const std::string &symbol,
                         const trdk::OrderStatus &,
                         const trdk::Qty &qty,
                         const trdk::Qty &remainingQty,
                         const boost::optional<trdk::Price> &,
                         const trdk::OrderSide &,
                         const trdk::TimeInForce &,
                         const boost::posix_time::ptime &openTime,
                         const boost::posix_time::ptime &updateTime) override;

  virtual void CopyTrade(
      const boost::posix_time::ptime &,
      const boost::optional<std::string> &tradingSystemTradeId,
      const trdk::OrderId &,
      const trdk::TradingSystem &,
      const trdk::Price &,
      const trdk::Qty &) override;

  virtual void CopyOperationStart(const boost::uuids::uuid &,
                                  const boost::posix_time::ptime &,
                                  const trdk::Strategy &) override;
  virtual void CopyOperationUpdate(const boost::uuids::uuid &,
                                   const trdk::Pnl::Data &) override;
  virtual void CopyOperationEnd(const boost::uuids::uuid &,
                                const boost::posix_time::ptime &,
                                std::unique_ptr<trdk::Pnl> &&) override;

  virtual void CopyBook(const trdk::Security &,
                        const trdk::PriceBook &) override;

  virtual void CopyBar(const trdk::DropCopyDataSourceInstanceId &,
                       size_t index,
                       const boost::posix_time::ptime &,
                       const trdk::Price &open,
                       const trdk::Price &high,
                       const trdk::Price &low,
                       const trdk::Price &close) override;

  virtual void CopyAbstractData(const trdk::DropCopyDataSourceInstanceId &,
                                size_t index,
                                const boost::posix_time::ptime &,
                                const trdk::Lib::Double &value) override;

  virtual void CopyLevel1(const trdk::Security &,
                          const boost::posix_time::ptime &,
                          const trdk::Level1TickValue &) override;
  virtual void CopyLevel1(const trdk::Security &,
                          const boost::posix_time::ptime &,
                          const trdk::Level1TickValue &,
                          const trdk::Level1TickValue &) override;
  virtual void CopyLevel1(const trdk::Security &,
                          const boost::posix_time::ptime &,
                          const trdk::Level1TickValue &,
                          const trdk::Level1TickValue &,
                          const trdk::Level1TickValue &) override;
  virtual void CopyLevel1(const trdk::Security &,
                          const boost::posix_time::ptime &,
                          const trdk::Level1TickValue &,
                          const trdk::Level1TickValue &,
                          const trdk::Level1TickValue &,
                          const trdk::Level1TickValue &) override;
  virtual void CopyLevel1(const trdk::Security &,
                          const boost::posix_time::ptime &,
                          const std::vector<trdk::Level1TickValue> &) override;

  virtual void CopyBalance(const trdk::TradingSystem &,
                           const std::string &symbol,
                           const trdk::Volume &available,
                           const trdk::Volume &locked) override;

 private:
  void SignalPriceUpdate(const Security &);

 private:
  const boost::posix_time::time_duration m_pollingInterval;
  boost::posix_time::ptime m_lastSignalTime;
};
}  // namespace Lib
}  // namespace FrontEnd
}  // namespace trdk