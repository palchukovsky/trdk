/*******************************************************************************
 *   Created: 2017/09/05 08:19:04
 *    Author: Eugene V. Palchukovsky
 *    E-mail: eugene@palchukovsky.com
 * -------------------------------------------------------------------
 *   Project: Trading Robot Development Kit
 *       URL: http://robotdk.com
 * Copyright: Eugene V. Palchukovsky
 ******************************************************************************/

#include "Prec.hpp"
#include "Lib/Style.hpp"
#include "ShellMainWindow.hpp"

using namespace trdk::FrontEnd::Lib;
using namespace trdk::FrontEnd::Shell;

int main(int argc, char *argv[]) {
  _CrtSetDbgFlag(0);

  QApplication application(argc, argv);
  application.setApplicationName(TRDK_NAME);
  LoadStyle(application);

  MainWindow mainWindow(Q_NULLPTR);
  mainWindow.show();

  return application.exec();
}
