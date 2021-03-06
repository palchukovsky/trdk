/**************************************************************************
 *   Created: 2008/10/23 10:28
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: TunnelEx
 *       URL: http://tunnelex.net
 * Copyright: Eugene V. Palchukovsky
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 **************************************************************************/

#pragma once

#include <boost/config.hpp>
#include <string>

////////////////////////////////////////////////////////////////////////////////

#ifndef BOOST_WINDOWS

#include <errno.h>

inline int GetLastError() throw() { return errno; }

#endif

////////////////////////////////////////////////////////////////////////////////

namespace trdk {
namespace Lib {

class SysError {
 public:
  explicit SysError(int errorNo) throw();
  ~SysError() throw();

  friend std::ostream &operator<<(std::ostream &, const trdk::Lib::SysError &);
  friend std::wostream &operator<<(std::wostream &,
                                   const trdk::Lib::SysError &);

 public:
  std::wstring GetStringW() const;
  std::string GetString() const;
  int GetErrorNo() const;

  bool IsError() const;

  //! Returns true if error could be resolved to string.
  bool CheckError() const;

 private:
  int m_errorNo;
};
}
}

////////////////////////////////////////////////////////////////////////////////
