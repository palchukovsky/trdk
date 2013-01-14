/**************************************************************************
 *   Created: 2012/09/19 23:57:31
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot
 **************************************************************************/

#pragma once

#include "Module.hpp"
#include "Api.h"

namespace Trader {

	class TRADER_CORE_API Observer
		: public Trader::Module,
		public boost::enable_shared_from_this<Trader::Observer> {

	public:

		typedef std::list<boost::shared_ptr<Trader::Security>> NotifyList;

	public:

		Observer(
				const std::string &name,
				const std::string &tag,
				const Trader::Observer::NotifyList &,
				boost::shared_ptr<Trader::TradeSystem>,
				boost::shared_ptr<const Settings>);
		virtual ~Observer();

	public:

		virtual void OnLevel1Update(const Trader::Security &);
		virtual void OnNewTrade(
				const Trader::Security &,
				const boost::posix_time::ptime &,
				Trader::ScaledPrice price,
				Trader::Qty qty,
				Trader::OrderSide);
		virtual void OnServiceDataUpdate(const Trader::Service &);

	public:

		void RaiseLevel1UpdateEvent(const Security &);
		void RaiseNewTradeEvent(
					const Trader::Security &,
					const boost::posix_time::ptime &,
					Trader::ScaledPrice,
					Trader::Qty,
					Trader::OrderSide);
		void RaiseServiceDataUpdateEvent(const Trader::Service &);

	public:

		const NotifyList & GetNotifyList() const;

	protected:

		Trader::TradeSystem & GetTradeSystem();

	private:

		class Implementation;
		Implementation *m_pimpl;

	};

}
