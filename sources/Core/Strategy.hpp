/**************************************************************************
 *   Created: 2012/07/09 17:27:21
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#pragma once

#include "Position.hpp"
#include "Consumer.hpp"
#include "Fwd.hpp"
#include "Api.h"

namespace trdk {

	class TRDK_CORE_API Strategy : public trdk::Consumer {

	public:

		typedef void (PositionUpdateSlotSignature)(trdk::Position &);
		typedef boost::function<PositionUpdateSlotSignature> PositionUpdateSlot;
		typedef boost::signals2::connection PositionUpdateSlotConnection;

		class TRDK_CORE_API PositionList {

		public:

			class ConstIterator;
		
			class TRDK_CORE_API Iterator
					: public boost::iterator_facade<
						Iterator,
						trdk::Position,
						boost::bidirectional_traversal_tag> {
				friend class trdk::Strategy::PositionList::ConstIterator;
			public:
				class Implementation;
			public:
				explicit Iterator(Implementation *) throw();
				Iterator(const Iterator &);
				~Iterator();
			public:
				Iterator & operator =(const Iterator &);
				void Swap(Iterator &);
			public:
				trdk::Position & dereference() const;
				bool equal(const Iterator &) const;
				bool equal(const ConstIterator &) const;
				void increment();
				void decrement();
				void advance(difference_type);
			private:
				Implementation *m_pimpl;
			};

			class TRDK_CORE_API ConstIterator
					: public boost::iterator_facade<
						ConstIterator,
						const trdk::Position,
						boost::bidirectional_traversal_tag> {
				friend class trdk::Strategy::PositionList::Iterator;
			public:
				class Implementation;
			public:
				explicit ConstIterator(Implementation *) throw();
				explicit ConstIterator(const Iterator &) throw();
				ConstIterator(const ConstIterator &);
				~ConstIterator();
			public:
				ConstIterator & operator =(const ConstIterator &);
				void Swap(ConstIterator &);
			public:
				const trdk::Position & dereference() const;
				bool equal(const Iterator &) const;
				bool equal(const ConstIterator &) const;
				void increment();
				void decrement();
				void advance(difference_type);
			private:
				Implementation *m_pimpl;
			};
		
		public:

			PositionList();
			virtual ~PositionList();

		public:

			virtual size_t GetSize() const = 0;
			virtual bool IsEmpty() const = 0;

			virtual Iterator GetBegin() = 0;
			virtual ConstIterator GetBegin() const = 0;

			virtual Iterator GetEnd() = 0;
			virtual ConstIterator GetEnd() const = 0;

		};

		typedef trdk::ModuleTradingLog TradingLog;

	public:

		explicit Strategy(
				trdk::Context &,
				const std::string &name,
				const std::string &tag);
		virtual ~Strategy();

	public:

		trdk::Strategy::TradingLog & GetTradingLog() const throw();

	public:

		virtual void OnLevel1Update(
					trdk::Security &,
					trdk::Lib::TimeMeasurement::Milestones &);

		virtual void OnPositionUpdate(trdk::Position &);

	public:

		bool IsBlocked(bool forever = false) const;
		void Block() throw();
		void Block(const boost::posix_time::time_duration &);

		struct CancelAndBlockCondition {
			boost::mutex mutex;
			boost::condition_variable condition;
		};
		virtual void CancelAllAndBlock(CancelAndBlockCondition &) = 0;
		virtual void WaitForCancelAndBlock(CancelAndBlockCondition &) = 0;

	public:

		void RaiseLevel1UpdateEvent(
					trdk::Security &,
					trdk::Lib::TimeMeasurement::Milestones &);
		void RaiseLevel1TickEvent(
					trdk::Security &,
					const boost::posix_time::ptime &,
					const trdk::Level1TickValue &);
		void RaiseNewTradeEvent(
					trdk::Security &,
					const boost::posix_time::ptime &,
					trdk::ScaledPrice,
					trdk::Qty,
					trdk::OrderSide);
		void RaiseServiceDataUpdateEvent(const trdk::Service &);
		void RaisePositionUpdateEvent(trdk::Position &);

	public:

		//! Registers position for this strategy.
		/** Thread-unsafe method! Must be call only from event-methods, or if
		  * strategy locked by GetMutex().
		  */
		virtual void Register(Position &);
		//! Unregisters position for this strategy.
		/** Thread-unsafe method! Must be call only from event-methods, or if
		  * strategy locked by GetMutex().
		  */
		virtual void Unregister(Position &) throw();

		PositionList & GetPositions();
		const PositionList & GetPositions() const;

	public:

		PositionUpdateSlotConnection SubscribeToPositionsUpdates(
				const PositionUpdateSlot &)
				const;

	private:

 		class Implementation;
 		Implementation *m_pimpl;

	};

}

//////////////////////////////////////////////////////////////////////////

namespace trdk {

	inline trdk::Strategy::PositionList::Iterator range_begin(
				trdk::Strategy::PositionList &list) {
		return list.GetBegin();
	}
	inline trdk::Strategy::PositionList::Iterator range_end(
				trdk::Strategy::PositionList &list) {
		return list.GetEnd();
	}
	inline trdk::Strategy::PositionList::ConstIterator range_begin(
				const trdk::Strategy::PositionList &list) {
		return list.GetBegin();
	}
	inline trdk::Strategy::PositionList::ConstIterator range_end(
				const trdk::Strategy::PositionList &list) {
		return list.GetEnd();
	}

}

namespace boost {
    template<>
    struct range_mutable_iterator<trdk::Strategy::PositionList> {
        typedef trdk::Strategy::PositionList::Iterator type;
    };
    template<>
    struct range_const_iterator<trdk::Strategy::PositionList> {
        typedef trdk::Strategy::PositionList::ConstIterator type;
    };
}

namespace std {
	template<>
	inline void swap(
				trdk::Strategy::PositionList::Iterator &lhs,
				trdk::Strategy::PositionList::Iterator &rhs) {
		lhs.Swap(rhs);
	}
	template<>
	inline void swap(
				trdk::Strategy::PositionList::ConstIterator &lhs,
				trdk::Strategy::PositionList::ConstIterator &rhs) {
		lhs.Swap(rhs);
	}
}

//////////////////////////////////////////////////////////////////////////
