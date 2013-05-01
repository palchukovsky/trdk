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

#include "Core/TradeSystem.hpp"
#include "Core/MarketDataSource.hpp"
#include "Core/Context.hpp"
#include "Fwd.hpp"

namespace trdk {  namespace Interaction { namespace InteractiveBrokers {

	class TradeSystem
		: public trdk::TradeSystem,
		public trdk::MarketDataSource {

	private:

		typedef boost::shared_mutex Mutex;
		typedef boost::unique_lock<Mutex> WriteLock;
		typedef boost::shared_lock<Mutex> ReadLock;

		struct PlacedOrder {
			OrderId id;
			std::string symbol;
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
						std::string,
						&PlacedOrder::symbol>>,
				boost::multi_index::hashed_unique<
					boost::multi_index::tag<ByOrder>,
					boost::multi_index::member<
						PlacedOrder,
						TradeSystem::OrderId,
						&PlacedOrder::id>>>>
			PlacedOrderSet;
		typedef PlacedOrderSet::index<BySymbol>::type PlacedOrderBySymbol;
		typedef PlacedOrderSet::index<ByOrder>::type PlacedOrderById;

	public:

		explicit TradeSystem(const Lib::IniFileSectionRef &, Context::Log &);
		virtual ~TradeSystem();

	public:

		virtual void Connect(const trdk::Lib::IniFileSectionRef &);

	public:

		virtual boost::shared_ptr<trdk::Security> CreateSecurity(
				trdk::Context &,
				const std::string &symbol,
				const std::string &primaryExchange,
				const std::string &exchange,
				bool logMarketData)
			const;

	public:

		virtual OrderId SellAtMarketPrice(
				trdk::Security &,
				trdk::Qty,
				const OrderStatusUpdateSlot &);
		virtual OrderId Sell(
				trdk::Security &,
				trdk::Qty,
				trdk::ScaledPrice,
				const OrderStatusUpdateSlot &);
		virtual OrderId SellAtMarketPriceWithStopPrice(
				trdk::Security &,
				trdk::Qty,
				trdk::ScaledPrice stopPrice,
				const OrderStatusUpdateSlot &);
		virtual OrderId SellOrCancel(
				trdk::Security &,
				trdk::Qty,
				trdk::ScaledPrice,
				const OrderStatusUpdateSlot &);

		virtual OrderId BuyAtMarketPrice(
				trdk::Security &,
				trdk::Qty,
				const OrderStatusUpdateSlot &);
		virtual OrderId Buy(
				trdk::Security &,
				trdk::Qty,
				trdk::ScaledPrice,
				const OrderStatusUpdateSlot &);
		virtual OrderId BuyAtMarketPriceWithStopPrice(
				trdk::Security &,
				trdk::Qty,
				trdk::ScaledPrice stopPrice,
				const OrderStatusUpdateSlot &);
		virtual OrderId BuyOrCancel(
				trdk::Security &,
				trdk::Qty,
				trdk::ScaledPrice,
				const OrderStatusUpdateSlot &);

		virtual void CancelOrder(OrderId);
		virtual void CancelAllOrders(trdk::Security &);

	private:

		void RegOrder(const PlacedOrder &);

	private:

		Context::Log &m_log;

		Mutex m_mutex;
		std::unique_ptr<Client> m_client;
		PlacedOrderSet m_placedOrders;

	};

} } }
