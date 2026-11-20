#pragma once

#include "Measure.h"
#include "StepModel.h"

#include <AIS_InteractiveObject.hxx>
#include <NCollection_Vector.hxx>

#include <QElapsedTimer>
#include <QMainWindow>
#include <memory>

class OcctWidget;
class QActionGroup;
class QComboBox;
class QLabel;
class QProgressBar;
class QTreeWidget;

class MainWindow : public QMainWindow
{
  Q_OBJECT
public:
  explicit MainWindow(QWidget* theParent = nullptr);
  ~MainWindow() override;

  void OpenFile(const QString& thePath);

protected:
  void dragEnterEvent(QDragEnterEvent*) override;
  void dropEvent(QDropEvent*) override;
  void keyPressEvent(QKeyEvent*) override;
  void closeEvent(QCloseEvent*) override;

private slots:
  void onOpen();
  void onSelectionChanged();
  void onDisplayModeChanged();
  void onSelectionFilterChanged();
  void onUnitsChanged();
  void onKeepMeasurement();
  void onClearMeasurements();
  void onCopyResults();
  void onAbout();
  void onControls();
  void onRulerToggled(bool theOn);
  void onRulerClick(int theX, int theY);
  void onRulerHover(int theX, int theY);
  void onRulerHoverLeft();

private:
  enum class SelFilter
  {
    Auto,
    Face,
    Edge,
    Vertex,
    Solid
  };

  void createUi();
  void onModelLoaded(const std::shared_ptr<StepModel>& theModel, const QString& thePath, qint64 theMs);
  void displayModel();
  void clearModel();
  void applySelectionFilter();
  void updateModelInfo();
  void showResult(const MeasureResult& theRes, QTreeWidget* theTree);
  void clearTransient();
  void addGraphic(const MeasureGraphic& theGraphic);
  void displayAnnotation(const Handle(AIS_InteractiveObject)& theObj);
  bool pickPoint(int theX, int theY, gp_Pnt& thePnt, QString& theKind) const;
  void clearRulerPreview();
  void showRulerPreview(const gp_Pnt& thePnt, const QString& theKind);

private:
  OcctWidget*   myViewer       = nullptr;
  QTreeWidget*  myMeasureTree  = nullptr;
  QTreeWidget*  myModelTree    = nullptr;
  QLabel*       myMeasureTitle = nullptr;
  QLabel*       myStatusLabel  = nullptr;
  QProgressBar* myProgress     = nullptr;
  QComboBox*    myFilterCombo  = nullptr;
  QComboBox*    myUnitsCombo   = nullptr;
  QActionGroup* myDisplayGroup = nullptr;
  QAction*      myXRayAction   = nullptr;
  QAction*      myRulerAction  = nullptr;

  std::shared_ptr<StepModel>                    myModel;
  QString                                       myFilePath;
  NCollection_Vector<Handle(AIS_InteractiveObject)> myShapes;
  NCollection_Vector<Handle(AIS_InteractiveObject)> myTransient; //!< annotations of the current selection
  NCollection_Vector<Handle(AIS_InteractiveObject)> myPinned;    //!< annotations kept by the user
  MeasureResult                                 myLastResult;
  UnitFormat                                    myFormat;
  SelFilter                                     myFilter  = SelFilter::Auto;
  bool                                          myLoading = false;

  // ruler (point-to-point) tool
  NCollection_Vector<Handle(AIS_InteractiveObject)> myRulerPreview; //!< hover marker + rubber band
  bool                                          myRulerHasP1 = false;
  gp_Pnt                                        myRulerP1;
  QString                                       myRulerP1Kind;
};
