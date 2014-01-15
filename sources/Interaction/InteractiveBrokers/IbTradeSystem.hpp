/**************************************************************************
 *   Created: May 26, 2012 9:44:37 PM
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#pragma once

#include "IbSecurity.hpp"
#include "Core/TradeSystem.hpp"
#include "Core/MarketDataSource.hpp"
#include "Core/Context.hpp"
#include "Fwd.hpp"

namespace trdk {  namespace Interaction { namespace InteractiveBrokers {

	class Client;

} } }

namespace trdk {  namespace Interaction { namespace InteractiveBrokers {

	class TradeSystem
		: public trdk::TradeSystem,
		public trdk::MarketDataSource {

		friend class trdk::Interaction::InteractiveBrokers::Client;

	public:

		typedef std::set<Security *> Securities;

	private:

		typedef boost::mutex OrdersMutex;
		typedef OrdersMutex::scoped_lock OrdersWriteLock;
		typedef OrdersMutex::scoped_lock OrdersReadLock;

		typedef boost::mutex PositionsMutex;
		typedef PositionsMutex::scoped_lock PositionsReadLock;
		typedef PositionsMutex::scoped_lock PositionsWriteLock;
		typedef std::map<std::string, Qty> Positions;

		struct PlacedOrder {
			OrderId id;
			trdk::Security *security;
			OrderStatusUpdateSlot callback;
			bool commission;
			bool completed;
		};

		struct BySymbol {
			//...//
		};
		struct ByOrder {
			//...//
		};
		struct BySymbolAndOrder {
			//...//
		};

		typedef boost::multi_index_container<
			PlacedOrder,
			boost::multi_index::indexed_by<
				boost::multi_index::ordered_non_unique<
					boost::multi_index::tag<BySymbol>,
					boost::multi_index::member<
						PlacedOrder,
						trdk::Security *,
						&PlacedOrder::security>>,
				boost::multi_index::ordered_unique<
					boost::multi_index::tag<ByOrder>,
					boost::multi_index::member<
						PlacedOrder,
						TradeSystem::OrderId,
						&PlacedOrder::id>>>>
			PlacedOrderSet;

	public:

		explicit TradeSystem(const Lib::IniSectionRef &, Context::Log &);
		virtual ~TradeSystem();

	public:

		virtual void Connect(const trdk::Lib::IniSectionRef &);

	public:

		virtual const Account & GetAccount() const;

		virtual trdk::TradeSystem::Position GetBrokerPostion(
				const trdk::Lib::Symbol &)
			const;

	public:

		virtual OrderId SellAtMarketPrice(
				trdk::Security &,
				trdk::Qty qty,
				const trdk::OrderParams &,
				const OrderStatusUpdateSlot &);
		virtual OrderId Sell(
				trdk::Security &,
				trdk::Qty qty,
				trdk::ScaledPrice,
				const trdk::OrderParams &,
				const OrderStatusUpdateSlot &);
		virtual OrderId SellAtMarketPriceWithStopPrice(
				trdk::Security &,
				trdk::Qty qty,
				trdk::ScaledPrice stopPrice,
				const trdk::OrderParams &,
				const OrderStatusUpdateSlot &);
		virtual OrderId SellOrCancel(
				trdk::Security &,
				trdk::Qty,
				trdk::ScaledPrice,
				const trdk::OrderParams &,
				const OrderStatusUpdateSlot &);

		virtual OrderId BuyAtMarketPrice(
				trdk::Security &,
				trdk::Qty qty,
				const trdk::OrderParams &,
				const OrderStatusUpdateSlot &);
		virtual OrderId Buy(
				trdk::Security &,
				trdk::Qty qty,
				trdk::ScaledPrice,
				const trdk::OrderParams &,
				const OrderStatusUpdateSlot &);
		virtual OrderId BuyAtMarketPriceWithStopPrice(
				trdk::Security &,
				trdk::Qty qty,
				trdk::ScaledPrice stopPrice,
				const trdk::OrderParams &,
				const OrderStatusUpdateSlot &);
		virtual OrderId BuyOrCancel(
				trdk::Security &,
				trdk::Qty,
				trdk::ScaledPrice,
				const trdk::OrderParams &,
				const OrderStatusUpdateSlot &);

		virtual void CancelOrder(OrderId);
		virtual void CancelAllOrders(trdk::Security &);

	protected:

		virtual boost::shared_ptr<trdk::Security> CreateSecurity(
				trdk::Context &,
				const trdk::Lib::Symbol &)
			const;

	private:

		void RegOrder(const PlacedOrder &);

	private:

		Context::Log &m_log;

		const bool m_isTestSource;

		OrdersMutex m_ordersMutex;
		std::unique_ptr<Client> m_client;
		PlacedOrderSet m_placedOrders;

		mutable Securities m_securities;

		std::unique_ptr<Account> m_account;

		mutable PositionsMutex m_positionsMutex;
		std::unique_ptr<Positions> m_positions;

	};

} } }
