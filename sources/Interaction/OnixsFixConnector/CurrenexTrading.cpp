/**************************************************************************
 *   Created: 2014/08/12 23:52:51
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#include "Prec.hpp"
#include "FixTrading.hpp"

using namespace trdk;
using namespace trdk::Lib;
using namespace trdk::Interaction::OnixsFixConnector;

namespace fix = OnixS::FIX;

////////////////////////////////////////////////////////////////////////////////

namespace trdk { namespace Interaction { namespace OnixsFixConnector {

	class CurrenexTrading : public FixTrading {

	public:

		explicit CurrenexTrading(
					Context &context,
					const std::string &tag,
					const Lib::IniSectionRef &conf)
				: FixTrading(context, tag, conf) {
			//...//
		}
		
		virtual ~CurrenexTrading() {
			//...//
		}

	public:

		virtual void onInboundApplicationMsg(
					fix::Message &message,
					fix::Session *session) {

			const auto &replyTime = TimeMeasurement::Milestones::GetNow();
	
			Assert(session == &GetSession().Get());
			UseUnused(session);

			if (message.type() != "8") {
				return;
			}

			const auto &execTransType = message.get(fix::FIX40::Tags::ExecTransType);

			if (execTransType == fix::FIX40::Values::ExecTransType::New) {
		
				const auto &execType = message.get(fix::FIX41::Tags::ExecType);
				const auto &ordStatus = message.get(fix::FIX40::Tags::OrdStatus);
		
				if (execType == fix::FIX41::Values::ExecType::New) {
			
					if (ordStatus == fix::FIX40::Values::OrdStatus::New) {
						OnOrderNew(message, replyTime);
						return;
					}

				} else if (execType == fix::FIX41::Values::ExecType::Cancelled) {

					if (ordStatus == fix::FIX40::Values::OrdStatus::Canceled) {
						OnOrderCanceled(message, replyTime);
						return;
					}

				} else if (execType == fix::FIX41::Values::ExecType::Fill) {

					if (ordStatus == fix::FIX40::Values::OrdStatus::Partially_filled) {
						OnOrderPartialFill(message, replyTime);
						return;
					} else if (ordStatus == fix::FIX40::Values::OrdStatus::Filled) {
						OnOrderFill(message, replyTime);
						return;
					}

				} else if (execType == fix::FIX41::Values::ExecType::Suspended) {
			
					//...//
		
				} else if (execType == fix::FIX41::Values::ExecType::Rejected) {
					const std::string &reason
						= message.get(fix::FIX40::Tags::Text);
					OnOrderRejected(
						message,
						replyTime,
						reason,
						boost::iequals(
							reason,
							"maximum operation limit exceeded"));
					return;
				} else if (execType == fix::FIX41::Values::ExecType::Expired) {

					if (ordStatus == fix::FIX40::Values::OrdStatus::Expired) {
						OnOrderRejected(message, replyTime, "expired", true);
						return;
					}

				}
		
			} else if (execTransType == fix::FIX40::Values::ExecTransType::Status) {
		
				//...//

			}

			GetLog().Error(
				"FIX Server sent unknown Execution Report: \"%1%\".",
				boost::make_tuple(boost::cref(message)));

		}

	protected:

		fix::Message CreateMarketOrderMessage(
					const OrderId &orderId,	
					const Security &security,
					const Currency &currency,
					const Qty &qty) {
			fix::Message order
				= CreateOrderMessage(orderId, security, currency, qty);
			order.set(
				fix::FIX40::Tags::OrdType,
				fix::FIX41::Values::OrdType::Forex_Market);
			return std::move(order);
		}

	};

} } }

////////////////////////////////////////////////////////////////////////////////

TRDK_INTERACTION_ONIXSFIXCONNECTOR_API
TradeSystemFactoryResult
CreateCurrenexTrading(
			Context &context,
			const std::string &tag,
			const IniSectionRef &configuration) {
	TradeSystemFactoryResult result;
	boost::get<0>(result).reset(
		new CurrenexTrading(context, tag, configuration));
	return result;
}

////////////////////////////////////////////////////////////////////////////////
