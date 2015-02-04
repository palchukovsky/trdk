/**************************************************************************
 *   Created: 2012/11/22 11:48:56
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#include "Prec.hpp"
#include "Dispatcher.hpp"
#include "Context.hpp"
#include "Core/Strategy.hpp"
#include "Core/Observer.hpp"

namespace pt = boost::posix_time;

using namespace trdk;
using namespace trdk::Lib;
using namespace trdk::Engine;

////////////////////////////////////////////////////////////////////////////////

namespace {
	
	struct NoMeasurementPolicy {
		static TimeMeasurement::Milestones StartDispatchingTimeMeasurement(
				const trdk::Context &) {
			return TimeMeasurement::Milestones();
		}
	};

	struct DispatchingTimeMeasurementPolicy {
		static TimeMeasurement::Milestones StartDispatchingTimeMeasurement(
				const trdk::Context &context) {
			return context.StartDispatchingTimeMeasurement();
		}
	};

}

////////////////////////////////////////////////////////////////////////////////

Dispatcher::Dispatcher(Engine::Context &context)
			: m_context(context),
			m_level1Updates("Level 1 Updates", m_context),
			m_level1Ticks("Level 1 Ticks", m_context),
			m_newTrades("Trades", m_context),
			m_positionsUpdates("Positions", m_context),
			m_brokerPositionsUpdates("Broker Positions", m_context),
			m_newBars("Bars", m_context),
			m_bookUpdateTicks("Book Update Ticks", m_context) {
	unsigned int threadsCount = 2;
	boost::barrier startBarrier(threadsCount + 1);
	StartNotificationTask<DispatchingTimeMeasurementPolicy>(
		startBarrier,
		m_bookUpdateTicks,
		threadsCount);
	StartNotificationTask<NoMeasurementPolicy>(
		startBarrier,
		m_positionsUpdates,
		threadsCount);
	AssertEq(0, threadsCount);
	startBarrier.wait();
}

Dispatcher::~Dispatcher() {
	try {
		m_context.GetLog().Debug("Stopping events dispatching...");
		m_bookUpdateTicks.Stop();
		m_positionsUpdates.Stop();
		m_threads.join_all();
		m_context.GetLog().Debug("Events dispatching stopped.");
	} catch (...) {
		AssertFailNoException();
		throw;
	}
}

void Dispatcher::Activate() {
	m_context.GetLog().Debug("Starting events dispatching...");
	m_positionsUpdates.Activate();
	m_bookUpdateTicks.Activate();
	m_context.GetLog().Debug("Events dispatching started.");
}

void Dispatcher::Suspend() {
	m_context.GetLog().Debug("Suspending events dispatching...");
	m_bookUpdateTicks.Suspend();
	m_positionsUpdates.Suspend();
	m_newBars.Suspend();
	m_context.GetLog().Debug("Events dispatching suspended.");
}

void Dispatcher::SignalLevel1Update(
			SubscriberPtrWrapper &subscriber,
			Security &security,
			const TimeMeasurement::Milestones &timeMeasurement) {
	if (subscriber.IsBlocked()) {
		return;
	}
	m_level1Updates.Queue(
		boost::make_tuple(&security, subscriber, timeMeasurement),
		true);
}

void Dispatcher::SignalLevel1Tick(
			SubscriberPtrWrapper &subscriber,
			Security &security,
			const boost::posix_time::ptime &time,
			const Level1TickValue &value,
			bool flush) {
	try {
		if (subscriber.IsBlocked()) {
			return;
		}
		const SubscriberPtrWrapper::Level1Tick tick(security, time, value);
		m_level1Ticks.Queue(
			boost::make_tuple(tick, subscriber),
			flush);
	} catch (...) {
		//! Blocking as irreversible error, data loss.
		subscriber.Block();
		throw;
	}
}

void Dispatcher::SignalNewTrade(
			SubscriberPtrWrapper &subscriber,
			Security &security,
			const pt::ptime &time,
			ScaledPrice price,
			Qty qty,
			OrderSide side) {
	try {
		if (subscriber.IsBlocked()) {
			return;
		}
		const SubscriberPtrWrapper::Trade trade = {
			&security,
			time,
			price,
			qty,
			side};
		m_newTrades.Queue(boost::make_tuple(trade, subscriber), true);
	} catch (...) {
		//! Blocking as irreversible error, data loss.
		subscriber.Block();
		throw;
	}
}

void Dispatcher::SignalPositionUpdate(
			SubscriberPtrWrapper &subscriber,
			Position &position) {
	try {
		m_positionsUpdates.Queue(
			boost::make_tuple(position.shared_from_this(), subscriber),
			true);
	} catch (...) {
		//! Blocking as irreversible error, data loss.
		subscriber.Block();
		throw;
	}
}

void Dispatcher::SignalBrokerPositionUpdate(
			SubscriberPtrWrapper &subscriber,
			Security &security,
			Qty qty,
			bool isInitial) {
	try {
		const SubscriberPtrWrapper::BrokerPosition position = {
			&security,
			qty,
			isInitial
		};
		m_brokerPositionsUpdates.Queue(
			boost::make_tuple(position , subscriber),
			true);
	} catch (...) {
		//! Blocking as irreversible error, data loss.
		subscriber.Block();
		throw;
	}
}

void Dispatcher::SignalNewBar(
			SubscriberPtrWrapper &subscriber,
			Security &security,
			const Security::Bar &bar) {
	try {
		if (subscriber.IsBlocked()) {
			return;
		}
		m_newBars.Queue(boost::make_tuple(&security, bar, subscriber), true);
	} catch (...) {
		//! Blocking as irreversible error, data loss.
		subscriber.Block();
		throw;
	}
}

void Dispatcher::SignalBookUpdateTick(
			SubscriberPtrWrapper &subscriber,
			Security &security,
			const boost::shared_ptr<const Security::Book> &book,
			const TimeMeasurement::Milestones &timeMeasurement) {
	try {
		if (subscriber.IsBlocked()) {
			return;
		}
		m_bookUpdateTicks.Queue(
			boost::make_tuple(
				&security,
				book,
				timeMeasurement,
				subscriber),
			true);
	} catch (...) {
		//! Blocking as irreversible error, data loss.
		subscriber.Block();
		throw;
	}
}

//////////////////////////////////////////////////////////////////////////
