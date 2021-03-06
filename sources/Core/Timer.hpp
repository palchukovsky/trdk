/*******************************************************************************
 *   Created: 2017/10/24 15:09:07
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 ******************************************************************************/

#pragma once

namespace trdk {

class TRDK_CORE_API TimerScope {
  friend class Timer;

 public:
  typedef uintmax_t Id;

  explicit TimerScope();
  TimerScope(TimerScope &&) = default;
  TimerScope(const TimerScope &) = delete;
  TimerScope &operator=(TimerScope &&) = delete;
  TimerScope &operator=(const TimerScope &) = delete;
  ~TimerScope();

  void Swap(TimerScope &rhs) noexcept {
    std::swap(m_id, rhs.m_id);
    std::swap(m_timer, rhs.m_timer);
  }

  //! If scope is not empty it activated for one or more scheduling.
  bool IsEmpty() const { return m_timer ? false : true; }

  //! Cancels all tasks scheduled by this scope, but not executed tasks.
  size_t Cancel() noexcept;

 private:
  Id m_id;
  const Timer *m_timer;
};

class TRDK_CORE_API Timer {
  friend class TimerScope;

 public:
  typedef TimerScope Scope;

  explicit Timer(const Context &);
  Timer(Timer &&) noexcept;
  Timer(const Timer &) = delete;
  const Timer &operator=(Timer &&) = delete;
  const Timer &operator=(const Timer &) = delete;
  ~Timer();

  void Schedule(const boost::posix_time::time_duration &,
                boost::function<void()>,
                Scope &) const;
  void Schedule(boost::function<void()>, Scope &) const;

  void Stop();

 private:
  class Implementation;
  std::unique_ptr<Implementation> m_pimpl;
};

}  // namespace trdk
