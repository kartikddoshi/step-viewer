#include "MainWindow.h"

#include <OSD.hxx>

#include <QApplication>
#include <QFileInfo>

int main(int argc, char* argv[])
{
  // turn access violations / FP errors inside the geometry kernel into catchable exceptions
  OSD::SetSignal(false);

  QApplication anApp(argc, argv);
  QApplication::setOrganizationName("StepViewer");
  QApplication::setApplicationName("StepViewer");
  QApplication::setApplicationVersion("1.0.0");
  QApplication::setStyle("fusion");

  MainWindow aWindow;
  aWindow.show();

  const QStringList anArgs = QApplication::arguments();
  for (int i = 1; i < anArgs.size(); ++i)
  {
    if (QFileInfo::exists(anArgs[i]))
    {
      aWindow.OpenFile(anArgs[i]);
      break;
    }
  }
  return anApp.exec();
}
