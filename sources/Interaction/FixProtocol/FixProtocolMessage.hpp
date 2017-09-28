/*******************************************************************************
 *   Created: 2017/09/21 21:29:52
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 ******************************************************************************/

#pragma once

namespace trdk {
namespace Interaction {
namespace FixProtocol {

enum { SOH = 0x1 };

class Message : private boost::noncopyable {
 public:
  typedef std::vector<char>::const_iterator Iterator;

 protected:
  void WriteStandardTrailer(std::vector<char> &result, unsigned char soh) const;
};

namespace Detail {

enum MessageType {
  MESSAGE_TYPE_LOGON = 'A',
  MESSAGE_TYPE_LOGOUT = '5',
  MESSAGE_TYPE_HEARTBEAT = '0',
  MESSAGE_TYPE_TEST_REQUEST = '1',
  MESSAGE_TYPE_MARKET_DATA_REQUEST = 'V',
  MESSAGE_TYPE_MARKET_DATA_SNAPSHOT_FULL_REFRESH = 'W',
  MESSAGE_TYPE_MARKET_DATA_INCREMENTAL_REFRESH = 'X',
};

template <typename It>
uint32_t CalcCheckSum(It begin, const It &end) {
  Assert(begin < end);
  uint32_t result = 0;
  for (; begin != end; ++begin) {
    result += static_cast<decltype(result)>(*begin);
  }
  return result % 256;
}
}
}
}
}