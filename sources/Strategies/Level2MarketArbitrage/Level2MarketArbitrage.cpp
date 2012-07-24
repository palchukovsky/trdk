/**************************************************************************
 *   Created: 2012/07/16 01:41:42
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot
 **************************************************************************/

#include "Prec.hpp"
#include "Level2MarketArbitrage.hpp"
#include "Core/PositionReporterAlgo.hpp"
#include "Core/MarketDataSource.hpp"
#include "Core/PositionBundle.hpp"

namespace s = Strategies::Level2MarketArbitrage;
namespace pt = boost::posix_time;

namespace {
	
	const char *const logTag = "level-2-arbitrage";
	const std::string algoName = "Level II Market Arbitrage";

	enum PositionState {
		STATE_OPENING				= 1,
		STATE_CLOSING_TRY_STOP_LOSS	= 2,
		STATE_CLOSING				= 3
	};

}

s::Algo::Algo(
			boost::shared_ptr<Security> security,
			const IniFile &ini,
			const std::string &section)
		: Base(security, logTag) {
	DoSettingsUpdate(ini, section);
}

s::Algo::~Algo() {
	//...//
}

const std::string & s::Algo::GetName() const {
	return algoName;
}

void s::Algo::SubscribeToMarketData(MarketDataSource &marketDataSource) {
	marketDataSource.SubscribeToMarketDataLevel1(GetSecurity());
	marketDataSource.SubscribeToMarketDataLevel2(GetSecurity());
}

void s::Algo::UpdateAlogImplSettings(const IniFile &ini, const std::string &section) {
	DoSettingsUpdate(ini, section);
}

std::auto_ptr<PositionReporter> s::Algo::CreatePositionReporter() const {
	std::auto_ptr<PositionReporter> result(new PositionReporterAlgo<decltype(*this)>(*this));
	return result;
}

void s::Algo::Update() {
	AssertFail("Strategy logic error - method \"Update\" has been called");
}

void s::Algo::DoSettingsUpdate(const IniFile &ini, const std::string &section) {
	
	Settings settings = {};

	settings.askBidDifferencePercent
		= ini.ReadTypedKey<double>(section, "ask_bid_difference_percent") / 100;
	settings.takeProfitPercent
		= ini.ReadTypedKey<double>(section, "take_profit_percent") / 100;
	settings.stopLossPercent
		= ini.ReadTypedKey<double>(section, "stop_loss_percent") / 100;
	settings.volume
		= GetSecurity()->Scale(ini.ReadTypedKey<double>(section, "volume"));
	
	struct Util {	
		static const char * ConvertToStr(Settings::OpenMode mode) {
			switch (mode) {
				default:
					AssertFail("Unknown open mode.");
				case Settings::OPEN_MODE_NONE:
					return "none";
				case Settings::OPEN_MODE_SHORT_IF_ASK_MORE_BID:
					return "short_if_ask>bid";
				case Settings::OPEN_MODE_SHORT_IF_BID_MORE_ASK:
					return "short_if_bid>ask";
			}
		}
		static const char * ConvertToStr(Settings::OrderType type) {
			switch (type) {
				case Settings::ORDER_TYPE_IOC:
					return "IOC";
				case  Settings::ORDER_TYPE_MKT:
					return "MKT";
				default:
					AssertFail("Unknown order type.");
					return "";
			}
		}
		static Settings::OrderType ConvertStrToOrderType(const std::string &str) {
			if (str == "IOC") {
				return Settings::ORDER_TYPE_IOC;
			} else if (str == "MKT") {
				return Settings::ORDER_TYPE_MKT;
			} else {
				throw IniFile::KeyFormatError("possible values: IOC, MKT");
			}
		}
	};
	{
		const std::string mode = ini.ReadKey(section, "open_mode", false);
		if (mode == "none") {
			settings.openMode = Settings::OPEN_MODE_NONE;
		} else if (mode == "short_if_ask>bid") {
			settings.openMode = Settings::OPEN_MODE_SHORT_IF_ASK_MORE_BID;
		} else if (mode == "short_if_bid>ask") {
			settings.openMode = Settings::OPEN_MODE_SHORT_IF_BID_MORE_ASK;
		} else {
			throw IniFile::KeyFormatError("open_mode possible values: none, short_if_ask>bid, short_if_bid>ask");
		}
	}

	settings.positionTimeSeconds
		= pt::seconds(ini.ReadTypedKey<long>(section, "position_time_seconds"));

	settings.openOrderType
		= Util::ConvertStrToOrderType(ini.ReadKey(section, "open_order_type", false));
	settings.closeOrderType
		= Util::ConvertStrToOrderType(ini.ReadKey(section, "close_order_type", false));

	SettingsReport report;
	AppendPercentSettingsReport("ask_bid_difference_percent", settings.askBidDifferencePercent * 100, report);
	if (settings.closeOrderType == Settings::ORDER_TYPE_IOC) {
		AppendPercentSettingsReport("take_profit_percent", settings.takeProfitPercent * 100, report);
		AppendPercentSettingsReport("stop_loss_percent", settings.stopLossPercent * 100, report);
	}
	AppendSettingsReport("open_mode", Util::ConvertToStr(settings.openMode), report);
	AppendSettingsReport("position_time_seconds", settings.positionTimeSeconds, report);
	AppendSettingsReport("open_order_type", Util::ConvertToStr(settings.openOrderType), report);
	AppendSettingsReport("close_order_type", Util::ConvertToStr(settings.closeOrderType), report);
	AppendSettingsReport("volume", GetSecurity()->Descale(settings.volume), report);
	ReportSettings(report);

	m_settings = settings;

}

boost::shared_ptr<Position> s::Algo::OpenShortPostion(
			Security::Qty askSize,
			Security::Qty bidSize) {
	const auto price = GetSecurity()->GetAskScaled();
	boost::shared_ptr<Position> result(
		new Position(
			GetSecurity(),
			Position::TYPE_SHORT,
			CalcQty(price, m_settings.volume),
			price,
			askSize,
			bidSize,
			price - Security::Price(boost::math::round(price * m_settings.takeProfitPercent)),
			price + Security::Price(boost::math::round(price * m_settings.stopLossPercent)),
			STATE_OPENING,
			shared_from_this()));
	switch (m_settings.openOrderType) {
		case Settings::ORDER_TYPE_IOC:
			GetSecurity()->SellOrCancel(result->GetPlanedQty(), result->GetStartPrice(), *result);
			break;
		case Settings::ORDER_TYPE_MKT:
			GetSecurity()->Sell(result->GetPlanedQty(), *result);
			break;
		default:
			AssertFail("Unknown order type.");
			break;
	}
	return result;
}

boost::shared_ptr<Position> s::Algo::OpenLongPostion(
			Security::Qty askSize,
			Security::Qty bidSize) {
	const auto price = GetSecurity()->GetBidScaled();
	boost::shared_ptr<Position> result(
		new Position(
			GetSecurity(),
			Position::TYPE_LONG,
			CalcQty(price, m_settings.volume),
			price,
			askSize,
			bidSize,
			price + Security::Price(boost::math::round(price * m_settings.takeProfitPercent)),
			price - Security::Price(boost::math::round(price * m_settings.stopLossPercent)),
			STATE_OPENING,
			shared_from_this()));
	switch (m_settings.openOrderType) {
		case Settings::ORDER_TYPE_IOC:
			GetSecurity()->BuyOrCancel(result->GetPlanedQty(), result->GetStartPrice(), *result);
			break;
		case Settings::ORDER_TYPE_MKT:
			GetSecurity()->Buy(result->GetPlanedQty(), *result);
			break;
		default:
			AssertFail("Unknown order type.");
			break;
	}
	return result;
}

boost::shared_ptr<PositionBandle> s::Algo::TryToOpenPositions() {
	
	boost::shared_ptr<PositionBandle> result;
	
	const auto askSize = GetSecurity()->GetAskSize();
	const auto bidSize = GetSecurity()->GetBidSize();
	if (	!askSize
			|| !bidSize
			|| m_settings.openMode == Settings::OPEN_MODE_NONE) {
		return result;
	}

	if (askSize > bidSize * m_settings.askBidDifferencePercent) {
		// we expect prices to drop (since many sellers have shown up temporarily)
		result.reset(new PositionBandle);
		switch (m_settings.openMode) {
			case Settings::OPEN_MODE_SHORT_IF_ASK_MORE_BID:
				result->Get().push_back(OpenShortPostion(askSize, bidSize));
				break;
			case Settings::OPEN_MODE_SHORT_IF_BID_MORE_ASK:
				result->Get().push_back(OpenLongPostion(askSize, bidSize));
				break;
			default:
				AssertFail("Unknown open mode");
				break;
		}
	} else if (bidSize > askSize * m_settings.askBidDifferencePercent) {
		// we expect prices to increase as many buyers have shown up
		result.reset(new PositionBandle);
		switch (m_settings.openMode) {
			case Settings::OPEN_MODE_SHORT_IF_ASK_MORE_BID:
				result->Get().push_back(OpenLongPostion(askSize, bidSize));
				break;
			case Settings::OPEN_MODE_SHORT_IF_BID_MORE_ASK:
				result->Get().push_back(OpenShortPostion(askSize, bidSize));
				break;
			default:
				AssertFail("Unknown open mode");
				break;
		}
	}

	return result;

}

void s::Algo::CloseLongPosition(Position &position, bool asIs) {
	Assert(position.GetType() == Position::TYPE_LONG);
	Security &security = *GetSecurity();
	if (position.GetAlgoFlag() == STATE_OPENING) {
		if (asIs) {
			CloseLongPositionStopLossDo(position);
		} else {
			ReportCloseTry(position);
			switch (m_settings.openOrderType) {
				case Settings::ORDER_TYPE_IOC:
					security.SellOrCancel(position.GetOpenedQty(), position.GetTakeProfit(), position);
					position.SetCloseType(Position::CLOSE_TYPE_TAKE_PROFIT);
					break;
				case Settings::ORDER_TYPE_MKT:
					security.Sell(position.GetOpenedQty(), position);
					position.SetCloseType(Position::CLOSE_TYPE_NONE);
					break;
				default:
					AssertFail("Unknown order type.");
					break;
			}
		}
		position.SetAlgoFlag(STATE_CLOSING);
	} else if (position.GetAlgoFlag() == STATE_CLOSING_TRY_STOP_LOSS) {
		CloseLongPositionStopLossDo(position);
	} else if (asIs) {
		CloseLongPositionStopLossTry(position);
	} else {
		Assert(position.IsCloseCanceled());
		Assert(position.GetCloseType() == Position::CLOSE_TYPE_TAKE_PROFIT);
		position.ResetState();
		switch (m_settings.openOrderType) {
			case Settings::ORDER_TYPE_IOC:
				position.SetTakeProfit(position.GetTakeProfit() - GetSecurity()->Scale(.01));
				ReportCloseTry(position);
				security.SellOrCancel(
					position.GetOpenedQty() - position.GetClosedQty(),
					position.GetTakeProfit(),
					position);
				break;
			case Settings::ORDER_TYPE_MKT:
				ReportCloseTry(position);
				security.Sell(
					position.GetOpenedQty() - position.GetClosedQty(),
					position);
				position.SetCloseType(Position::CLOSE_TYPE_NONE);
				break;
			default:
				AssertFail("Unknown order type.");
				break;
		}
	}
}

void s::Algo::CloseShortPosition(Position &position, bool asIs) {
	Assert(position.GetType() == Position::TYPE_SHORT);
	Security &security = *GetSecurity();
	if (position.GetAlgoFlag() == STATE_OPENING) {
		if (asIs) {
			CloseShortPositionStopLossDo(position);
		} else {
			ReportCloseTry(position);
			security.Buy(position.GetOpenedQty(), position);
		}
		position.SetAlgoFlag(STATE_CLOSING);
	} else if (position.GetAlgoFlag() == STATE_CLOSING_TRY_STOP_LOSS) {
		CloseShortPositionStopLossDo(position);
	} else if (asIs) {
		CloseShortPositionStopLossTry(position);
	} else {
		Assert(position.IsCloseCanceled());
		Assert(position.GetCloseType() == Position::CLOSE_TYPE_TAKE_PROFIT);
		position.ResetState();
		switch (m_settings.openOrderType) {
			case Settings::ORDER_TYPE_IOC:
				position.SetTakeProfit(position.GetTakeProfit() + GetSecurity()->Scale(.01));
				ReportCloseTry(position);
				security.BuyOrCancel(
					position.GetOpenedQty() - position.GetClosedQty(),
					position.GetTakeProfit(),
					position);
				break;
			case Settings::ORDER_TYPE_MKT:
				ReportCloseTry(position);
				security.Buy(
					position.GetOpenedQty() - position.GetClosedQty(),
					position);
				position.SetCloseType(Position::CLOSE_TYPE_NONE);
				break;
			default:
				AssertFail("Unknown order type.");
				break;
		}
	}
}

void s::Algo::ClosePosition(Position &position, bool asIs) {

	Assert(
		position.GetAlgoFlag() == STATE_OPENING
		|| position.GetAlgoFlag() == STATE_CLOSING_TRY_STOP_LOSS
		|| position.GetAlgoFlag() == STATE_CLOSING);
	
	if (!position.IsOpened()) {
		Assert(!position.IsClosed());
		if (asIs) {
			GetSecurity()->CancelAllOrders();
		}
		return;
	} else if (position.IsClosed()) {
		return;
	} else if (position.GetAlgoFlag() == STATE_CLOSING && !position.IsCloseCanceled()) {
		return;
	} else if (!asIs && position.GetAlgoFlag() == STATE_OPENING) {
		Assert(!position.GetOpenTime().is_not_a_date_time());
		if (	!position.GetOpenTime().is_not_a_date_time()
				&& position.GetOpenTime() + m_settings.positionTimeSeconds > GetCurrentTime()) {
			return;
		}
	}

	switch (position.GetType()) {
		case Position::TYPE_LONG:
			CloseLongPosition(position, asIs);
			break;
		case Position::TYPE_SHORT:
			CloseShortPosition(position, asIs);
			break;
		default:
			AssertFail("Unknown position type.");
			break;
	}

	Assert(position.GetAlgoFlag() != STATE_OPENING);

}

void s::Algo::TryToClosePositions(PositionBandle &positions) {
	foreach (auto p, positions.Get()) {
		ClosePosition(*p, false);
	}
}

void s::Algo::ClosePositionsAsIs(PositionBandle &positions) {
	foreach (auto p, positions.Get()) {
		ClosePosition(*p, true);
	}
}

void s::Algo::ReportDecision(const Position &position) const {
	Log::Trading(
		logTag,
		"%1% %2% open-try size-ask-bid=%3%/%4% limit-used=%5% qty=%6% take-profit=%7% stop-loss=%8%",
		position.GetSecurity().GetSymbol(),
		position.GetTypeStr(),
		Security::Qty(position.GetDecisionAks()),
		Security::Qty(position.GetDecisionBid()),
		position.GetSecurity().Descale(position.GetStartPrice()),
		position.GetPlanedQty(),
		position.GetSecurity().Descale(position.GetTakeProfit()),
		position.GetSecurity().Descale(position.GetStopLoss()));
}

void s::Algo::ReportCloseTry(const Position &position) {
	Log::Trading(
		GetLogTag().c_str(),
		"%1% %2% close-try limit-price=%3% open-price=%4% opened-qty=%5%",
		position.GetSecurity().GetSymbol(),
		position.GetTypeStr(),
		position.GetSecurity().Descale(position.GetTakeProfit()),
		position.GetSecurity().Descale(position.GetOpenPrice()),
		position.GetOpenedQty() - position.GetClosedQty());

}

void s::Algo::ClosePositionStopLossTry(Position &position) {
	ReportStopLossTry(position);
	GetSecurity()->CancelAllOrders();
	position.SetAlgoFlag(STATE_CLOSING_TRY_STOP_LOSS);
}

void s::Algo::CloseLongPositionStopLossDo(Position &position) {
	Assert(position.GetType() == Position::TYPE_LONG);
	ReportStopLossDo(position);
	GetSecurity()->Sell(position.GetOpenedQty() - position.GetClosedQty(), position);
	position.SetAlgoFlag(STATE_CLOSING);
}

void s::Algo::CloseLongPositionStopLossTry(Position &position) {
	Assert(position.GetType() == Position::TYPE_LONG);
	ClosePositionStopLossTry(position);
}

void s::Algo::CloseShortPositionStopLossDo(Position &position) {
	Assert(position.GetType() == Position::TYPE_SHORT);
	ReportStopLossDo(position);
	GetSecurity()->Buy(position.GetOpenedQty() - position.GetClosedQty(), position);
	position.SetAlgoFlag(STATE_CLOSING);
}

void s::Algo::CloseShortPositionStopLossTry(Position &position) {
	Assert(position.GetType() == Position::TYPE_SHORT);
	ClosePositionStopLossTry(position);
}

