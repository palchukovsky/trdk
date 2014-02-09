/**************************************************************************
 *   Created: 2012/11/22 11:45:17
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#pragma once

#include "SubscriberPtrWrapper.hpp"
#include "Context.hpp"
#include "Fwd.hpp"

namespace trdk { namespace Engine {

	class Dispatcher : private boost::noncopyable {

	private:

		typedef boost::mutex EventQueueMutex;
		typedef EventQueueMutex::scoped_lock EventQueueLock;
		typedef boost::condition_variable EventQueueCondition;

		struct EventListsSyncObjects {
			EventQueueMutex mutex;
			EventQueueCondition newDataCondition;
		};

		template<typename ListT>
		class EventQueue : private boost::noncopyable {

		public:

			typedef ListT List;

			typedef EventQueueMutex Mutex;
			typedef EventQueueLock Lock;
			typedef EventQueueCondition Condition;

		private:

			enum TaskState {
				TASK_STATE_INACTIVE,
				TASK_STATE_ACTIVE,
				TASK_STATE_STOPPED,
			};

		public:
			
			EventQueue(const char *name, const Context &context)
					: m_context(context),
					m_name(name),
					m_current(&m_lists.first),
					m_taksState(TASK_STATE_INACTIVE) {
				if (m_context.GetSettings().IsReplayMode()) {
					m_readyToReadCondition.reset(new Condition);
				}
			}

		public:

			void AssignSyncObjects(
						boost::shared_ptr<EventListsSyncObjects> &sync) {
				Assert(!m_sync);
				m_sync = sync;
			}

			const char * GetName() const {
				return m_name;
			}

			bool IsActive() const {
				return m_taksState == TASK_STATE_ACTIVE;
			}

			void Activate() {
				Assert(m_sync);
				const Lock lock(m_sync->mutex);
				AssertEq(int(TASK_STATE_INACTIVE), int(m_taksState));
				m_taksState = TASK_STATE_ACTIVE;
			}

			void Suspend() {
				Assert(m_sync);
				const Lock lock(m_sync->mutex);
				AssertNe(int(TASK_STATE_STOPPED), int(m_taksState));
				m_taksState = TASK_STATE_INACTIVE;
			}

			void Stop() {
				Assert(m_sync);
				{
					const Lock lock(m_sync->mutex);
					AssertNe(int(TASK_STATE_STOPPED), int(m_taksState));
					m_taksState = TASK_STATE_STOPPED;
				}
				m_sync->newDataCondition.notify_all();
				if (m_readyToReadCondition) {
					m_readyToReadCondition->notify_all();
				}
			}

			bool IsStopped(const Lock &lock) const {
				Assert(m_sync);
				Assert(&m_sync->mutex == lock.mutex());
				Assert(lock);
				UseUnused(lock);
				return m_taksState == TASK_STATE_STOPPED;
			}

			template<typename Event>
			void Queue(const Event &event, bool flush) {
				Assert(m_sync);
				Lock lock(m_sync->mutex);
				if (m_taksState == TASK_STATE_STOPPED) {
					return;
				}
				Assert(
					m_current == &m_lists.first
					|| m_current == &m_lists.second);
				if (Dispatcher::QueueEvent(event, *m_current) || !flush) {
					m_sync->newDataCondition.notify_one();
					if (m_readyToReadCondition) {
						m_readyToReadCondition->wait(lock);
					}
				}
				if (!(m_current->size() % 50)) {
					m_context.GetLog().Warn(
						"Dispatcher queue \"%1%\" is too long (%2% events)!",
						boost::make_tuple(
							boost::cref(m_name),
							m_current->size()));
				}
			}

			bool Enqueue(Lock &lock) {

				Assert(m_sync);
				Assert(&m_sync->mutex == lock.mutex());
				Assert(lock);
				Assert(
					m_current == &m_lists.first
					|| m_current == &m_lists.second);

				size_t heavyLoadsCount = 0;
				while (	!m_current->empty()
						&& m_taksState == TASK_STATE_ACTIVE) {

					if (!(++heavyLoadsCount % 500)) {
						m_context.GetLog().Warn(
							"Dispatcher task \"%1%\" is heavy loaded"
								" (%2% iterations)!",
							boost::make_tuple(
								boost::cref(m_name),
								heavyLoadsCount));
					}

					List *listToRead = m_current;
					m_current = m_current == &m_lists.first
						?	&m_lists.second
						:	&m_lists.first;

					lock.unlock();
					Assert(!listToRead->empty());
					foreach (const auto &event, *listToRead) {
						Dispatcher::RaiseEvent(event);
					}
					listToRead->clear();
					lock.lock();

					if (m_readyToReadCondition) {
						m_readyToReadCondition->notify_all();
					}

				}

				return heavyLoadsCount > 0;

			}

		private:

			const Context &m_context;

			const char *const m_name;

			std::pair<List, List> m_lists;
			List *m_current;
			
			boost::shared_ptr<EventListsSyncObjects> m_sync;
			std::unique_ptr<Condition> m_readyToReadCondition;

			TaskState m_taksState;

		};

		typedef boost::tuple<Security *, SubscriberPtrWrapper>
			Level1UpdateEvent;
		//! @todo: Check performance: set or list + find
		typedef EventQueue<std::set<Level1UpdateEvent>> Level1UpdateEventQueue;

		typedef boost::tuple<
				SubscriberPtrWrapper::Level1Tick,
				SubscriberPtrWrapper>
			Level1TickEvent;
		//! @todo	HAVY OPTIMIZATION!!! Use preallocated buffer here instead
		//!			std::list.
		typedef EventQueue<std::list<Level1TickEvent>> Level1TicksEventQueue;

		typedef boost::tuple<
				SubscriberPtrWrapper::Trade,
				SubscriberPtrWrapper>
			NewTradeEvent;
		//! @todo	HAVY OPTIMIZATION!!! Use preallocated buffer here instead
		//!			std::list.
		typedef EventQueue<std::list<NewTradeEvent>> NewTradeEventQueue;

		//! Increasing position references count for safety work in core (which
		//! can lost owning references at event handling).
		typedef boost::tuple<
				boost::shared_ptr<Position>,
				SubscriberPtrWrapper>
			PositionUpdateEvent;
		//! @todo: Check performance: set or list
		typedef EventQueue<std::set<PositionUpdateEvent>>
			PositionsUpdateEventQueue;

		typedef boost::tuple<
				SubscriberPtrWrapper::BrokerPosition,
				SubscriberPtrWrapper>
			BrokerPositionUpdateEvent;
		//! @todo: Check performance: map to exclude duplicate securities
		typedef EventQueue<std::list<BrokerPositionUpdateEvent>>
			BrokerPositionsUpdateEventQueue;

		typedef boost::tuple<
				Security *,
				Security::Bar,
				SubscriberPtrWrapper>
			NewBarEvent;
		//! @todo	HAVY OPTIMIZATION!!! Use preallocated buffer here instead
		//!			std::list.
		typedef EventQueue<std::list<NewBarEvent>> NewBarEventQueue;

	public:

		explicit Dispatcher(Engine::Context &);
		~Dispatcher();

	public:

		bool IsActive() const {
			return
				|| m_brokerPositionsUpdates.IsActive()
				|| m_positionsUpdates.IsActive()
				|| m_level1Updates.IsActive();
		}

		void Activate();
		void Suspend();

	public:

		void SignalLevel1Update(SubscriberPtrWrapper &, Security &);
		void SignalLevel1Tick(
					SubscriberPtrWrapper &,
					Security &,
					const boost::posix_time::ptime &,
					const trdk::Level1TickValue &,
					bool flush);
		void SignalPositionUpdate(SubscriberPtrWrapper &, Position &);
		void SignalBrokerPositionUpdate(
					SubscriberPtrWrapper &,
					Security &,
					Qty,
					bool isInitial);

	private:

		template<typename Event>
		static void RaiseEvent(const Event &) {
			static_assert(false, "Failed to find event raise specialization.");
		}
		template<>
		static void RaiseEvent(const Level1UpdateEvent &level1Update) {
			boost::get<1>(level1Update).RaiseLevel1UpdateEvent(
				*boost::get<0>(level1Update));
		}
		template<>
		static void RaiseEvent(const Level1TickEvent &tick) {
			boost::get<1>(tick).RaiseLevel1TickEvent(boost::get<0>(tick));
		}
		template<>
		static void RaiseEvent(const NewTradeEvent &newTradeEvent) {
			boost::get<1>(newTradeEvent).RaiseNewTradeEvent(
				boost::get<0>(newTradeEvent));
		}
		template<>
		static void RaiseEvent(const PositionUpdateEvent &positionUpdateEvent) {
			boost::get<1>(positionUpdateEvent).RaisePositionUpdateEvent(
				*boost::get<0>(positionUpdateEvent));
		}
		template<>
		static void RaiseEvent(
					const BrokerPositionUpdateEvent &positionUpdateEvent) {
			boost::get<1>(positionUpdateEvent).RaiseBrokerPositionUpdateEvent(
				boost::get<0>(positionUpdateEvent));
		}
		template<>
		static void RaiseEvent(const NewBarEvent &newBarEvent) {
			boost::get<2>(newBarEvent).RaiseNewBarEvent(
				*boost::get<0>(newBarEvent),
				boost::get<1>(newBarEvent));
		}

		template<typename Event, typename EventList>
		static bool QueueEvent(const Event &, EventList &) {
			static_assert(false, "Failed to find event queue specialization.");
		}
		template<typename EventList>
		static bool QueueEvent(
					const Level1UpdateEvent &level1UpdateEvent,
					EventList &eventList) {
			//! @todo place for optimization
			if (eventList.find(level1UpdateEvent) != eventList.end()) {
				return false;
			}
			eventList.insert(level1UpdateEvent);
			return true;
		}
		template<typename EventList>
		static bool QueueEvent(
					const Level1TickEvent &tick,
					EventList &eventList) {
			eventList.push_back(tick);
			return true;
		}
		template<typename EventList>
		static bool QueueEvent(
					const NewTradeEvent &newTradeEvent,
					EventList &eventList) {
			eventList.push_back(newTradeEvent);
			return true;
		}
		template<typename EventList>
		static bool QueueEvent(
					const PositionUpdateEvent &positionUpdateEvent,
					EventList &eventList) {
			//! @todo place for optimization
			if (eventList.find(positionUpdateEvent) != eventList.end()) {
				return false;
			}
			eventList.insert(positionUpdateEvent);
			return true;
		}
		template<typename EventList>
		static bool QueueEvent(
					const BrokerPositionUpdateEvent &positionUpdateEvent,
					EventList &eventList) {
			eventList.push_back(positionUpdateEvent);
			return true;
		}
		template<typename EventList>
		static bool QueueEvent(
					const NewBarEvent &newBarEvent,
					EventList &eventList) {
			eventList.push_back(newBarEvent);
			return true;
		}

	private:

		////////////////////////////////////////////////////////////////////////////////

		template<typename EventList>
		bool EnqueueEvents(EventList &list, EventQueueLock &lock) const {
			try {
				return list.Enqueue(lock);
			} catch (const trdk::Lib::ModuleError &ex) {
				m_context.GetLog().Error(
					"Module error in dispatcher notification task"
						" \"%1%\": \"%2%\".",
					boost::make_tuple(
					boost::cref(list.GetName()),
					boost::cref(ex)));
				throw;
			} catch (...) {
				m_context.GetLog().Error(
					"Unhandled exception caught in dispatcher"
						" notification task \"%1%\".",
					list.GetName());
				AssertFailNoException();
				throw;
			}
		}

		template<size_t index, typename EventLists>
		bool EnqueueEventList(
					EventLists &lists,
					std::bitset<boost::tuples::length<EventLists>::value>
						&deactivationMask,
					EventQueueLock &lock)
				const {
			if (deactivationMask[index]) {
				return false;
			}
			auto &list = lists.get<index>();
			if (EnqueueEvents(list, lock)) {
				return true;
			}
			deactivationMask[index] = list.IsStopped(lock);
			return false;
		}

		////////////////////////////////////////////////////////////////////////////////

		template<
			typename ListWithHighPriority,
			typename ListWithLowPriority,
			typename ListWithExtraLowPriority,
			typename ListWithExtraLowPriority2>
		void StartNotificationTask(
					boost::barrier &startBarrier,
					ListWithHighPriority &listWithHighPriority,
					ListWithLowPriority &listWithLowPriority,
					ListWithExtraLowPriority &listWithExtraLowPriority,
					ListWithExtraLowPriority2 &listWithExtraLowPriority2,
					size_t &threadsCounter) {
			UseUnused(threadsCounter);
			const auto lists = boost::make_tuple(
				boost::ref(listWithHighPriority),
				boost::ref(listWithLowPriority),
				boost::ref(listWithExtraLowPriority),
				boost::ref(listWithExtraLowPriority2));
			m_threads.create_thread(
				boost::bind(
					&Dispatcher::NotificationTask<decltype(lists)>,
					this,
					boost::ref(startBarrier),
					lists));
			Assert(1 == threadsCounter--);
		}

		template<typename T1, typename T2, typename T3, typename T4>
		static std::string GetEventListsName(
					const boost::tuple<T1, T2, T3, T4> &lists) {
			boost::format result("%1%, %2%, %3%, %4%");
			result
				% lists.get<0>().GetName()
				% lists.get<1>().GetName()
				% lists.get<2>().GetName()
				% lists.get<3>().GetName();
			return result.str();
		}

		template<typename T1, typename T2, typename T3, typename T4>
		static void AssignEventListsSyncObjects(
					boost::shared_ptr<EventListsSyncObjects> &sync,
					const boost::tuple<T1, T2, T3, T4> &lists) {
			lists.get<0>().AssignSyncObjects(sync);
			lists.get<1>().AssignSyncObjects(sync);
			lists.get<2>().AssignSyncObjects(sync);
			lists.get<3>().AssignSyncObjects(sync);
		}

		template<typename T1, typename T2, typename T3, typename T4>
		void EnqueueEventListsCollection(
					const boost::tuple<T1, T2, T3, T4> &lists,
					std::bitset<4> &deactivationMask,
					EventQueueLock &lock)
				const {
			do {
				do {
					do {
						EnqueueEventList<0>(lists, deactivationMask, lock);
					} while (EnqueueEventList<1>(lists, deactivationMask, lock));
				} while (EnqueueEventList<2>(lists, deactivationMask, lock));
			} while (EnqueueEventList<3>(lists, deactivationMask, lock));
		}

		////////////////////////////////////////////////////////////////////////////////

		template<typename EventLists>
		void NotificationTask(
					boost::barrier &startBarrier,
					EventLists &lists)
				const {
			bool isError = false;
			try {
				m_context.GetLog().Debug(
					"Dispatcher notification task \"%1%\" started...",
					GetEventListsName(lists));
				std::bitset<boost::tuples::length<EventLists>::value>
					deactivationMask;
				boost::shared_ptr<EventListsSyncObjects> sync(
					new EventListsSyncObjects);
				AssignEventListsSyncObjects(sync, lists);
				startBarrier.wait();
				EventQueueLock lock(sync->mutex);
				for ( ; ; ) {
					EnqueueEventListsCollection(lists, deactivationMask, lock);
					if (deactivationMask.all()) {
						break;
					}
					sync->newDataCondition.wait(lock);
				}
			} catch (...) {
				// error already logged
				isError = true;
				AssertFailNoException();
			}
			m_context.GetLog().Debug(
				"Dispatcher notification task \"%1%\" stopped.",
				GetEventListsName(lists));
			if (isError) {
				//! @todo: Call engine instance stop instead.
				exit(1);
			}
		}

	private:

		Engine::Context &m_context;

		boost::thread_group m_threads;

		Level1UpdateEventQueue m_level1Updates;
		Level1TicksEventQueue m_level1Ticks;
		PositionsUpdateEventQueue m_positionsUpdates;
		BrokerPositionsUpdateEventQueue m_brokerPositionsUpdates;

	};

} }
