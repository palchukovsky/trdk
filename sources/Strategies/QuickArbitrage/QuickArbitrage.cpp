/**************************************************************************
 *   Created: 2012/07/10 01:25:51
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot
 **************************************************************************/

#include "Prec.hpp"
#include "QuickArbitrage.hpp"
#include "Core/PositionBundle.hpp"
#include "Core/Position.hpp"
#include "Core/PositionReporterAlgo.hpp"

namespace s = Strategies::QuickArbitrage;

namespace {

	enum PositionState {
		STATE_OPENING	= 1,
		STATE_CLOSING	= 2
	};

	const char *const logTag = "quick-arbitrage";

	const std::string algoName = "Quick Arbitrage";

}

s::Algo::Algo(
			boost::shared_ptr<DynamicSecurity> security,
			const IniFile &ini,
			const std::string &section)
		: Base(security) {
	DoSettingsUpodate(ini, section);
}

s::Algo::~Algo() {
	//...//
}

void s::Algo::UpdateAlogImplSettings(const IniFile &ini, const std::string &section) {
	DoSettingsUpodate(ini, section);
}

void s::Algo::DoSettingsUpodate(const IniFile &ini, const std::string &section) {
	
	Settings settings = {};
	settings.shortPos.isEnabled = ini.ReadBoolKey(section, "open_shorts");
	if (settings.shortPos.isEnabled) {
		settings.shortPos.priceMod
			= GetSecurity()->Scale(ini.ReadTypedKey<double>(section, "short_open_price"));
	}
	settings.longPos.isEnabled = ini.ReadBoolKey(section, "open_longs");
	if (settings.longPos.isEnabled) {
		settings.longPos.priceMod
			= GetSecurity()->Scale(ini.ReadTypedKey<double>(section, "long_open_price"));
	}
	settings.takeProfit
		= GetSecurity()->Scale(ini.ReadTypedKey<double>(section, "take_profit"));
	settings.stopLoss
		= GetSecurity()->Scale(ini.ReadTypedKey<double>(section, "stop_loss"));
	settings.volume
		= GetSecurity()->Scale(ini.ReadTypedKey<double>(section, "volume"));

	m_settings = settings;

	Log::Info(
		"Settings: algo \"%1%\" for \"%2%\":"
			" open_shorts = %3%; short_open_price = %4%; open_longs = %5%; long_open_price = %6%;"
			" take_profit = %7%; stop_loss = %8%; volume = %9%;",
		algoName,
		GetSecurity()->GetFullSymbol(),
		m_settings.shortPos.isEnabled ? "yes" : "no",
		GetSecurity()->Descale(m_settings.shortPos.priceMod),
		m_settings.longPos.isEnabled ? "yes" : "no",
		GetSecurity()->Descale(m_settings.longPos.priceMod),
		GetSecurity()->Descale(m_settings.takeProfit),
		GetSecurity()->Descale(m_settings.stopLoss),
		GetSecurity()->Descale(m_settings.volume));

}

const std::string & s::Algo::GetName() const {
	return algoName;
}

std::auto_ptr<PositionReporter> s::Algo::CreatePositionReporter() const {
	std::auto_ptr<PositionReporter> result(new PositionReporterAlgo<s::Algo>(*this));
	return result;
}

void s::Algo::Update() {
	AssertFail("Strategy logic error - method \"Update\" has been called");
}

boost::shared_ptr<PositionBandle> s::Algo::OpenPositions() {
	boost::shared_ptr<PositionBandle> result(new PositionBandle);
	if (m_settings.longPos.isEnabled) {
		result->Get().push_back(OpenLongPosition());
	}
	if (m_settings.shortPos.isEnabled) {
		result->Get().push_back(OpenShortPosition());
	}
	return result;
}

boost::shared_ptr<Position> s::Algo::OpenLongPosition() {
	
	DynamicSecurity &security = *GetSecurity();
	const auto ask = security.GetAskScaled();
	const auto bid = security.GetBidScaled();
	const auto price = ask - m_settings.longPos.priceMod;
	const auto takeProfit = price + m_settings.takeProfit;
	const auto stopLoss = price - m_settings.stopLoss;
	
	boost::shared_ptr<Position> result(
		new Position(
			GetSecurity(),
			Position::TYPE_LONG,
			CalcQty(price, m_settings.volume),
			price,
			ask,
			bid,
			takeProfit,
			stopLoss,
			STATE_OPENING));
	security.BuyOrCancel(result->GetPlanedQty(), result->GetStartPrice(), *result);

	return result;

}

boost::shared_ptr<Position> s::Algo::OpenShortPosition() {
	
	DynamicSecurity &security = *GetSecurity();
	const auto ask = security.GetAskScaled();
	const auto bid = security.GetBidScaled();
	const auto price = bid + m_settings.shortPos.priceMod;
	const auto takeProfit = price - m_settings.takeProfit;
	const auto stopLoss = price + m_settings.stopLoss;

	boost::shared_ptr<Position> result(
		new Position(
			GetSecurity(),
			Position::TYPE_SHORT,
			CalcQty(price, m_settings.volume),
			price,
			ask,
			bid,
			takeProfit,
			stopLoss,
			STATE_OPENING));
	security.SellOrCancel(result->GetPlanedQty(), result->GetStartPrice(), *result);
	
	return result;

}

void s::Algo::CloseLongPosition(Position &position) {
	Assert(position.GetType() == Position::TYPE_LONG);
	Assert(position.GetCloseType() != Position::CLOSE_TYPE_STOP_LOSS);
	DynamicSecurity &security = *GetSecurity();
	if (position.GetStopLoss() >= security.GetAskScaled()) {
		security.CancelAllOrders();
		ReportStopLoss(position);
		security.Sell(position.GetOpenedQty() - position.GetClosedQty(), position);
		position.SetCloseType(Position::CLOSE_TYPE_STOP_LOSS);
		return;
	}
	security.Sell(position.GetOpenedQty(), position.GetTakeProfit(), position);
}

void s::Algo::CloseShortPosition(Position &position) {
	Assert(position.GetType() == Position::TYPE_SHORT);
	Assert(position.GetCloseType() != Position::CLOSE_TYPE_STOP_LOSS);
	DynamicSecurity &security = *GetSecurity();
	if (position.GetStopLoss() <= security.GetBidScaled()) {
		security.CancelAllOrders();
		ReportStopLoss(position);
		security.Buy(position.GetOpenedQty() - position.GetClosedQty(), position);
		position.SetCloseType(Position::CLOSE_TYPE_STOP_LOSS);
		return;
	}
	security.Buy(position.GetOpenedQty(), position.GetTakeProfit(), position);
}

void s::Algo::ClosePosition(Position &position) {
	
	Assert(
		position.GetAlgoFlag() == STATE_OPENING
		|| position.GetAlgoFlag() == STATE_CLOSING);
	
	if (!position.IsOpened()) {
		return;
	} else if (position.IsCompleted()) {
		return;
	} else if (
			position.GetAlgoFlag() == STATE_CLOSING
			&& position.GetCloseType() == Position::CLOSE_TYPE_STOP_LOSS) {
		return;
	}
	
	switch (position.GetType()) {
		case Position::TYPE_LONG:
			CloseLongPosition(position);
			break;
		case Position::TYPE_SHORT:
			CloseShortPosition(position);
			break;
		default:
			AssertFail("Unknown position type.");
			break;
	}

}

void s::Algo::ClosePositions(PositionBandle &positions) {
	foreach (auto p, positions.Get()) {
		ClosePosition(*p);
	}
}

void s::Algo::ReportDecision(const Position &position) const {
	Log::Trading(
		logTag,
		"%1% %2% cur-ask-bid=%3%/%4% limit-used=%5% number-of-shares=%6% take-profit-price=%7% stop-loss-price=%8%",
		position.GetSecurity().GetSymbol(),
		position.GetTypeStr(),
		position.GetSecurity().Descale(position.GetDecisionAks()),
		position.GetSecurity().Descale(position.GetDecisionBid()),
		position.GetSecurity().Descale(position.GetStartPrice()),
		position.GetPlanedQty(),
		position.GetSecurity().Descale(position.GetTakeProfit()),
		position.GetSecurity().Descale(position.GetStopLoss()));
}

void s::Algo::ReportStopLoss(const Position &position) const {
	Log::Trading(
		logTag,
		"%1% %2% stop-loss cur-ask-bid=%3%/%4% stop-loss-price=%5%",
		position.GetSecurity().GetSymbol(),
		position.GetTypeStr(),
		position.GetSecurity().Descale(position.GetDecisionAks()),
		position.GetSecurity().Descale(position.GetDecisionBid()),
		position.GetSecurity().Descale(position.GetStopLoss()));
}
