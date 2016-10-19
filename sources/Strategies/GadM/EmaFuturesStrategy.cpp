/**************************************************************************
 *   Created: 2016/04/05 08:28:08
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#include "Prec.hpp"
#include "EmaFuturesStrategyPosition.hpp"
#include "Emas.hpp"
#include "Services/BarService.hpp"
#include "Core/Strategy.hpp"
#include "Core/MarketDataSource.hpp"
#include "Core/TradingLog.hpp"
#include "Core/Settings.hpp"
#include "Common/ExpirationCalendar.hpp"

using namespace trdk;
using namespace trdk::Lib;
using namespace trdk::Lib::TimeMeasurement;
using namespace trdk::Services;

namespace pt = boost::posix_time;
namespace fs = boost::filesystem;

////////////////////////////////////////////////////////////////////////////////

namespace {

	//! Profit taking level.
	/** @sa https://app.asana.com/0/196887491555385/192879506137993
	  */
	struct ProfitLevel {
		double volume;
		size_t numberOfContracts;
	};

	boost::optional<ProfitLevel> ReadProfitLevelConf(
			const IniSectionRef &conf) {
		
		const ProfitLevel result = {
			conf.ReadTypedKey<double>("profit_level_volume", 0),
			conf.ReadTypedKey<uint32_t>("profit_level_number_of_contracts", 0)
		};
		if (IsZero(result.volume) && !result.numberOfContracts) {
			return boost::none;
		}
		if (IsZero(result.volume) != !result.numberOfContracts) {
			throw Exception(
				"Should be set both settings \"profit_level_volume\""
					" and \"profit_level_number_of_contracts\"");
		}

		const auto numberOfContracts
			= conf.ReadTypedKey<uint32_t>("number_of_contracts");
		if (numberOfContracts < result.numberOfContracts) {
			throw Exception(
				"\"profit_level_number_of_contracts\" should be less than"
					" \"number_of_contracts\"");
		}
		if (numberOfContracts % result.numberOfContracts) {
			throw Exception(
				"\"profit_level_number_of_contracts\" should be multiple"
					" \"number_of_contracts\"");
		}

		return result;
		
	}

}

namespace trdk { namespace Strategies { namespace GadM {
namespace EmaFuturesStrategy {

	////////////////////////////////////////////////////////////////////////////////

	class Strategy : public trdk::Strategy {

	public:

		typedef trdk::Strategy Base;

	public:

		explicit Strategy(
				Context &context,
				const std::string &tag,
				const IniSectionRef &conf)
			: Base(
				context,
				boost::uuids::string_generator()(
					"{E316A97B-1C1F-433B-88BF-4DB788E94208}"),
				"EmaFutures",
				tag,
				conf)
			, m_numberOfContracts(
				conf.ReadTypedKey<uint32_t>("number_of_contracts"))
			, m_passiveOrderMaxLifetime(
				pt::seconds(
					conf.ReadTypedKey<unsigned int>(
						"passive_order_max_lifetime_sec")))
			, m_orderPriceMaxDelta(
				conf.ReadTypedKey<double>("order_price_max_delta"))
			, m_minProfitToActivateTakeProfit(
				conf.ReadTypedKey<double>(
					"min_profit_per_contract_to_activate_take_profit"))
			, m_takeProfitTrailingRatio(
				conf.ReadTypedKey<int>("take_profit_trailing_percentage") 
				/ 100.0)
			, m_maxLossMoneyPerContract(
				conf.ReadTypedKey<double>("max_loss_per_contract"))
			, m_profitLevel(ReadProfitLevelConf(conf))
			, m_security(nullptr)
			, m_barService(nullptr)
			, m_barServiceId(DropCopy::nDataSourceInstanceId)
			, m_fastEmaDirection(DIRECTION_LEVEL) {
			GetLog().Info(
				"Number of contracts: %1%."
					" Passive order max. lifetime: %2%."
					" Order price max. delta: %3%."
					" Take-profit trailing: %4%%%"
						" will be activated after profit %5% * %6% = %7%."
					" Max loss: %8% * %9% = %10%.",
				m_numberOfContracts, // 1
				m_passiveOrderMaxLifetime, // 2
				m_orderPriceMaxDelta, // 3
				int(m_takeProfitTrailingRatio * 100), // 4
				m_minProfitToActivateTakeProfit, // 5
				m_numberOfContracts, // 6
				m_minProfitToActivateTakeProfit * m_numberOfContracts, // 7
				m_maxLossMoneyPerContract, // 8
				m_numberOfContracts, // 9
				m_maxLossMoneyPerContract * m_numberOfContracts);
			if (m_profitLevel) {
				GetLog().Info(
					"Profit level volume: %1%, profit value size: %2%.",
					m_profitLevel->volume,
					m_profitLevel->numberOfContracts);
			} else {
				GetLog().Info("Profit level value is not set.");
			}
		}
		
		virtual ~Strategy() {
			//...//
		}

	public:

		virtual void OnSecurityStart(
				Security &security,
				Security::Request &request) {
			if (!m_security) {
				m_security = &security;
				GetLog().Info("Using \"%1%\" to trade...", *m_security);
			} else if (m_security != &security) {
				throw Exception(
					"Strategy can not work with more than one security");
			}
			Base::OnSecurityStart(security, request);
		}

		virtual void OnServiceStart(const Service &service) {
			
			const auto *const maService
				= dynamic_cast<const MovingAverageService *>(&service);
			if (maService) {

				DropCopy::DataSourceInstanceId dropCopySourceId
					 = DropCopy::nDataSourceInstanceId;
				GetContext().InvokeDropCopy(
					[this, &dropCopySourceId, maService](DropCopy &dropCopy) {
						dropCopySourceId = dropCopy.RegisterDataSourceInstance(
							*this,
							maService->GetTypeId(),
							maService->GetId());
					});
				const std::string dropCopySourceIdStr
					= dropCopySourceId !=  DropCopy::nDataSourceInstanceId
						?	boost::lexical_cast<std::string>(dropCopySourceId)
						:	std::string("not used");

				if (boost::iends_with(maService->GetTag(), "fastema")) {
					GetLog().Info(
						"Using EMA service \"%1%\" as fast EMA"
							" (drop copy ID: %2%)...",
						maService->GetTag(),
						dropCopySourceIdStr);
					if (m_ema[FAST].HasData()) {
						throw Exception(
							"Strategy should have only one fast EMA service");
					}
					m_ema[FAST] = Ema(*maService, dropCopySourceId);
				} else if (boost::iends_with(maService->GetTag(), "slowema")) {
					GetLog().Info(
						"Using EMA service \"%1%\" as slow EMA"
							" (drop copy ID: %2%)...",
						maService->GetTag(),
						dropCopySourceIdStr);
					if (m_ema[SLOW].HasData()) {
						throw Exception(
							"Strategy should have only one slow EMA service");
					}
					m_ema[SLOW] = Ema(*maService, dropCopySourceId);
				} else {
					GetLog().Error(
						"Failed to resolve EMA service \"%1%\".",
						maService->GetTag());
					throw Exception("Failed to resolve EMA service");
				}

				return;

			}

			const auto *const barService
				= dynamic_cast<const BarService *>(&service);
			if (barService) {

				if (m_barService) {
					throw Exception(
						"Strategy should have only one bar service");
				}
				
				GetContext().InvokeDropCopy(
					[this, &barService](DropCopy &dropCopy) {
						m_barServiceId = dropCopy.RegisterDataSourceInstance(
							*this,
							barService->GetTypeId(),
							barService->GetId());
						m_barService = barService;
						GetLog().Info(
							"Using Bar Service \"%1%\" with instance ID %2%"
								" to drop bars copies...",
							m_barServiceId,
							*m_barService);
					});

				return;

			}

			throw Exception(
				"Strategy should be configured"
					" to work with MovingAverageService");

		}

	protected:

		virtual void OnLevel1Update(
				Security &security,
				const Milestones &timeMeasurement) {

			Assert(security.IsActive());
			Assert(m_security == &security);
			UseUnused(security);

			if (m_barService) {
				m_barService->DropUncompletedBarCopy(m_barServiceId);
			}

			if (StartRollOver()) {
				return;
			}

			if (!GetPositions().GetSize()) {
				CheckStartegyLog();
				return;
			}

			auto &position
				= dynamic_cast<Position &>(*GetPositions().GetBegin());

			CheckSlowOrderFilling(position);
			CheckStopLoss(position, timeMeasurement)
				|| CheckTakeProfit(position, timeMeasurement)
				|| CheckProfitLevel(position, timeMeasurement);

		}
		
		virtual void OnPositionUpdate(trdk::Position &position) {

			Assert(m_security);
			Assert(m_security->IsActive());

			auto &startegyPosition = dynamic_cast<Position &>(position);
			CheckSlowOrderFilling(startegyPosition);
			startegyPosition.Sync();

			FinishRollOver(startegyPosition);

		}

		virtual void OnServiceDataUpdate(
				const Service &service,
				const Milestones &timeMeasurement) {

			if (&service == m_barService) {
				m_barService->DropLastBarCopy(m_barServiceId);
			} else {
				for (const Ema &ema: m_ema) {
					if (ema.IsSame(service)) {
						ema.DropLastPointCopy();
						break;
					}
				}
			}

			if (!m_security->IsActive()) {
				return;
			}

			if (StartRollOver()) {
				return;
			}

			m_ema[FAST].CheckSource(service, m_ema[SLOW])
				|| m_ema[SLOW].CheckSource(service, m_ema[FAST]);
			if (!IsDataActual()) {
				return;
			}
			
			const Direction &signal = UpdateDirection();
			Assert(m_security);
			if (signal != DIRECTION_LEVEL) {
				GetTradingLog().Write(
					"signal\t%1%\tslow-ema=%2%\tfast-ema=%3%"
						"\t%4%\tbid=%5%\task=%6%",
					[&](TradingRecord &record) {
						record
							% (signal == DIRECTION_UP ? "BUY" : "SELL")
							% m_ema[SLOW].GetValue()
							% m_ema[FAST].GetValue()
							% ConvertToPch(m_fastEmaDirection)
							% m_security->GetBidPrice()
							% m_security->GetAskPrice();
					});
			}

			GetPositions().GetSize() > 0
				?	CheckPositionCloseSignal(signal, timeMeasurement)
				:	CheckPositionOpenSignal(signal, timeMeasurement);

		}

		virtual void OnPostionsCloseRequest() {
			throw MethodDoesNotImplementedError(
				"EmaFuturesStrategy::OnPostionsCloseRequest"
					" is not implemented");
		}

		virtual void OnSecurityContractSwitched(
				const pt::ptime &time,
				Security &security,
				Security::Request &request) {

			if (&security != m_security) {
				Base::OnSecurityContractSwitched(time, security, request);
				return;
			}

			if (m_security->IsActive()) {
				StartRollOver();
			}

		}

	private:

		bool IsDataActual() {
			if (m_ema[FAST] && m_ema[SLOW]) {
				// Data started.
				Assert(m_ema[FAST].HasSource());
				Assert(m_ema[SLOW].HasSource());
				Assert(m_security);
				Assert(
					m_ema[FAST].GetNumberOfUpdates()
							== m_ema[SLOW].GetNumberOfUpdates()
					||	m_ema[FAST].GetNumberOfUpdates() + 1
							== m_ema[SLOW].GetNumberOfUpdates()
					||	m_ema[FAST].GetNumberOfUpdates()
							== m_ema[SLOW].GetNumberOfUpdates() + 1);
				return
					m_ema[FAST].GetNumberOfUpdates()
					== m_ema[SLOW].GetNumberOfUpdates();
			}
			if (!m_ema[FAST].HasSource() || !m_ema[SLOW].HasSource()) {
				throw Exception(
					"Strategy doesn't have one or more EMA sources");
			}
			// Data not collected yet.
			return false;
		}

		void CheckPositionOpenSignal(
				const Direction &signal,
				const Milestones &timeMeasurement) {

			Assert(m_security);
			AssertEq(0, GetPositions().GetSize());

			boost::shared_ptr<Position> position;
			switch (signal) {
				case DIRECTION_LEVEL:
					timeMeasurement.Measure(SM_STRATEGY_WITHOUT_DECISION_1);
					return;
				case DIRECTION_UP:
					position = CreatePosition<LongPosition>(
						signal,
						timeMeasurement);
					break;
				case DIRECTION_DOWN:
					position = CreatePosition<ShortPosition>(
						signal,
						timeMeasurement);
					break;
				default:
					throw LogicError("Internal error: Unknown direction");
			}
			Assert(position);

			position->Sync();
			timeMeasurement.Measure(SM_STRATEGY_EXECUTION_COMPLETE_1);
			Assert(position->HasActiveOpenOrders());

		}

		void CheckPositionCloseSignal(
				const Direction &signal,
				const Milestones &timeMeasurement) {

			Assert(m_security);
			AssertEq(1, GetPositions().GetSize());

			auto &position
				= dynamic_cast<Position &>(*GetPositions().GetBegin());
			if (position.GetIntention() > INTENTION_HOLD) {
				return;
			}

			if (CheckStopLoss(position, timeMeasurement)) {
				return;
			}

			switch (signal) {
				case  DIRECTION_LEVEL:
					return;
				case DIRECTION_UP:
					if (position.GetType() == Position::TYPE_LONG) {
						return;
					}
					break;
				case DIRECTION_DOWN:
					if (position.GetType() == Position::TYPE_SHORT) {
						return;
					}
					break;
			}

			if (!position.GetActiveQty()) {
				Assert(position.HasActiveOrders());
				Assert(position.HasActiveOpenOrders());
				GetTradingLog().Write(
					"slow-order\tsignal for empty order",
					[&](const TradingRecord &) {});
				position.SetIntention(
					INTENTION_DONOT_OPEN,
					Position::CLOSE_TYPE_OPEN_FAILED,
					DIRECTION_LEVEL);
				return;
			}

			position.SetIntention(
				INTENTION_CLOSE_PASSIVE,
				Position::CLOSE_TYPE_NONE,
				signal);
			timeMeasurement.Measure(SM_STRATEGY_EXECUTION_COMPLETE_2);

		}

		bool CheckTakeProfit(
				Position &position,
				const Milestones &timeMeasurement) {
			if (position.GetIntention() != INTENTION_HOLD) {
				return false;
			}
			Assert(position.GetActiveQty());
			const Position::PriceCheckResult result = position.CheckTakeProfit(
				m_minProfitToActivateTakeProfit,
				m_takeProfitTrailingRatio);
			if (result.isAllowed) {
				return false;
			}
			GetTradingLog().Write(
				"slow-order\ttake-profit"
					"\tmax_profit=%1%\tmargin=%2%\tcurrent=%3%"
					"\tpnl_rlz=%4%\tpnl_unr=%5%\tpnl_plan=%6%"
					"\topen_vol=%7%\topen_price=%8%\tclose_vol=%9%"
					"\tbid=%10%\task=%11%",
			[&](TradingRecord &record) {
				record
					%	position.GetSecurity().DescalePrice(result.start)
					%	position.GetSecurity().DescalePrice(result.margin)
					%	position.GetSecurity().DescalePrice(result.current)
					%	position.GetRealizedPnl()
					%	position.GetUnrealizedPnl()
					%	position.GetPlannedPnl()
					%	position.GetOpenedVolume()
					%	position.GetSecurity().DescalePrice(
							position.GetOpenPrice())
					%	position.GetClosedVolume()
					%	position.GetSecurity().GetBidPrice()
					%	position.GetSecurity().GetAskPrice();
			});
			position.SetIntention(
				INTENTION_CLOSE_PASSIVE,
				Position::CLOSE_TYPE_TAKE_PROFIT,
				DIRECTION_LEVEL);
			timeMeasurement.Measure(SM_STRATEGY_EXECUTION_COMPLETE_2);
			return true;
		}

		bool CheckProfitLevel(
				Position &position,
				const Milestones &timeMeasurement) {
			if (!m_profitLevel || position.GetIntention() != INTENTION_HOLD) {
				return false;
			}
			Assert(position.GetActiveQty());
			const Position::PriceCheckResult result = position.CheckProfitLevel(
				m_profitLevel->volume);
			if (result.isAllowed) {
				return false;
			}
			Qty intentionSize = 1;
			GetTradingLog().Write(
				"slow-order\tprofit-level"
					"\trealized=%1%\tmargin=%2%\tcurrent=%3%"
					"\tint_size=%13%"
					"\tpnl_rlz=%4%\tpnl_unr=%5%\tpnl_plan=%6%"
					"\topen_vol=%7%\topen_price=%8%"
					"\tclose_vol=%9%\tcur_vol=%10%"
					"\tbid=%11%\task=%12%",
			[&](TradingRecord &record) {
				record
					%	position.GetSecurity().DescalePrice(result.start)
					%	position.GetSecurity().DescalePrice(result.margin)
					%	position.GetSecurity().DescalePrice(result.current)
					%	position.GetRealizedPnl()
					%	position.GetUnrealizedPnl()
					%	position.GetPlannedPnl()
					%	position.GetOpenedVolume()
					%	position.GetSecurity().DescalePrice(
							position.GetOpenPrice())
					%	position.GetClosedVolume()
					%	position.GetActiveVolume()
					%	position.GetSecurity().GetBidPrice()
					%	position.GetSecurity().GetAskPrice()
					%	intentionSize;
			});
			position.SetIntention(
				INTENTION_CLOSE_PASSIVE,
				Position::CLOSE_TYPE_TAKE_PROFIT,
				DIRECTION_LEVEL,
				intentionSize);
			timeMeasurement.Measure(SM_STRATEGY_EXECUTION_COMPLETE_2);
			return true;
		}

		bool CheckStopLoss(
				Position &position,
				const Milestones &timeMeasurement) {
			if (position.GetIntention() != INTENTION_HOLD) {
				return false;
			}
			Assert(position.GetActiveQty());
			const Position::PriceCheckResult result
				= position.CheckStopLoss(m_maxLossMoneyPerContract);
			if (result.isAllowed) {
				return false;
			}
			GetTradingLog().Write(
				"slow-order\tstop-loss"
					"\tstart=%1%\tmargin=%2%\tnow=%3%"
					"\tbid=%4%\task=%5%",
			[&](TradingRecord &record) {
				record
					%	position.GetSecurity().DescalePrice(result.start)
					%	position.GetSecurity().DescalePrice(result.margin)
					%	position.GetSecurity().DescalePrice(result.current)
					%	position.GetSecurity().GetBidPrice()
					%	position.GetSecurity().GetAskPrice();
			});
			position.SetIntention(
				INTENTION_CLOSE_PASSIVE,
				Position::CLOSE_TYPE_STOP_LOSS,
				DIRECTION_LEVEL);
			timeMeasurement.Measure(SM_STRATEGY_EXECUTION_COMPLETE_2);
			return true;
		}

		void CheckSlowOrderFilling(Position &position) {
			
			if (!position.HasActiveOrders()) {
				return;
			}

			static_assert(numberOfIntentions == 6, "List changed.");
			switch (position.GetIntention()) {
				case INTENTION_HOLD:
				case INTENTION_DONOT_OPEN:
					return;
			}

			!CheckOrderAge(position) || !CheckOrderPrice(position);

		}

		bool CheckOrderAge(Position &position) {

			static_assert(numberOfIntentions == 6, "List changed.");
			switch (position.GetIntention()) {
				case INTENTION_OPEN_PASSIVE:
				case INTENTION_CLOSE_PASSIVE:
					break;
				default:
					return true;
			}

			const auto &startTime = position.HasActiveCloseOrders()
				?	position.GetCloseStartTime()
				:	position.GetStartTime();
			const auto orderExpirationTime
				=  startTime + m_passiveOrderMaxLifetime;
			const auto &now = GetContext().GetCurrentTime();
			if (orderExpirationTime > now) {
				return true;
			}

			GetTradingLog().Write(
				"slow-order\ttime\tstart=%1%\tmargin=%2%\tnow=%3%\t%4%(%5%)"
					"\tbid=%6%\task=%7%",
				[&](TradingRecord &record) {
					record
						%	startTime.time_of_day()
						%	orderExpirationTime.time_of_day()
						%	now.time_of_day()
						%	(position.GetIntention() == INTENTION_OPEN_PASSIVE
								||	position.GetIntention()
										== INTENTION_CLOSE_PASSIVE
							?	"passive"
							:	"aggressive")
						%	position.GetIntention()
						%	position.GetSecurity().GetBidPrice()
						%	position.GetSecurity().GetAskPrice();
				});

			position.SetIntention(
				position.GetIntention() == INTENTION_OPEN_PASSIVE
					?	INTENTION_OPEN_AGGRESIVE
					:	INTENTION_CLOSE_AGGRESIVE,
				Position::CLOSE_TYPE_NONE,
				DIRECTION_LEVEL);

			return false;

		}

		bool CheckOrderPrice(Position &position) {

			const Position::PriceCheckResult &result
				= position.CheckOrderPrice(m_orderPriceMaxDelta);
			if (result.isAllowed) {
				return true;
			}

			GetTradingLog().Write(
				"slow-order\tprice\tstart=%1%\tmargin=%2%\tnow=%3%\t%4%(%5%)"
					"\tbid=%6%\task=%7%",
				[&](TradingRecord &record) {
					record
						% position.GetSecurity().DescalePrice(result.start)
						% position.GetSecurity().DescalePrice(result.margin)
						% position.GetSecurity().DescalePrice(result.current)
						%	(position.GetIntention() == INTENTION_OPEN_PASSIVE
								||	position.GetIntention()
										== INTENTION_CLOSE_PASSIVE
							?	"passive"
							:	"aggressive")
						%	position.GetIntention()
						%	position.GetSecurity().GetBidPrice()
						%	position.GetSecurity().GetAskPrice();;
				});
			
			position.MoveOrderToCurrentPrice();
			
			return false;


		}

		Direction UpdateDirection() {
			
			auto fastEmaDirection
				= IsEqual(m_ema[FAST].GetValue(), m_ema[SLOW].GetValue())
					?	DIRECTION_LEVEL
					:	m_ema[FAST].GetValue() > m_ema[SLOW].GetValue()
						?	DIRECTION_UP
						:	DIRECTION_DOWN;

			if (m_fastEmaDirection == fastEmaDirection) {
				return DIRECTION_LEVEL;
			}

			std::swap(fastEmaDirection, m_fastEmaDirection);
			GetTradingLog().Write(
				"fast-ema\t%1%->%2%\tslow-ema=%3%\tfast-ema=%4%"
					"\tbid=%5%\task=%6%",
				[&](TradingRecord &record) {
					record
						% ConvertToPch(fastEmaDirection)
						% ConvertToPch(m_fastEmaDirection)
						% m_ema[SLOW].GetValue()
						% m_ema[FAST].GetValue()
						% m_security->GetBidPrice()
						% m_security->GetAskPrice();
				});

			switch (fastEmaDirection) {
				case  DIRECTION_LEVEL:
					// Intersection was at previous update.
					return DIRECTION_LEVEL;
				default:
					switch (m_fastEmaDirection) {
						case DIRECTION_UP:
						case DIRECTION_DOWN:
							return m_fastEmaDirection;
							break;
						case DIRECTION_LEVEL:
							return fastEmaDirection == DIRECTION_DOWN
								?	DIRECTION_UP
								:	DIRECTION_DOWN;
							break;
						default:
							throw LogicError(
								"Internal error: Unknown direction");
					}
			}

		}

		template<typename Position>
		boost::shared_ptr<Position> CreatePosition(
				const Direction &reason,
				const Milestones &timeMeasurement) {
			Assert(m_security);
			return boost::make_shared<Position>(
				*this,
				m_generateUuid(),
				GetTradingSystem(m_security->GetSource().GetIndex()),
				*m_security,
				m_numberOfContracts,
				timeMeasurement,
				reason,
				m_ema,
				m_strategyLog);
		}

		void CheckStartegyLog() {
			if (m_strategyLog.is_open()) {
				return;
			}
			OpenStartegyLog();
			Assert(m_strategyLog.is_open());
		}

		void OpenStartegyLog() {

			Assert(!m_strategyLog.is_open());

			fs::path path = GetContext().GetSettings().GetPositionsLogDir();

			if (!GetContext().GetSettings().IsReplayMode()) {
				boost::format fileName("%1%__%2%__%3%");
				fileName
					% GetTag()
					% ConvertToFileName(GetContext().GetStartTime())
					% GetId();
				path /= SymbolToFileName(fileName.str(), "csv");
			} else {
				boost::format fileName("%1%__%2%__%3%__%4%");
				fileName
					% GetTag()
					% ConvertToFileName(GetContext().GetCurrentTime())
					% ConvertToFileName(GetContext().GetStartTime())
					% GetId();
				path /= SymbolToFileName(fileName.str(), "csv");
			}
			
			fs::create_directories(path.branch_path());

			m_strategyLog.open(
				path.string(),
				std::ios::out | std::ios::ate | std::ios::app);
			if (!m_strategyLog) {
				GetLog().Error("Failed to open strategy log file %1%", path);
				throw Exception("Failed to open strategy log file");
			} else {
				GetLog().Info("Strategy log: %1%.", path);
			}

			Position::OpenReport(m_strategyLog);

		}

		bool StartRollOver() {

			if (!m_security->HasExpiration() || !GetPositions().GetSize()) {
				return false;
			}
			
			AssertEq(1, GetPositions().GetSize());

			auto &position
				= dynamic_cast<Position &>(*GetPositions().GetBegin());
			if (position.HasActiveCloseOrders()) {
				return false;
			}

			if (position.GetExpiration() == m_security->GetExpiration()) {
				return false;
			}

			GetTradingLog().Write(
				"rollover\texpiration=%1%\tposition=%2%",
				[&](TradingRecord &record) {
					record
						% m_security->GetExpiration().GetDate()
						% position.GetId();
				});

			position.SetIntention(
				INTENTION_CLOSE_PASSIVE,
				Position::CLOSE_TYPE_ROLLOVER,
				DIRECTION_LEVEL);

			return true;

		}

		void FinishRollOver(Position &oldPosition) {

			if (
					!m_security->HasExpiration()
					|| !oldPosition.IsCompleted()
					|| oldPosition.GetCloseType()
						!= Position::CLOSE_TYPE_ROLLOVER) {
				return;
			}

			AssertLt(0, oldPosition.GetOpenedQty());
			boost::shared_ptr<Position> newPosition;
			switch (oldPosition.GetType()) {
				case Position::TYPE_LONG:
					newPosition = CreatePosition<LongPosition>(
						oldPosition.GetOpenReason(),
						oldPosition.GetTimeMeasurement());
					break;
				case Position::TYPE_SHORT:
					newPosition = CreatePosition<ShortPosition>(
						oldPosition.GetOpenReason(),
						oldPosition.GetTimeMeasurement());
					break;
			}

			Assert(newPosition);
			newPosition->Sync();
			Assert(newPosition->HasActiveOpenOrders());

		}

	private:

		const Qty m_numberOfContracts;
		const pt::time_duration m_passiveOrderMaxLifetime;
		const double m_orderPriceMaxDelta;
		const double m_minProfitToActivateTakeProfit;
		const double m_takeProfitTrailingRatio;
		const double m_maxLossMoneyPerContract;
		const boost::optional<ProfitLevel> m_profitLevel;

		Security *m_security;

		const BarService *m_barService;
		DropCopy::DataSourceInstanceId m_barServiceId;

		SlowFastEmas m_ema;
		Direction m_fastEmaDirection;

		boost::uuids::random_generator m_generateUuid;

		std::ofstream m_strategyLog;

	};

} } } }

////////////////////////////////////////////////////////////////////////////////

TRDK_STRATEGY_GADM_API boost::shared_ptr<Strategy> CreateEmaFuturesStrategy(
		Context &context,
		const std::string &tag,
		const IniSectionRef &conf) {
	using namespace trdk::Strategies::GadM;
	return boost::make_shared<EmaFuturesStrategy::Strategy>(context, tag, conf);
}

////////////////////////////////////////////////////////////////////////////////
