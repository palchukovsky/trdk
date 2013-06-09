/**************************************************************************
 *   Created: 2012/07/23 23:49:34
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#include "Prec.hpp"
#include "TradeSystem.hpp"
#include "Core/Security.hpp"

using namespace trdk;
using namespace trdk::Lib;
using namespace trdk::Interaction;
using namespace trdk::Interaction::Fake;

/////////////////////////////////////////////////////////////////////////

namespace {

	const std::string sellLogTag = "sell";
	const std::string buyLogTag = "buy";

	struct Order {
		Security *security;
		bool isSell;
		OrderId id;
		Fake::TradeSystem::OrderStatusUpdateSlot callback;
		Qty qty;
		ScaledPrice price;
	};

}

//////////////////////////////////////////////////////////////////////////

class Fake::TradeSystem::Implementation : private boost::noncopyable {

private:

	typedef boost::mutex Mutex;
	typedef Mutex::scoped_lock Lock;
	typedef boost::condition_variable Condition;

	typedef std::list<Order> Orders;

public:

	Implementation(Context::Log &log)
			: m_log(log),
			m_isStarted(0),
			m_currentOrders(&m_orders1) {
		Interlocking::Exchange(m_id, 0);
	}

	~Implementation() {
		if (m_isStarted) {
			m_log.Debug("Stopping Fake Trade System task...");
			{
				const Lock lock(m_mutex);
				m_isStarted = false;
				m_condition.notify_all();
			}
			m_thread.join();
		}
	}

public:

	Context::Log & GetLog() {
		return m_log;
	}

	OrderId TakeOrderId() {
		return Interlocking::Increment(m_id);
	}

	void Start() {
		Lock lock(m_mutex);
		Assert(!m_isStarted);
		if (m_isStarted) {
			return;
		}
		m_isStarted = true;
		boost::thread(boost::bind(&Implementation::Task, this)).swap(m_thread);
		m_condition.wait(lock);
	}

	void SendOrder(const Order &order) {
		if (!order.callback) {
			return;
		}
		const Lock lock(m_mutex);
		m_currentOrders->push_back(order);
		m_condition.notify_all();
	}

private:

	void Task() {
		try {
			{
				Lock lock(m_mutex);
				m_condition.notify_all();
			}
			m_log.Info("Stated Fake Trade System task...");
			for ( ; ; ) {
				Orders *orders = nullptr;
				{
					Lock lock(m_mutex);
					if (!m_isStarted) {
						break;
					}
					if (m_currentOrders->empty()) {
						m_condition.wait(lock);
					}
					if (!m_isStarted) {
						break;
					}
					if (!m_currentOrders) {
						continue;
					}
					orders = m_currentOrders;
					m_currentOrders = orders == &m_orders1
						? &m_orders2
						: &m_orders1;
				}
				Assert(!orders->empty());
				foreach (const Order &order, *orders) {
					Assert(order.callback);
					auto price = order.price
						?	order.security->DescalePrice(order.price)
						:	order.isSell
							?	order.security->GetAskPrice()
							:	order.security->GetBidPrice();
					if (IsZero(price)) {
						price = order.security->GetLastPrice();
					}
					Assert(!IsZero(price));
					order.callback(
						order.id,
						TradeSystem::ORDER_STATUS_FILLED,
						order.qty,
						0,
						price,
						price);
				}
				orders->clear();
			}
		} catch (...) {
			AssertFailNoException();
			throw;
		}
		m_log.Info("Fake Trade System stopped.");
	}

private:

	Context::Log &m_log;

	volatile long m_id;
	bool m_isStarted;

	Mutex m_mutex;
	Condition m_condition;
	boost::thread m_thread;

	Orders m_orders1;
	Orders m_orders2;
	Orders *m_currentOrders;


};

//////////////////////////////////////////////////////////////////////////

Fake::TradeSystem::TradeSystem(const IniFileSectionRef &, Context::Log &log)
		: m_pimpl(new Implementation(log)) {
	//...//
}

Fake::TradeSystem::~TradeSystem() {
	delete m_pimpl;
}

void Fake::TradeSystem::Connect(const IniFileSectionRef &) {
	m_pimpl->Start();
}

OrderId Fake::TradeSystem::SellAtMarketPrice(
			Security &security,
			Qty qty,
			Qty displaySize,
			const OrderStatusUpdateSlot &statusUpdateSlot) {
	UseUnused(displaySize);
	AssertLe(displaySize, qty);
	AssertLt(0, qty);
	AssertLt(0, displaySize);
	const Order order = {
		&security,
		true,
		m_pimpl->TakeOrderId(),
		statusUpdateSlot,
		qty};
	m_pimpl->SendOrder(order);
	m_pimpl->GetLog().Trading(
		sellLogTag,
		"%2% order-id=%1% qty=%3% price=market",
		boost::make_tuple(
			order.id,
			boost::cref(security.GetSymbol()),
			qty));
	return order.id;
}

OrderId Fake::TradeSystem::Sell(
			Security &,
			Qty qty,
			ScaledPrice,
			Qty displaySize,
			const OrderStatusUpdateSlot  &) {
	UseUnused(qty, displaySize);
	AssertLe(displaySize, qty);
	AssertLt(0, qty);
	AssertLt(0, displaySize);
	AssertFail("Doesn't implemented.");
	throw Exception("Method doesn't implemented");
}

OrderId Fake::TradeSystem::SellAtMarketPriceWithStopPrice(
			Security &,
			Qty qty,
			ScaledPrice /*stopPrice*/,
			Qty displaySize,
			const OrderStatusUpdateSlot  &) {
	UseUnused(qty, displaySize);
	AssertLe(displaySize, qty);
	AssertLt(0, qty);
	AssertLt(0, displaySize);
	AssertFail("Doesn't implemented.");
	throw Exception("Method doesn't implemented");
}

OrderId Fake::TradeSystem::SellOrCancel(
			Security &security,
			Qty qty,
			ScaledPrice price,
			const OrderStatusUpdateSlot &statusUpdateSlot) {
	const Order order = {
		&security,
		true,
		m_pimpl->TakeOrderId(),
		statusUpdateSlot,
		qty,
		price};
	m_pimpl->SendOrder(order);
	m_pimpl->GetLog().Trading(
		sellLogTag,
		"%2% order-id=%1% type=IOC qty=%3% price=%4%",
		boost::make_tuple(
			order.id,
			boost::cref(security.GetSymbol()),
			qty,
			security.DescalePrice(price)));
	return order.id;
}

OrderId Fake::TradeSystem::BuyAtMarketPrice(
			Security &security,
			Qty qty,
			Qty displaySize,
			const OrderStatusUpdateSlot &statusUpdateSlot) {
	UseUnused(displaySize);
	AssertLe(displaySize, qty);
	AssertLt(0, qty);
	AssertLt(0, displaySize);
	const Order order = {
		&security,
		false,
		m_pimpl->TakeOrderId(),
		statusUpdateSlot,
		qty};
	m_pimpl->SendOrder(order);
	m_pimpl->GetLog().Trading(
		buyLogTag,
		"%2% order-id=%1% qty=%3% price=market",
		boost::make_tuple(
			order.id,
			boost::cref(security.GetSymbol()),
			qty));
	return order.id;
}

OrderId Fake::TradeSystem::Buy(
			Security &,
			Qty qty,
			ScaledPrice,
			Qty displaySize,
			const OrderStatusUpdateSlot  &) {
	UseUnused(qty, displaySize);
	AssertLe(displaySize, qty);
	AssertLt(0, qty);
	AssertLt(0, displaySize);
	AssertFail("Doesn't implemented.");
	throw Exception("Method doesn't implemented");
}

OrderId Fake::TradeSystem::BuyAtMarketPriceWithStopPrice(
			Security &,
			Qty qty,
			ScaledPrice /*stopPrice*/,
			Qty displaySize,
			const OrderStatusUpdateSlot  &) {
	UseUnused(qty, displaySize);
	AssertLe(displaySize, qty);
	AssertLt(0, qty);
	AssertLt(0, displaySize);
	AssertFail("Doesn't implemented.");
	throw Exception("Method doesn't implemented");
}

OrderId Fake::TradeSystem::BuyOrCancel(
			Security &security,
			Qty qty,
			ScaledPrice price,
			const OrderStatusUpdateSlot &statusUpdateSlot) {
	const Order order = {
		&security,
		false,
		m_pimpl->TakeOrderId(),
		statusUpdateSlot,
		qty,
		price};
	m_pimpl->SendOrder(order);
	m_pimpl->GetLog().Trading(
		buyLogTag,
		"%2% order-id=%1% type=IOC qty=%3% price=%4%",
		boost::make_tuple(
			order.id,
			boost::cref(security.GetSymbol()),
			qty,
			security.DescalePrice(price)));
	return order.id;
}

void Fake::TradeSystem::CancelOrder(OrderId) {
	AssertFail("Doesn't implemented.");
	throw Exception("Method doesn't implemented");
}

void Fake::TradeSystem::CancelAllOrders(Security &security) {
	m_pimpl->GetLog().Trading("cancel", "%1% orders=[all]", security.GetSymbol());
}

//////////////////////////////////////////////////////////////////////////
