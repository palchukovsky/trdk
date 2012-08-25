/**************************************************************************
 *   Created: 2012/07/09 00:29:59
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot
 **************************************************************************/

#pragma once

#include "Api.h"

class Position;

class TRADER_CORE_API PositionReporter : private boost::noncopyable {

public:

	PositionReporter();
	virtual ~PositionReporter();

public:

	virtual void ReportClosedPositon(const Position &) = 0;

};
