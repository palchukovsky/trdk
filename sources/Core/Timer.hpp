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

#include "Api.h"

namespace trdk {

class TRDK_CORE_API Timer {
 public:
  class TRDK_CORE_API Scope {
    friend class trdk::Timer;

   public:
    typedef uintmax_t Id;

   public:
    explicit Scope();
    Scope(Scope &&) = default;
    ~Scope();

    void Swap(Scope &rhs) noexcept {
      std::swap(m_id, rhs.m_id);
      std::swap(m_timer, rhs.m_timer);
    }

   private:
    Scope(const Scope &);
    const Scope operator=(const Scope &);

   public:
    //! If scope is not empty it activated for one or more scheduling.
    bool IsEmpty() const { return m_timer ? false : true; }

    //! Cancels all tasks scheduled by this scope, but not executed tasks.
    size_t Cancel() noexcept;

   private:
    Id m_id;
    const Timer *m_timer;
  };

 public:
  explicit Timer(const trdk::Context &);
  ~Timer();

 private:
  Timer(const Timer &);
  const Timer &operator=(const Timer &);

 public:
  void Schedule(const boost::posix_time::time_duration &,
                const boost::function<void()> &&,
                Scope &) const;
  void Schedule(const boost::function<void()> &&, Scope &) const;

  void Stop();

 private:
  class Implementation;
  std::unique_ptr<Implementation> m_pimpl;
};
}
