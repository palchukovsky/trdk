/**************************************************************************
 *   Created: 2012/09/23 20:27:10
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot
 **************************************************************************/

#pragma once

#include "Gateway/Interface/soapStub.h"
#include "Gateway/Interface/soapTraderServiceProxy.h"
#include "Gateway/Interface/TraderService.nsmap"

#pragma warning(push, 3)
#include <QMainWindow>
#include <QMessageBox>
#include <QTimer>
#include <QList>
#include <QStandardItemModel>
#include <QtCore/QVariant>
#include <QtGui/QApplication>
#include <QtGui/QAction>
#include <QtGui/QApplication>
#include <QtGui/QButtonGroup>
#include <QtGui/QComboBox>
#include <QtGui/QHBoxLayout>
#include <QtGui/QHeaderView>
#include <QtGui/QLabel>
#include <QtGui/QLineEdit>
#include <QtGui/QMainWindow>
#include <QtGui/QPushButton>
#include <QtGui/QSpacerItem>
#include <QtGui/QTableView>
#include <QtGui/QVBoxLayout>
#include <QtGui/QWidget>
#pragma warning(pop)

#include "Common/DisableBoostWarningsBegin.h"
#	include <boost/noncopyable.hpp>
#	include <boost/date_time.hpp>
#	include <boost/thread/thread_time.hpp>
#include "Common/DisableBoostWarningsEnd.h"

#include <stdint.h>
