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
#include "FakeTradingSystem.hpp"
#include "Core/Security.hpp"
#include "Core/Settings.hpp"

namespace pt = boost::posix_time;

using namespace trdk;
using namespace trdk::Lib;
using namespace trdk::Interaction;
using namespace trdk::Interaction::Fake;

/////////////////////////////////////////////////////////////////////////

namespace {

	struct Order {
		Security *security;
		Currency currency;
		bool isSell;
		OrderId id;
		Fake::TradingSystem::OrderStatusUpdateSlot callback;
		Qty qty;
		ScaledPrice price;
		OrderParams params;
		pt::ptime submitTime;
		pt::time_duration execDelay;
	};

	typedef boost::shared_mutex SettingsMutex;
	typedef boost::shared_lock<SettingsMutex> SettingsReadLock;
	typedef boost::unique_lock<SettingsMutex> SettingsWriteLock;

}

//////////////////////////////////////////////////////////////////////////

class Fake::TradingSystem::Implementation : private boost::noncopyable {

public:

	TradingSystem *m_self;

	struct DelayGenerator {

		boost::mt19937 random;
		boost::uniform_int<size_t> executionDelayRange;
		mutable boost::variate_generator<boost::mt19937, boost::uniform_int<size_t>>
			executionDelayGenerator;
		boost::uniform_int<size_t> reportDelayRange;
		mutable boost::variate_generator<boost::mt19937, boost::uniform_int<size_t>>
			reportDelayGenerator;

		explicit DelayGenerator(const IniSectionRef &conf)
			: executionDelayRange(
				conf.ReadTypedKey<size_t>("delay_microseconds.execution.min"),
				conf.ReadTypedKey<size_t>("delay_microseconds.execution.max"))
			, executionDelayGenerator(random, executionDelayRange)
			, reportDelayRange(
				conf.ReadTypedKey<size_t>("delay_microseconds.report.min"),
				conf.ReadTypedKey<size_t>("delay_microseconds.report.max"))
			, reportDelayGenerator(random, reportDelayRange) {
			//...//
		}

		void Validate() const {
			if (
					executionDelayRange.min() > executionDelayRange.max()
					|| reportDelayRange.min() > reportDelayRange.max()) {
				throw ModuleError("Min delay can't be more then max delay");
			}
		}

		void Report(TradingSystem::Log &log) const {
			log.Info(
				"Execution delay range: %1% - %2%; Report delay range: %3% - %4%.",
				pt::microseconds(executionDelayRange.min()),
				pt::microseconds(executionDelayRange.max()),
				pt::microseconds(reportDelayRange.min()),
				pt::microseconds(reportDelayRange.max()));
		}

	};

	class ExecChanceGenerator {

	public:

		explicit ExecChanceGenerator(const IniSectionRef &conf)
			: m_executionProbability(
				conf.ReadTypedKey<uint16_t>("execution_probability")),
			m_range(0, 100),
			m_generator(m_random, m_range) {
			//...//
		}

		void Validate() const {
			if (m_executionProbability <= 0 || m_executionProbability > 100) {
				throw ModuleError(
					"Execution probability must be between 0% and 101%.");
			}
		}

		bool HasChance() const {
			return
				m_executionProbability >= 100
				|| m_generator() <= m_executionProbability;
		}

	private:

		uint16_t m_executionProbability;
		boost::mt19937 m_random;
		boost::uniform_int<uint8_t> m_range;
		mutable boost::variate_generator<boost::mt19937, boost::uniform_int<uint8_t>>
			m_generator;

	};

private:

	typedef boost::mutex Mutex;
	typedef Mutex::scoped_lock Lock;
	typedef boost::condition_variable Condition;

	typedef std::deque<Order> Orders;

public:

	DelayGenerator m_delayGenerator;
	ExecChanceGenerator m_execChanceGenerator;

	mutable SettingsMutex m_settingsMutex;

public:

	explicit Implementation(const IniSectionRef &conf)
		: m_delayGenerator(conf)
		, m_execChanceGenerator(conf)
		, m_id(1)
		, m_isStarted(0)
		, m_currentOrders(&m_orders1) {
		m_delayGenerator.Validate();
		m_execChanceGenerator.Validate();
	}

	~Implementation() {
		if (!m_self->GetContext().GetSettings().IsReplayMode()) {
			if (m_isStarted) {
				m_self->GetLog().Debug("Stopping Fake Trading System task...");
				{
					const Lock lock(m_mutex);
					m_isStarted = false;
					m_condition.notify_all();
				}
				m_thread.join();
			}
		} else {
			m_self->GetLog().Debug("Stopping Fake Trading System replay...");
			m_currentTimeChangeSlotConnection.disconnect();
		}
	}

public:

	OrderId TakeOrderId() {
		return m_id++;
	}

	bool IsStarted() const {
		const Lock lock(m_mutex);
		return m_isStarted;
	}

	void Start() {
		if (!m_self->GetContext().GetSettings().IsReplayMode()) {
			Lock lock(m_mutex);
			Assert(!m_isStarted);
			boost::thread thread(boost::bind(&Implementation::Task, this));
			m_isStarted = true;
			thread.swap(m_thread);
			m_condition.wait(lock);
		} else {
			m_self->GetLog().Info("Stated Fake Trading System replay...");
			m_currentTimeChangeSlotConnection
				= m_self->GetContext().SubscribeToCurrentTimeChange(
					boost::bind(
						&Implementation::OnCurrentTimeChanged,
						this,
						_1));
		}
	}

	void SendOrder(Order &order) {
		
		Assert(order.callback);

		const auto &now = m_self->GetContext().GetCurrentTime();

		order.execDelay = ChooseExecutionDelay();
		if (!m_self->GetContext().GetSettings().IsReplayMode()) {
			const auto callback = order.callback;
			order.callback
				= [this, callback](
						const OrderId &id,
						const std::string &uuid,
						const OrderStatus &status,
						const Qty &remainingQty,
						const TradeInfo *trade) {
					boost::this_thread::sleep(
						boost::get_system_time() + ChooseReportDelay());
					callback(id, uuid, status, remainingQty, trade);	
				};  
		}

		const Lock lock(m_mutex);

		if (!m_currentOrders->empty()) {
			const auto &lastOrder = m_currentOrders->back();
			order.submitTime = lastOrder.submitTime + lastOrder.execDelay;
		} else {
			order.submitTime = now;
		}
		m_currentOrders->push_back(order);
	
		m_condition.notify_all();
	
	}

private:
	
	pt::time_duration ChooseExecutionDelay() const {
		const SettingsReadLock lock(m_settingsMutex);
		return pt::microseconds(m_delayGenerator.executionDelayGenerator());
	}

	pt::time_duration ChooseReportDelay() const {
		const SettingsReadLock lock(m_settingsMutex);
		return pt::microseconds(m_delayGenerator.reportDelayGenerator());
	}

private:

	void Task() {
		try {
			{
				Lock lock(m_mutex);
				m_condition.notify_all();
			}
			m_self->GetLog().Info("Stated Fake Trading System task...");
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
					Assert(!IsZero(order.price));
					if (order.execDelay.total_nanoseconds() > 0) {
						boost::this_thread::sleep(
							boost::get_system_time() + order.execDelay);
					}
					ExecuteOrder(order);
				}
				orders->clear();
			}
		} catch (...) {
			AssertFailNoException();
			throw;
		}
		m_self->GetLog().Info("Fake Trading System stopped.");
	}

	void OnCurrentTimeChanged(const pt::ptime &newTime) {
	
		Lock lock(m_mutex);
		
		if (m_currentOrders->empty()) {
			return;
		}
		
		while (!m_currentOrders->empty()) {
			
			const Order &order = m_currentOrders->front();
			Assert(order.callback);
			Assert(!IsZero(order.price));

			const auto &orderExecTime = order.submitTime + order.execDelay;
			if (orderExecTime >= newTime) {
				break;
			}

			m_self->GetContext().SetCurrentTime(orderExecTime, false);
			ExecuteOrder(order);
			m_currentOrders->pop_front();

			lock.unlock();
			m_self->GetContext().SyncDispatching();
			lock.lock();

		}

	}

	void ExecuteOrder(const Order &order) {
		bool isMatched = false;
		TradeInfo trade = {};
		if (order.isSell) {
			trade.price = order.security->GetBidPriceScaled();
			isMatched = order.price <= trade.price;
		} else {
			trade.price = order.security->GetAskPriceScaled();
			isMatched = order.price >= trade.price;
		}
		const auto &tradingSystemOrderId
			= (boost::format("PAPER%1%") % order.id).str();
		if (isMatched && m_execChanceGenerator.HasChance()) {
			trade.id = tradingSystemOrderId;
			trade.qty = order.qty;
			order.callback(
				order.id,
				tradingSystemOrderId,
				ORDER_STATUS_FILLED,
				0,
				&trade);
		} else {
			order.callback(
				order.id, 
				tradingSystemOrderId,
				ORDER_STATUS_CANCELLED,
				order.qty,
				nullptr);
		}
	}

private:

	Context::CurrentTimeChangeSlotConnection m_currentTimeChangeSlotConnection;

	boost::atomic_uintmax_t m_id;
	bool m_isStarted;

	mutable Mutex m_mutex;
	Condition m_condition;
	boost::thread m_thread;

	Orders m_orders1;
	Orders m_orders2;
	Orders *m_currentOrders;

};

//////////////////////////////////////////////////////////////////////////

Fake::TradingSystem::TradingSystem(
		const TradingMode &mode,
		size_t index,
		Context &context,
		const std::string &tag,
		const IniSectionRef &conf)
	: Base(mode, index, context, tag),
	m_pimpl(new Implementation(conf)) {
	m_pimpl->m_self = this;
	m_pimpl->m_delayGenerator.Report(GetLog());
}

Fake::TradingSystem::~TradingSystem() {
	delete m_pimpl;
}

bool Fake::TradingSystem::IsConnected() const {
	return m_pimpl->IsStarted();
}

void Fake::TradingSystem::CreateConnection(const IniSectionRef &) {
	m_pimpl->Start();
}

OrderId Fake::TradingSystem::SendSellAtMarketPrice(
			Security &security,
			const Currency &currency,
			const Qty &qty,
			const OrderParams &params,
			const OrderStatusUpdateSlot &statusUpdateSlot) {
	AssertLt(0, qty);
	Order order = {
		&security,
		currency,
		true,
		m_pimpl->TakeOrderId(),
		statusUpdateSlot,
		qty,
		0,
		params
	};
	m_pimpl->SendOrder(order);
	return order.id;
}

OrderId Fake::TradingSystem::SendSell(
			Security &security,
			const Currency &currency,
			const Qty &qty,
			const ScaledPrice &price,
			const OrderParams &params,
			const OrderStatusUpdateSlot &statusUpdateSlot) {
	AssertLt(0, qty);
	Order order = {
		&security,
		currency,
		false,
		m_pimpl->TakeOrderId(),
		statusUpdateSlot,
		qty,
		price,
		params
	};
	m_pimpl->SendOrder(order);
	return order.id;
}

OrderId Fake::TradingSystem::SendSellAtMarketPriceWithStopPrice(
			Security &,
			const Currency &,
			const Qty &qty,
			const ScaledPrice &,
			const OrderParams &,
			const OrderStatusUpdateSlot &) {
	AssertLt(0, qty);
	AssertFail("Is not implemented.");
	UseUnused(qty);
	throw Exception("Method is not implemented");
}

OrderId Fake::TradingSystem::SendSellImmediatelyOrCancel(
			Security &security,
			const Currency &currency,
			const Qty &qty,
			const ScaledPrice &price,
			const OrderParams &params,
			const OrderStatusUpdateSlot &statusUpdateSlot) {
	Order order = {
		&security,
		currency,
		true,
		m_pimpl->TakeOrderId(),
		statusUpdateSlot,
		qty,
		price,
		params
	};
	m_pimpl->SendOrder(order);
	return order.id;
}

OrderId Fake::TradingSystem::SendSellAtMarketPriceImmediatelyOrCancel(
			Security &security,
			const Currency &currency,
			const Qty &qty,
			const OrderParams &params,
			const OrderStatusUpdateSlot &statusUpdateSlot) {
	AssertLt(0, qty);
	Order order = {
		&security,
		currency,
		false,
		m_pimpl->TakeOrderId(),
		statusUpdateSlot,
		qty,
		0,
		params
	};
	m_pimpl->SendOrder(order);
	return order.id;
}

OrderId Fake::TradingSystem::SendBuyAtMarketPrice(
			Security &security,
			const Currency &currency,
			const Qty &qty,
			const OrderParams &params,
			const OrderStatusUpdateSlot &statusUpdateSlot) {
	AssertLt(0, qty);
	Order order = {
		&security,
		currency,
		false,
		m_pimpl->TakeOrderId(),
		statusUpdateSlot,
		qty,
		0,
		params
	};
	m_pimpl->SendOrder(order);
	return order.id;
}

OrderId Fake::TradingSystem::SendBuy(
			Security &security,
			const Currency &currency,
			const Qty &qty,
			const ScaledPrice &price,
			const OrderParams &params,
			const OrderStatusUpdateSlot &statusUpdateSlot) {
	AssertLt(0, qty);
	Order order = {
		&security,
		currency,
		false,
		m_pimpl->TakeOrderId(),
		statusUpdateSlot,
		qty,
		price,
		params
	};
	m_pimpl->SendOrder(order);
	return order.id;
}

OrderId Fake::TradingSystem::SendBuyAtMarketPriceWithStopPrice(
			Security &,
			const Currency &,
			const Qty &qty,
			const ScaledPrice &/*stopPrice*/,
			const OrderParams &,
			const OrderStatusUpdateSlot &) {
	AssertLt(0, qty);
	AssertFail("Is not implemented.");
	UseUnused(qty);
	throw Exception("Method is not implemented");
}

OrderId Fake::TradingSystem::SendBuyImmediatelyOrCancel(
			Security &security,
			const Currency &currency,
			const Qty &qty,
			const ScaledPrice &price,
			const OrderParams &params,
			const OrderStatusUpdateSlot &statusUpdateSlot) {
	Order order = {
		&security,
		currency,
		false,
		m_pimpl->TakeOrderId(),
		statusUpdateSlot,
		qty,
		price,
		params
	};
	m_pimpl->SendOrder(order);
	return order.id;
}

OrderId Fake::TradingSystem::SendBuyAtMarketPriceImmediatelyOrCancel(
			Security &security,
			const Currency &currency,
			const Qty &qty,
			const OrderParams &params,
			const OrderStatusUpdateSlot &statusUpdateSlot) {
	AssertLt(0, qty);
	Order order = {
		&security,
		currency,
		false,
		m_pimpl->TakeOrderId(),
		statusUpdateSlot,
		qty,
		0,
		params
	};
	m_pimpl->SendOrder(order);
	return order.id;
}

void Fake::TradingSystem::SendCancelOrder(const OrderId &) {
	AssertFail("Is not implemented.");
	throw Exception("Method is not implemented");
}

void Fake::TradingSystem::SendCancelAllOrders(Security &) {
	AssertFail("Is not implemented.");
	throw Exception("Method is not implemented");
}

void Fake::TradingSystem::OnSettingsUpdate(const IniSectionRef &conf) {

	Base::OnSettingsUpdate(conf);

	Implementation::DelayGenerator delayGenerator(conf);
	delayGenerator.Validate();
	const SettingsWriteLock lock(m_pimpl->m_settingsMutex);
	m_pimpl->m_delayGenerator = delayGenerator;
	delayGenerator.Report(GetLog());

}

//////////////////////////////////////////////////////////////////////////