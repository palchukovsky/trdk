/**************************************************************************
 *   Created: 2012/07/09 17:32:04
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#pragma once

#include "Security.hpp"
#include "Fwd.hpp"
#include "Api.h"

namespace trdk {

	//////////////////////////////////////////////////////////////////////////

	class TRDK_CORE_API Position
			: private boost::noncopyable,
			public boost::enable_shared_from_this<trdk::Position> {

	public:

		typedef void (StateUpdateSlotSignature)();
		typedef boost::function<StateUpdateSlotSignature> StateUpdateSlot;
		typedef boost::signals2::connection StateUpdateConnection;

		typedef boost::posix_time::ptime Time;

		enum Type {
			TYPE_LONG,
			TYPE_SHORT,
			numberOfTypes
		};

		enum CloseType {
			CLOSE_TYPE_NONE,
			CLOSE_TYPE_TAKE_PROFIT,
			CLOSE_TYPE_STOP_LOSS,
			CLOSE_TYPE_TIMEOUT,
			CLOSE_TYPE_SCHEDULE,
			CLOSE_TYPE_ENGINE_STOP
		};

		class TRDK_CORE_API LogicError : public trdk::Lib::LogicError {
		public:
			explicit LogicError(const char *what) throw();
		};
		
		class TRDK_CORE_API AlreadyStartedError : public LogicError {
		public:
			AlreadyStartedError() throw();
		};
		class TRDK_CORE_API NotOpenedError : public LogicError {
		public:
			NotOpenedError() throw();
		};
		
		class TRDK_CORE_API AlreadyClosedError : public LogicError {
		public:
			AlreadyClosedError() throw();
		};

	public:

		explicit Position(
				trdk::Strategy &,
				trdk::Qty,
				trdk::ScaledPrice startPrice);
		virtual ~Position();

	public:

		virtual Type GetType() const = 0;
		virtual const std::string & GetTypeStr() const throw() = 0;

	public:

		const trdk::Security & GetSecurity() const throw();
		trdk::Security & GetSecurity() throw();

	public:

		CloseType GetCloseType() const throw();
		const std::string & GetCloseTypeStr() const;

		//! Has opened qty and hasn't active open-orders.
		bool IsOpened() const throw();
		//! Has opened qty and the same closed qty. Hasn't active close-orders.
		bool IsClosed() const throw();

		//! Started - one of open-orders sent.
		/** @sa	IsCompleted
		  */
		bool IsStarted() const throw();
		//! Started and now hasn't any orders and active qty.
		/** @sa	IsStarted
		  */
		bool IsCompleted() const throw();

		//! Open operation started, but error occurred at opening or closing.
		bool IsError() const throw();
		//! All orders canceled, position will be closed or already closed.
		bool IsCanceled() const throw();

		bool HasActiveOrders() const throw();
		bool HasActiveOpenOrders() const throw();
		bool HasActiveCloseOrders() const throw();

	public:

		trdk::Qty GetPlanedQty() const;
		trdk::ScaledPrice GetOpenStartPrice() const;

		trdk::OrderId GetOpenOrderId() const throw();
		trdk::Qty GetOpenedQty() const throw();
		trdk::ScaledPrice GetOpenPrice() const;
		Time GetOpenTime() const;

		trdk::Qty GetNotOpenedQty() const;
		trdk::Qty GetActiveQty() const throw();

		trdk::OrderId GetCloseOrderId() const throw();
		void SetCloseStartPrice(trdk::ScaledPrice);
		trdk::ScaledPrice GetCloseStartPrice() const;
		trdk::ScaledPrice GetClosePrice() const;
		trdk::Qty GetClosedQty() const throw();
		Time GetCloseTime() const;

		trdk::ScaledPrice GetCommission() const;

	public:

		trdk::OrderId OpenAtMarketPrice();
		trdk::OrderId Open(trdk::ScaledPrice);
		trdk::OrderId OpenAtMarketPriceWithStopPrice(
					trdk::ScaledPrice stopPrice);
		trdk::OrderId OpenOrCancel(trdk::ScaledPrice);

		trdk::OrderId CloseAtMarketPrice(CloseType);
		trdk::OrderId Close(CloseType, trdk::ScaledPrice);
		trdk::OrderId CloseAtMarketPriceWithStopPrice(
					CloseType,
					trdk::ScaledPrice stopPrice);
		trdk::OrderId CloseOrCancel(CloseType, trdk::ScaledPrice);

		bool CancelAtMarketPrice(CloseType);
		bool CancelAllOrders();

	public:

		StateUpdateConnection Subscribe(const StateUpdateSlot &) const;

		virtual trdk::Security::OrderStatusUpdateSlot
		GetSellOrderStatusUpdateSlot() = 0;
		
		virtual trdk::Security::OrderStatusUpdateSlot
		GetBuyOrderStatusUpdateSlot() = 0;

	protected:

		virtual trdk::OrderId DoOpenAtMarketPrice(trdk::Qty) = 0;
		virtual trdk::OrderId DoOpen(trdk::Qty, trdk::ScaledPrice) = 0;
		virtual trdk::OrderId DoOpenAtMarketPriceWithStopPrice(
					trdk::Qty,
					trdk::ScaledPrice stopPrice)
				= 0;
		virtual trdk::OrderId DoOpenOrCancel(
					trdk::Qty,
					trdk::ScaledPrice)
				= 0;

		virtual trdk::OrderId DoCloseAtMarketPrice(trdk::Qty) = 0;
		virtual trdk::OrderId DoClose(trdk::Qty, trdk::ScaledPrice) = 0;
		virtual trdk::OrderId DoCloseAtMarketPriceWithStopPrice(
					trdk::Qty,
					trdk::ScaledPrice stopPrice)
				= 0;
		virtual trdk::OrderId DoCloseOrCancel(
					trdk::Qty,
					trdk::ScaledPrice)
				= 0;

	protected:

		void UpdateOpening(
					trdk::OrderId,
					trdk::TradeSystem::OrderStatus,
					trdk::Qty filled,
					trdk::Qty remaining,
					double avgPrice,
					double lastPrice);
		void UpdateClosing(
					trdk::OrderId,
					trdk::TradeSystem::OrderStatus,
					trdk::Qty filled,
					trdk::Qty remaining,
					double avgPrice,
					double lastPrice);

	private:

		class Implementation;
		Implementation *m_pimpl;

	};

	//////////////////////////////////////////////////////////////////////////

	class TRDK_CORE_API LongPosition : public Position {

	public:

		explicit LongPosition(
				trdk::Strategy &,
				trdk::Qty,
				trdk::ScaledPrice startPrice);
		virtual ~LongPosition();

	public:

		virtual Type GetType() const;
		virtual const std::string & GetTypeStr() const throw();

	public:

		virtual trdk::Security::OrderStatusUpdateSlot
		GetSellOrderStatusUpdateSlot();
		
		virtual trdk::Security::OrderStatusUpdateSlot
		GetBuyOrderStatusUpdateSlot();

	protected:

		virtual trdk::OrderId DoOpenAtMarketPrice(trdk::Qty);
		virtual trdk::OrderId DoOpen(trdk::Qty, trdk::ScaledPrice);
		virtual trdk::OrderId DoOpenAtMarketPriceWithStopPrice(
					trdk::Qty,
					trdk::ScaledPrice stopPrice);
		virtual trdk::OrderId DoOpenOrCancel(
					trdk::Qty,
					trdk::ScaledPrice);

		virtual trdk::OrderId DoCloseAtMarketPrice(trdk::Qty);
		virtual trdk::OrderId DoClose(trdk::Qty, trdk::ScaledPrice);
		virtual trdk::OrderId DoCloseAtMarketPriceWithStopPrice(
					trdk::Qty,
					trdk::ScaledPrice stopPrice);
		virtual trdk::OrderId DoCloseOrCancel(
					trdk::Qty,
					trdk::ScaledPrice);

	};

	//////////////////////////////////////////////////////////////////////////

	class TRDK_CORE_API ShortPosition : public Position {

	public:

		explicit ShortPosition(
				trdk::Strategy &,
				trdk::Qty,
				trdk::ScaledPrice startPrice);
		virtual ~ShortPosition();

	public:

		virtual Type GetType() const;
		virtual const std::string & GetTypeStr() const throw();

	public:

		virtual trdk::Security::OrderStatusUpdateSlot
		GetSellOrderStatusUpdateSlot();
		
		virtual trdk::Security::OrderStatusUpdateSlot
		GetBuyOrderStatusUpdateSlot();

	protected:

		virtual trdk::OrderId DoOpenAtMarketPrice(trdk::Qty);
		virtual trdk::OrderId DoOpen(trdk::Qty, trdk::ScaledPrice);
		virtual trdk::OrderId DoOpenAtMarketPriceWithStopPrice(
					trdk::Qty,
					trdk::ScaledPrice stopPrice);
		virtual trdk::OrderId DoOpenOrCancel(
					trdk::Qty,
					trdk::ScaledPrice);

		virtual trdk::OrderId DoCloseAtMarketPrice(trdk::Qty);
		virtual trdk::OrderId DoClose(trdk::Qty, trdk::ScaledPrice);
		virtual trdk::OrderId DoCloseAtMarketPriceWithStopPrice(
					trdk::Qty,
					trdk::ScaledPrice stopPrice);
		virtual trdk::OrderId DoCloseOrCancel(
					trdk::Qty,
					trdk::ScaledPrice);

	};

	//////////////////////////////////////////////////////////////////////////

}
