/*******************************************************************************
 *   Created: 2018/01/27 16:08:41
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 ******************************************************************************/

#include "Prec.hpp"
#include "OperationModelUtils.hpp"

using namespace trdk;
using namespace trdk::FrontEnd::Lib;
using namespace trdk::FrontEnd::Lib::Detail;

namespace pt = boost::posix_time;
namespace ids = boost::uuids;

namespace {
QString ShortenStrategyInstance(const std::string &source) {
  const auto &start = source.find('/');
  return QString::fromStdString(
      start == std::string::npos ? source : source.substr(start + 1));
}
}  // namespace

OperationRecord::OperationRecord(const ids::uuid &id,
                                 const pt::ptime &startTime,
                                 const Strategy &strategy)
    : id(QString::fromStdString(boost::lexical_cast<std::string>(id))),
      startTime(ConvertToQDateTime(startTime).time()),
      strategyName(QString::fromStdString(strategy.GetTitle())),
      strategyInstance(ShortenStrategyInstance(strategy.GetInstanceName())),
      status(QObject::tr("active")) {}

void OperationRecord::Update(const Pnl::Data &data) {
  QString buffer;
  for (const auto &item : data) {
    if (!item.second) {
      continue;
    }
    if (!buffer.isEmpty()) {
      buffer += ", ";
    }
    if (item.second > 0) {
      buffer += '+';
    }
    {
      std::ostringstream os;
      os << std ::fixed << std::setprecision(8) << item.second;
      auto vol = os.str();
      boost::trim_right_if(vol, [](char ch) { return ch == '0'; });
      if (!vol.empty() && (vol.back() == '.' || vol.back() == ',')) {
        vol.pop_back();
      }
      if (vol.empty()) {
        vol = "0.00000001";
      }
      buffer += QString::fromStdString(std::move(vol));
    }
    buffer += " " + QString::fromStdString(item.first);
  }
  financialResult = std::move(buffer);
}

void OperationRecord::Complete(const pt::ptime &newEndTime, const Pnl &pnl) {
  Assert(!result);
  Update(pnl.GetData());
  endTime = ConvertToQDateTime(newEndTime).time();
  result = pnl.GetResult();
  static_assert(Pnl::numberOfResults == 4, "List changed.");
  switch (*result) {
    case Pnl::RESULT_NONE:
      status = QObject::tr("canceled");
      break;
    case Pnl::RESULT_PROFIT:
      status = QObject::tr("profit");
      break;
    case Pnl::RESULT_LOSS:
      status = QObject::tr("loss");
      break;
    default:
      AssertEq(Pnl::RESULT_ERROR, *result);
    case Pnl::RESULT_ERROR:
      status = QObject::tr("error");
      break;
  }
}
