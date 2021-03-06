/**************************************************************************
 *   Created: 2012/07/09 18:12:34
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#include "Prec.hpp"
#include "Strategy.hpp"
#include "MarketDataSource.hpp"
#include "Operation.hpp"
#include "RiskControl.hpp"
#include "Timer.hpp"

namespace mi = boost::multi_index;
namespace pt = boost::posix_time;
namespace ptr = boost::property_tree;
namespace sig = boost::signals2;
namespace uuids = boost::uuids;
using namespace trdk;
using namespace Lib;

//////////////////////////////////////////////////////////////////////////

namespace {

class PositionHolder {
 public:
  explicit PositionHolder(
      Position& position,
      const Position::StateUpdateConnection& stateUpdateConnection)
      : m_refCount(new size_t(1)),
        m_position(position.shared_from_this()),
        m_stateUpdateConnection(stateUpdateConnection) {
    Assert(m_stateUpdateConnection.connected());
  }

  PositionHolder(const PositionHolder& rhs)
      : m_refCount(rhs.m_refCount),
        m_position(rhs.m_position),
        m_stateUpdateConnection(rhs.m_stateUpdateConnection) {
    Verify(++*m_refCount > 1);
  }

  ~PositionHolder() {
    Assert(m_stateUpdateConnection.connected());
    AssertLt(0, *m_refCount);
    if (!--*m_refCount) {
      try {
        m_stateUpdateConnection.disconnect();
      } catch (...) {
        AssertFailNoException();
        terminate();
      }
      delete m_refCount;
    }
  }

 private:
  PositionHolder& operator=(const PositionHolder&);

 public:
  bool operator==(const Position& rhs) const { return GetPtr() == &rhs; }

  Position& operator*() const {
    return const_cast<PositionHolder*>(this)->Get();
  }

  Position* operator->() const {
    return const_cast<PositionHolder*>(this)->GetPtr();
  }

  Position& Get() { return *m_position; }

  const Position& Get() const {
    return const_cast<PositionHolder*>(this)->Get();
  }

  Position* GetPtr() { return &Get(); }

  const Position* GetPtr() const {
    return const_cast<PositionHolder*>(this)->GetPtr();
  }

 private:
  size_t* m_refCount;
  boost::shared_ptr<Position> m_position;
  Position::StateUpdateConnection m_stateUpdateConnection;
};

struct ByPtr {};

typedef boost::multi_index_container<
    PositionHolder,
    mi::indexed_by<
        mi::hashed_unique<mi::tag<ByPtr>,
                          mi::const_mem_fun<PositionHolder,
                                            const Position*,
                                            &PositionHolder::GetPtr>>>>
    PositionHolderList;
}  // namespace

////////////////////////////////////////////////////////////////////////////////

class Strategy::PositionList::Iterator::Implementation {
 public:
  PositionHolderList::iterator iterator;
};

class Strategy::PositionList::ConstIterator::Implementation {
 public:
  PositionHolderList::const_iterator iterator;
};

Strategy::PositionList::Iterator::Iterator(
    std::unique_ptr<Implementation>&& pimpl)
    : m_pimpl(std::move(pimpl)) {
  Assert(m_pimpl);
}

Strategy::PositionList::Iterator::Iterator(const Iterator& rhs)
    : m_pimpl(boost::make_unique<Implementation>(*rhs.m_pimpl)) {}

Strategy::PositionList::Iterator::~Iterator() = default;

Strategy::PositionList::Iterator& Strategy::PositionList::Iterator::operator=(
    const Iterator& rhs) {
  Assert(this != &rhs);
  Iterator(rhs).Swap(*this);
  return *this;
}

void Strategy::PositionList::Iterator::Swap(Iterator& rhs) noexcept {
  Assert(this != &rhs);
  std::swap(m_pimpl, rhs.m_pimpl);
}

Position& Strategy::PositionList::Iterator::dereference() const {
  return **m_pimpl->iterator;
}

bool Strategy::PositionList::Iterator::equal(const Iterator& rhs) const {
  return m_pimpl->iterator == rhs.m_pimpl->iterator;
}

bool Strategy::PositionList::Iterator::equal(const ConstIterator& rhs) const {
  return m_pimpl->iterator == rhs.m_pimpl->iterator;
}

void Strategy::PositionList::Iterator::increment() { ++m_pimpl->iterator; }

Strategy::PositionList::ConstIterator::ConstIterator(
    std::unique_ptr<Implementation>&& pimpl)
    : m_pimpl(std::move(pimpl)) {
  Assert(m_pimpl);
}

Strategy::PositionList::ConstIterator::ConstIterator(const Iterator& rhs)
    : m_pimpl(boost::make_unique<Implementation>({rhs.m_pimpl->iterator})) {}

Strategy::PositionList::ConstIterator::ConstIterator(const ConstIterator& rhs)
    : m_pimpl(boost::make_unique<Implementation>(*rhs.m_pimpl)) {}

Strategy::PositionList::ConstIterator::~ConstIterator() = default;

Strategy::PositionList::ConstIterator& Strategy::PositionList::ConstIterator::
operator=(const ConstIterator& rhs) {
  Assert(this != &rhs);
  ConstIterator(rhs).Swap(*this);
  return *this;
}

void Strategy::PositionList::ConstIterator::Swap(ConstIterator& rhs) {
  Assert(this != &rhs);
  std::swap(m_pimpl, rhs.m_pimpl);
}

const Position& Strategy::PositionList::ConstIterator::dereference() const {
  return **m_pimpl->iterator;
}

bool Strategy::PositionList::ConstIterator::equal(
    const ConstIterator& rhs) const {
  Assert(this != &rhs);
  return m_pimpl->iterator == rhs.m_pimpl->iterator;
}

bool Strategy::PositionList::ConstIterator::equal(const Iterator& rhs) const {
  return m_pimpl->iterator == rhs.m_pimpl->iterator;
}

void Strategy::PositionList::ConstIterator::increment() { ++m_pimpl->iterator; }

namespace {
class PositionMutableList : public Strategy::PositionList {
 public:
  ~PositionMutableList() override = default;

  void Insert(const PositionHolder&& holder) {
    Verify(m_impl.emplace(std::move(holder)).second);
  }

  void Erase(const Position& position) {
    AssertLt(0, m_impl.get<ByPtr>().count(&position));
    m_impl.get<ByPtr>().erase(&position);
  }

  bool Has(const Position& position) const {
    return m_impl.get<ByPtr>().count(&position) > 0;
  }

  size_t GetSize() const override { return m_impl.size(); }

  bool IsEmpty() const override { return m_impl.empty(); }

  Iterator GetBegin() override {
    return Iterator(
        boost::make_unique<Iterator::Implementation>({m_impl.begin()}));
  }

  ConstIterator GetBegin() const override {
    return ConstIterator(
        boost::make_unique<ConstIterator::Implementation>({m_impl.begin()}));
  }

  Iterator GetEnd() override {
    return Iterator(
        boost::make_unique<Iterator::Implementation>({m_impl.end()}));
  }

  ConstIterator GetEnd() const override {
    return ConstIterator(
        boost::make_unique<ConstIterator::Implementation>({m_impl.end()}));
  }

 private:
  PositionHolderList m_impl;
};

}  // namespace

////////////////////////////////////////////////////////////////////////////////

namespace {

class ThreadPositionListTransaction : public Strategy::PositionListTransaction {
 public:
  class Data : public PositionListTransaction::Data {
   public:
    explicit Data(PositionMutableList& list) : m_originalList(list) {}

    ~Data() {
      try {
        const auto& end = m_inserted.cend();
        for (auto i = m_inserted.begin(); i != end; ++i) {
          m_originalList.Insert(std::move(*i));
        }
      } catch (...) {
        AssertFailNoException();
        terminate();
      }
    }

    void Insert(PositionHolder&& position) {
      Assert(std::find(m_inserted.cbegin(), m_inserted.cend(), *position) ==
             m_inserted.cend());
      m_inserted.emplace_back(std::move(position));
    }

    void Erase(const Position& position) {
      // Supported only current list as another was not required before.
      const auto it = std::find(m_inserted.begin(), m_inserted.end(), position);
      Assert(it != m_inserted.cend());
      if (it == m_inserted.cend()) {
        return;
      }
      m_inserted.erase(it);
    }

   private:
    PositionMutableList& m_originalList;
    std::list<PositionHolder> m_inserted;
  };

  explicit ThreadPositionListTransaction(PositionMutableList& list) {
    if (IsStarted()) {
      throw SystemException("Thread position list transaction already started");
    }
    m_instance.reset(new Data(list));
  }

  ~ThreadPositionListTransaction() override { m_instance.reset(); }

  std::unique_ptr<PositionListTransaction::Data> MoveToThread() override {
    Assert(IsStarted());
    return std::unique_ptr<PositionListTransaction::Data>(m_instance.release());
  }

  static bool IsStarted() { return m_instance.get() ? true : false; }

  static Data& GetData() {
    Assert(m_instance.get());
    Assert(IsStarted());
    return *m_instance.get();
  }

 private:
  static boost::thread_specific_ptr<Data> m_instance;
};
}  // namespace

////////////////////////////////////////////////////////////////////////////////

class Strategy::Implementation : private boost::noncopyable {
 public:
  typedef boost::mutex BlockMutex;
  typedef BlockMutex::scoped_lock BlockLock;
  typedef boost::condition_variable StopCondition;

  template <typename SlotSignature>
  struct SignalTrait {
    typedef sig::signal<
        SlotSignature,
        sig::optional_last_value<
            typename boost::function_traits<SlotSignature>::result_type>,
        int,
        std::less<int>,
        boost::function<SlotSignature>,
        typename sig::detail::extended_signature<
            boost::function_traits<SlotSignature>::arity,
            SlotSignature>::function_type,
        sig::dummy_mutex>
        Signal;
  };

  Strategy& m_strategy;
  const uuids::uuid m_typeId;
  const TradingMode m_tradingMode;

  const std::unique_ptr<RiskControlScope> m_riskControlScope;

  boost::atomic_bool m_isEnabled;

  boost::atomic_bool m_isBlocked;
  pt::ptime m_blockEndTime;
  BlockMutex m_blockMutex;
  StopCondition m_stopCondition;
  StopMode m_stopMode;

  PositionMutableList m_positions;
  sig::signal<PositionUpdateSlotSignature> m_positionUpdateSignal;

  boost::shared_mutex m_profitRatioMutex;
  boost::optional<std::pair<Double, bool>> m_profitOpportunity;
  SignalTrait<ProfitScannerSlotSignature>::Signal m_profitScannerSignal;

  std::vector<Position*> m_delayedPositionToForget;

  TimerScope m_timerScope;

  explicit Implementation(Strategy& strategy,
                          const uuids::uuid& typeId,
                          const ptr::ptree& conf)
      : m_strategy(strategy),
        m_typeId(typeId),
        m_tradingMode(
            ConvertTradingModeFromString(conf.get<std::string>("tradingMode"))),
        m_riskControlScope(
            m_strategy.GetContext()
                .GetRiskControl(m_tradingMode)
                .CreateScope(m_strategy.GetInstanceName(), conf)),
        m_isEnabled(conf.get<bool>("isEnabled")),
        m_isBlocked(false),
        m_stopMode(STOP_MODE_UNKNOWN) {
    m_strategy.GetLog().Info(
        "%1%, %2% mode.",
        m_isEnabled ? "ENABLED" : "DISABLED",                   // 1
        boost::to_upper_copy(ConvertToString(m_tradingMode)));  // 2
  }

  void ForgetPosition(const Position& position) {
    Assert(std::find(m_delayedPositionToForget.cbegin(),
                     m_delayedPositionToForget.cend(),
                     &position) == m_delayedPositionToForget.cend());
    ThreadPositionListTransaction::IsStarted()
        ? ThreadPositionListTransaction::GetData().Erase(position)
        : m_positions.Erase(position);
  }

  void BlockByRiskControlEvent(const RiskControlException& ex,
                               const char* action) const {
    boost::format message("Risk Control event: \"%1%\" (at %2%).");
    message % ex % action;
    m_strategy.Block(message.str());
  }

  void Block(const std::string* reason = nullptr) noexcept {
    try {
      const BlockLock lock(m_blockMutex);
      m_isBlocked = true;
      m_blockEndTime = pt::not_a_date_time;
      reason ? m_strategy.GetLog().Error("Blocked by reason: \"%s\".", *reason)
             : m_strategy.GetLog().Error("Blocked.");
      m_stopCondition.notify_all();
    } catch (...) {
      AssertFailNoException();
      raise(SIGTERM);  // is it can mutex or notify_all, also see "include"
    }
    if (!m_strategy.OnBlocked(reason)) {
      return;
    }
    try {
      reason ? m_strategy.GetContext().RaiseStateUpdate(
                   Context::STATE_STRATEGY_BLOCKED, *reason)
             : m_strategy.GetContext().RaiseStateUpdate(
                   Context::STATE_STRATEGY_BLOCKED);
    } catch (...) {
      AssertFailNoException();
    }
  }

  void RunAllAlgos() {
    // Not supported as was not required before:
    Assert(!ThreadPositionListTransaction::IsStarted());

    const auto& end = m_positions.GetEnd();
    for (auto it = m_positions.GetBegin(); it != end; ++it) {
      it->RunAlgos();
    }
  }

  void RaiseSinglePositionUpdateEvent(Position& position) {
    // Not supported as was not required before:
    Assert(!ThreadPositionListTransaction::IsStarted());

    Assert(m_positions.Has(position));

    if (position.IsError()) {
      m_strategy.GetLog().Error("Will be blocked by position error...");
      m_strategy.Block();
      ForgetPosition(position);
      return;
    }

    const bool isCompleted = position.IsCompleted();
    try {
      Assert(!position.IsCancelling());
      position.RunAlgos();
      if (!position.IsCancelling()) {
        m_strategy.OnPositionUpdate(position);
      }
    } catch (const RiskControlException& ex) {
      BlockByRiskControlEvent(ex, "position update");
      return;
    } catch (const Exception& ex) {
      m_strategy.Block(ex.what());
      return;
    }

    if (isCompleted) {
      ForgetPosition(position);
    }
  }

  void FlushDelayed(Lock& lock) {
    // Not supported as was not required before:
    Assert(!ThreadPositionListTransaction::IsStarted());

    while (!m_delayedPositionToForget.empty()) {
      lock.unlock();
      boost::this_thread::yield();
      lock.lock();
      if (m_delayedPositionToForget.empty()) {
        break;
      }
      Assert(m_delayedPositionToForget.back());
      auto& delayedPosition = *m_delayedPositionToForget.back();
      m_delayedPositionToForget.pop_back();
      Assert(delayedPosition.IsCompleted());
      // It may also be in the ThreadPositionListTransaction::GetInstance by
      // this case was not required before as no one calls "make completed"
      // from async task:
      Assert(m_positions.Has(delayedPosition));
      RaiseSinglePositionUpdateEvent(delayedPosition);
      Assert(!m_positions.Has(delayedPosition) || m_strategy.IsBlocked());
    }
  }

  template <typename Callback>
  void SignalByScheduledEvent(const Callback& callback) {
    auto lock = m_strategy.LockForOtherThreads();
    if (m_strategy.IsBlocked()) {
      return;
    }
    try {
      callback();
    } catch (const RiskControlException& ex) {
      BlockByRiskControlEvent(ex, "scheduled event");
      return;
    } catch (const Exception& ex) {
      m_strategy.Block(ex.what());
      return;
    }
    FlushDelayed(lock);
  }
};

boost::thread_specific_ptr<ThreadPositionListTransaction::Data>
    ThreadPositionListTransaction::m_instance;

//////////////////////////////////////////////////////////////////////////

namespace {
const std::string typeName = "Strategy";
}

Strategy::Strategy(Context& context,
                   const uuids::uuid& typeId,
                   const std::string& implementationName,
                   const std::string& instanceName,
                   const ptr::ptree& conf)
    : Consumer(context, typeName, implementationName, instanceName, conf),
      m_pimpl(boost::make_unique<Implementation>(*this, typeId, conf)) {}

Strategy::Strategy(Context& context,
                   const std::string& typeId,
                   const std::string& implementationName,
                   const std::string& instanceName,
                   const ptr::ptree& conf)
    : Consumer(context, typeName, implementationName, instanceName, conf),
      m_pimpl(boost::make_unique<Implementation>(
          *this, uuids::string_generator()(typeId), conf)) {}

Strategy::~Strategy() {
  Assert(!ThreadPositionListTransaction::IsStarted());
  try {
    if (!m_pimpl->m_positions.IsEmpty()) {
      GetLog().Info("%1% active position(s).", m_pimpl->m_positions.GetSize());
    }
  } catch (...) {
    AssertFailNoException();
  }
}

const uuids::uuid& Strategy::GetTypeId() const { return m_pimpl->m_typeId; }

TradingMode Strategy::GetTradingMode() const { return m_pimpl->m_tradingMode; }

RiskControlScope& Strategy::GetRiskControlScope() {
  return *m_pimpl->m_riskControlScope;
}

TradingSystem& Strategy::GetTradingSystem(const size_t index) {
  return GetContext().GetTradingSystem(index, GetTradingMode());
}

const TradingSystem& Strategy::GetTradingSystem(const size_t index) const {
  return const_cast<Strategy*>(this)->GetTradingSystem(index);
}

TradingSystem& Strategy::GetTradingSystem(const Security& security) {
  return GetTradingSystem(security.GetSource().GetIndex());
}

const TradingSystem& Strategy::GetTradingSystem(
    const Security& security) const {
  return const_cast<Strategy*>(this)->GetTradingSystem(security);
}

void Strategy::OnLevel1Update(Security& security,
                              const TimeMeasurement::Milestones&) {
  GetLog().Error(
      "Subscribed to %1% level 1 updates, but can't work with it"
      " (doesn't have OnLevel1Update method implementation).",
      security);
  throw MethodIsNotImplementedException(
      "Module subscribed to level 1 updates, but can't work with it");
}

void Strategy::OnPositionUpdate(Position&) {}

void Strategy::OnBookUpdateTick(Security& security,
                                const PriceBook&,
                                const TimeMeasurement::Milestones&) {
  GetLog().Error(
      "Subscribed to %1% book Update ticks, but can't work with it"
      " (doesn't have OnBookUpdateTick method implementation).",
      security);
  throw MethodIsNotImplementedException(
      "Module subscribed to book Update ticks, but can't work with it");
}

void Strategy::Register(Position& position) {
  PositionHolder holder(position, position.Subscribe([this, &position]() {
    m_pimpl->m_positionUpdateSignal(position);
  }));
  ThreadPositionListTransaction::IsStarted()
      ? ThreadPositionListTransaction::GetData().Insert(std::move(holder))
      : m_pimpl->m_positions.Insert(std::move(holder));
}

void Strategy::Unregister(Position& position) noexcept {
  try {
    ThreadPositionListTransaction::IsStarted()
        ? ThreadPositionListTransaction::GetData().Erase(position)
        : m_pimpl->m_positions.Erase(position);
  } catch (...) {
    AssertFailNoException();
    Block();
  }
}

void Strategy::RaiseLevel1UpdateEvent(
    Security& security, const TimeMeasurement::Milestones& delayMeasurement) {
  auto lock = LockForOtherThreads();
  // 1st time already checked: before enqueue event (without locking), here -
  // control check (under mutex as blocking and enabling - under the mutex too):
  if (IsBlocked()) {
    return;
  }
  delayMeasurement.Measure(TimeMeasurement::SM_DISPATCHING_DATA_RAISE);
  try {
    m_pimpl->RunAllAlgos();
    OnLevel1Update(security, delayMeasurement);
  } catch (const RiskControlException& ex) {
    m_pimpl->BlockByRiskControlEvent(ex, "level 1 update");
    return;
  } catch (const Exception& ex) {
    Block(ex.what());
    return;
  }
  m_pimpl->FlushDelayed(lock);
}

void Strategy::RaiseLevel1TickEvent(
    Security& security,
    const boost::posix_time::ptime& time,
    const Level1TickValue& value,
    const TimeMeasurement::Milestones& delayMeasurement) {
  auto lock = LockForOtherThreads();
  // 1st time already checked: before enqueue event (without locking), here -
  // control check (under mutex as blocking and enabling - under the mutex too):
  if (IsBlocked()) {
    return;
  }
  delayMeasurement.Measure(TimeMeasurement::SM_DISPATCHING_DATA_RAISE);
  try {
    m_pimpl->RunAllAlgos();
    OnLevel1Tick(security, time, value, delayMeasurement);
  } catch (const RiskControlException& ex) {
    m_pimpl->BlockByRiskControlEvent(ex, "level 1 tick");
    return;
  } catch (const Exception& ex) {
    Block(ex.what());
    return;
  }
  m_pimpl->FlushDelayed(lock);
}

void Strategy::RaiseNewTradeEvent(Security& service,
                                  const boost::posix_time::ptime& time,
                                  const Price& price,
                                  const Qty& qty) {
  auto lock = LockForOtherThreads();
  // 1st time already checked: before enqueue event (without locking), here -
  // control check (under mutex as blocking and enabling - under the mutex too):
  if (IsBlocked()) {
    return;
  }
  try {
    OnNewTrade(service, time, price, qty);
  } catch (const RiskControlException& ex) {
    m_pimpl->BlockByRiskControlEvent(ex, "new trade");
    return;
  } catch (const Exception& ex) {
    Block(ex.what());
    return;
  }
  m_pimpl->FlushDelayed(lock);
}

void Strategy::RaisePositionUpdateEvent(Position& position) {
  auto lock = LockForOtherThreads();
  // 1st time already checked: before enqueue event (without locking), here -
  // control check (under mutex as blocking and enabling - under the mutex too):
  if (IsBlocked()) {
    return;
  }
  if (position.IsCompleted() && !m_pimpl->m_positions.Has(position)) {
    return;
  }
  m_pimpl->RaiseSinglePositionUpdateEvent(position);
  m_pimpl->FlushDelayed(lock);
}

void Strategy::OnPositionMarkedAsCompleted(Position& position) {
  //! @todo Extend: delay several positions, don't remove from callback,
  //! don't forget about all other callbacks where positions can be used.
  Assert(position.IsCompleted());
  Assert(std::find(m_pimpl->m_delayedPositionToForget.cbegin(),
                   m_pimpl->m_delayedPositionToForget.cend(),
                   &position) == m_pimpl->m_delayedPositionToForget.cend());
  m_pimpl->m_delayedPositionToForget.emplace_back(&position);
}

void Strategy::RaiseSecurityContractSwitchedEvent(const pt::ptime& time,
                                                  Security& security,
                                                  Security::Request& request,
                                                  bool& isSwitched) {
  auto lock = LockForOtherThreads();
  // 1st time already checked: before enqueue event (without locking), here -
  // control check (under mutex as blocking and enabling - under the mutex too):
  if (IsBlocked()) {
    return;
  }
  try {
    OnSecurityContractSwitched(time, security, request, isSwitched);
  } catch (const RiskControlException& ex) {
    m_pimpl->BlockByRiskControlEvent(ex, "security contract switched");
    return;
  } catch (const Exception& ex) {
    Block(ex.what());
    return;
  }
  m_pimpl->FlushDelayed(lock);
}

void Strategy::RaiseBrokerPositionUpdateEvent(Security& security,
                                              bool isLong,
                                              const Qty& qty,
                                              const Volume& volume,
                                              bool isInitial) {
  auto lock = LockForOtherThreads();
  // 1st time already checked: before enqueue event (without locking), here -
  // control check (under mutex as blocking and enabling - under the mutex too):
  if (IsBlocked()) {
    return;
  }
  try {
    OnBrokerPositionUpdate(security, isLong, qty, volume, isInitial);
  } catch (const RiskControlException& ex) {
    m_pimpl->BlockByRiskControlEvent(ex, "broker position update");
    return;
  } catch (const Exception& ex) {
    Block(ex.what());
    return;
  }
  m_pimpl->FlushDelayed(lock);
}

void Strategy::RaiseNewBarEvent(Security& security, const Bar& bar) {
  auto lock = LockForOtherThreads();
  // 1st time already checked: before enqueue event (without locking), here -
  // control check (under mutex as blocking and enabling - under the mutex too):
  if (IsBlocked()) {
    return;
  }
  try {
    OnBarUpdate(security, bar);
  } catch (const RiskControlException& ex) {
    m_pimpl->BlockByRiskControlEvent(ex, "new bar");
    return;
  } catch (const Exception& ex) {
    Block(ex.what());
    return;
  }
  m_pimpl->FlushDelayed(lock);
}

void Strategy::RaiseBookUpdateTickEvent(
    Security& security,
    const PriceBook& book,
    const TimeMeasurement::Milestones& timeMeasurement) {
  auto lock = LockForOtherThreads();
  // 1st time already checked: before enqueue event (without locking), here -
  // control check (under mutex as blocking and enabling - under the mutex too):
  if (IsBlocked()) {
    return;
  }
  timeMeasurement.Measure(TimeMeasurement::SM_DISPATCHING_DATA_RAISE);
  try {
    OnBookUpdateTick(security, book, timeMeasurement);
  } catch (const RiskControlException& ex) {
    m_pimpl->BlockByRiskControlEvent(ex, "book update tick");
    return;
  } catch (const Exception& ex) {
    Block(ex.what());
    return;
  }
  m_pimpl->FlushDelayed(lock);
}

void Strategy::RaiseSecurityServiceEvent(const pt::ptime& time,
                                         Security& security,
                                         const Security::ServiceEvent& event) {
  auto lock = LockForOtherThreads();
  // 1st time already checked: before enqueue event (without locking), here -
  // control check (under mutex as blocking and enabling - under the mutex too):
  if (IsBlocked()) {
    return;
  }
  try {
    OnSecurityServiceEvent(time, security, event);
  } catch (const RiskControlException& ex) {
    m_pimpl->BlockByRiskControlEvent(ex, "security service event");
    return;
  } catch (const Exception& ex) {
    Block(ex.what());
    return;
  }
  m_pimpl->FlushDelayed(lock);
}

bool Strategy::IsBlocked(bool isForever) const {
  if (!m_pimpl->m_isEnabled) {
    return true;
  }

  if (!m_pimpl->m_isBlocked) {
    return false;
  }

  const Implementation::BlockLock lock(m_pimpl->m_blockMutex);

  if (m_pimpl->m_blockEndTime == pt::not_a_date_time ||
      m_pimpl->m_blockEndTime > GetContext().GetCurrentTime()) {
    return true;
  }
  if (isForever) {
    return false;
  }

  m_pimpl->m_blockEndTime = pt::not_a_date_time;
  m_pimpl->m_isBlocked = false;

  GetLog().Info("Unblocked.");

  return false;
}

void Strategy::Block() noexcept { m_pimpl->Block(); }

void Strategy::Block(const std::string& reason) noexcept {
  m_pimpl->Block(&reason);
}

void Strategy::Block(const pt::time_duration& blockDuration) {
  const Implementation::BlockLock lock(m_pimpl->m_blockMutex);
  const pt::ptime& blockEndTime = GetContext().GetCurrentTime() + blockDuration;
  if (m_pimpl->m_isBlocked && m_pimpl->m_blockEndTime != pt::not_a_date_time &&
      blockEndTime <= m_pimpl->m_blockEndTime) {
    return;
  }
  m_pimpl->m_isBlocked = true;
  m_pimpl->m_blockEndTime = blockEndTime;
  GetLog().Warn("Blocked until %1%.", m_pimpl->m_blockEndTime);
}

void Strategy::Stop(const StopMode& stopMode) {
  const auto lock = LockForOtherThreads();
  m_pimpl->m_stopMode = stopMode;
  OnStopRequest(stopMode);
}

void Strategy::OnStopRequest(const StopMode&) { ReportStop(); }

void Strategy::ReportStop() {
  const Implementation::BlockLock lock(m_pimpl->m_blockMutex);

  static_assert(numberOfStopModes == 3, "Stop mode list changed.");
  switch (GetStopMode()) {
    case STOP_MODE_GRACEFULLY_ORDERS:
      for (const auto& pos : GetPositions()) {
        if (pos.HasActiveOrders()) {
          GetLog().Error(
              "Found position %1%/%2% with active orders at stop"
              " with mode \"wait for orders before\".",
              pos.GetOperation()->GetId(),  // 1
              pos.GetSubOperationId());     // 2
        }
        Assert(!pos.HasActiveOrders());
      }
      break;
    case STOP_MODE_GRACEFULLY_POSITIONS:
      if (!GetPositions().IsEmpty()) {
        GetLog().Error(
            "Found %1% active positions at stop"
            " with mode \"wait for positions before\".",
            GetPositions().GetSize());
        Assert(GetPositions().IsEmpty());
      }
      break;
    case STOP_MODE_UNKNOWN:
      throw LogicError("Strategy stop not requested");
    default:
      break;
  }

  m_pimpl->m_isBlocked = true;
  m_pimpl->m_blockEndTime = pt::not_a_date_time;

  GetLog().Info("Stopped.");
  m_pimpl->m_stopCondition.notify_all();
}

const StopMode& Strategy::GetStopMode() const { return m_pimpl->m_stopMode; }

void Strategy::WaitForStop() {
  Implementation::BlockLock lock(m_pimpl->m_blockMutex);
  if (m_pimpl->m_isBlocked && m_pimpl->m_blockEndTime == pt::not_a_date_time) {
    return;
  }
  m_pimpl->m_stopCondition.wait(lock);
  Assert(m_pimpl->m_isBlocked);
  AssertEq(m_pimpl->m_blockEndTime, pt::not_a_date_time);
}

Strategy::PositionUpdateSlotConnection Strategy::SubscribeToPositionsUpdates(
    const PositionUpdateSlot& slot) const {
  return m_pimpl->m_positionUpdateSignal.connect(slot);
}

Strategy::PositionList& Strategy::GetPositions() {
  // Not supported as was not required before:
  Assert(!ThreadPositionListTransaction::IsStarted());
  return m_pimpl->m_positions;
}

const Strategy::PositionList& Strategy::GetPositions() const {
  return const_cast<Strategy*>(this)->GetPositions();
}

std::unique_ptr<Strategy::PositionListTransaction>
Strategy::StartThreadPositionsTransaction() {
  return boost::make_unique<ThreadPositionListTransaction>(
      m_pimpl->m_positions);
}

void Strategy::OnSettingsUpdate(const ptr::ptree& conf) {
  Consumer::OnSettingsUpdate(conf);

  if (m_pimpl->m_isEnabled != conf.get<bool>("isEnabled")) {
    m_pimpl->m_isEnabled = !m_pimpl->m_isEnabled;
    GetLog().Info("%1%.", m_pimpl->m_isEnabled ? "ENABLED" : "DISABLED");
  }

  m_pimpl->m_riskControlScope->OnSettingsUpdate(conf);
}

void Strategy::ClosePositions() {
  const auto lock = LockForOtherThreads();
  GetLog().Info("Closing positions by request...");
  OnPostionsCloseRequest();
}

bool Strategy::OnBlocked(const std::string*) noexcept { return true; }

void Strategy::Schedule(const pt::time_duration& delay,
                        boost::function<void()>&& callback) {
  GetContext().GetTimer().Schedule(
      delay, [this, callback]() { m_pimpl->SignalByScheduledEvent(callback); },
      m_pimpl->m_timerScope);
}

void Strategy::Schedule(boost::function<void()>&& callback) {
  GetContext().GetTimer().Schedule(
      [this, callback]() { m_pimpl->SignalByScheduledEvent(callback); },
      m_pimpl->m_timerScope);
}

Strategy::ProfitScannerSlotConnection Strategy::SubscribeToProfitScanner(
    const ProfitScannerSlot& slot) const {
  return m_pimpl->m_profitScannerSignal.connect(slot);
}

void Strategy::SetProfitOpportunity(const Double& ratio,
                                    const bool isAvailable) {
  const boost::unique_lock<boost::shared_mutex> lock(
      m_pimpl->m_profitRatioMutex);
  if (m_pimpl->m_profitOpportunity &&
      m_pimpl->m_profitOpportunity->first == ratio &&
      m_pimpl->m_profitOpportunity->second == isAvailable) {
    return;
  }
  m_pimpl->m_profitOpportunity.emplace(ratio, isAvailable);
  m_pimpl->m_profitScannerSignal(m_pimpl->m_profitOpportunity->first,
                                 m_pimpl->m_profitOpportunity->second);
}

boost::optional<std::pair<Double, bool>> Strategy::GetProfitOpportunity()
    const {
  const boost::shared_lock<boost::shared_mutex> lock(
      m_pimpl->m_profitRatioMutex);
  return m_pimpl->m_profitOpportunity;
}

////////////////////////////////////////////////////////////////////////////////
