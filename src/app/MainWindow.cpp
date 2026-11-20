#include "MainWindow.h"

#include "OcctWidget.h"

#include <AIS_InteractiveContext.hxx>
#include <AIS_Point.hxx>
#include <AIS_Shape.hxx>
#include <AIS_TextLabel.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRep_Tool.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <StdSelect_ViewerSelector3d.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Edge.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <Geom_CartesianPoint.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_PointAspect.hxx>
#include <StdPrs_ToolTriangulatedShape.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFPrs_AISObject.hxx>
#include <XCAFPrs_Style.hxx>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QComboBox>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace
{
const Quantity_Color DIM_COLOR(0.86, 0.10, 0.10, Quantity_TOC_sRGB);
const Quantity_Color LABEL_BG(0.10, 0.12, 0.16, Quantity_TOC_sRGB);
const Quantity_Color SNAP_COLOR(0.05, 0.70, 0.20, Quantity_TOC_sRGB);

//! XCAF presentation with a curve colour that is visible on the light background
//! (OCCT's default for uncoloured curves / wire bodies is white).
class ModelPrs : public XCAFPrs_AISObject
{
public:
  explicit ModelPrs(const TDF_Label& theLabel)
      : XCAFPrs_AISObject(theLabel)
  {
  }
  void DefaultStyle(XCAFPrs_Style& theStyle) const override
  {
    XCAFPrs_AISObject::DefaultStyle(theStyle);
    theStyle.SetColorCurv(Quantity_Color(0.08, 0.27, 0.62, Quantity_TOC_sRGB));
  }
};

bool isSnapKind(const QString& theKind)
{
  return theKind == "corner" || theKind == "edge end" || theKind == "edge midpoint";
}

bool isStepFile(const QString& thePath)
{
  const QString aSuffix = QFileInfo(thePath).suffix().toLower();
  return aSuffix == "step" || aSuffix == "stp" || aSuffix == "p21";
}
} // namespace

MainWindow::MainWindow(QWidget* theParent)
    : QMainWindow(theParent)
{
  setWindowTitle("STEP Viewer");
  setAcceptDrops(true);
  resize(1280, 800);
  createUi();

  QSettings aSettings;
  restoreGeometry(aSettings.value("window/geometry").toByteArray());
  restoreState(aSettings.value("window/state").toByteArray());
  myUnitsCombo->setCurrentIndex(aSettings.value("units", 0).toInt());
}

MainWindow::~MainWindow()
{
  clearModel();
}

// ------------------------------------------------------------------ UI

void MainWindow::createUi()
{
  myViewer = new OcctWidget(this);
  setCentralWidget(myViewer);
  connect(myViewer, &OcctWidget::selectionChanged, this, &MainWindow::onSelectionChanged);
  connect(myViewer, &OcctWidget::pointClicked, this, &MainWindow::onRulerClick);
  connect(myViewer, &OcctWidget::pointHovered, this, &MainWindow::onRulerHover);
  connect(myViewer, &OcctWidget::pointHoverLeft, this, &MainWindow::onRulerHoverLeft);

  // ---- actions
  QAction* anOpen = new QAction("&Open...", this);
  anOpen->setShortcut(QKeySequence::Open);
  connect(anOpen, &QAction::triggered, this, &MainWindow::onOpen);

  QAction* aFit = new QAction("&Fit All", this);
  aFit->setShortcut(Qt::Key_F);
  aFit->setToolTip("Fit model in view (F)");
  connect(aFit, &QAction::triggered, myViewer, &OcctWidget::FitAll);

  struct ViewDef
  {
    const char*           Name;
    V3d_TypeOfOrientation Orient;
    QKeySequence          Key;
  };
  const ViewDef aViews[] = {
    {"&Isometric", V3d_XposYnegZpos, QKeySequence(Qt::Key_0)}, {"&Front", V3d_Yneg, QKeySequence(Qt::Key_1)},
    {"&Back", V3d_Ypos, QKeySequence(Qt::Key_2)},              {"&Top", V3d_Zpos, QKeySequence(Qt::Key_3)},
    {"Botto&m", V3d_Zneg, QKeySequence(Qt::Key_4)},            {"&Left", V3d_Xneg, QKeySequence(Qt::Key_5)},
    {"&Right", V3d_Xpos, QKeySequence(Qt::Key_6)},
  };
  QList<QAction*> aViewActions;
  for (const ViewDef& v : aViews)
  {
    QAction* a = new QAction(QString(v.Name), this);
    a->setShortcut(v.Key);
    const V3d_TypeOfOrientation anOrient = v.Orient;
    connect(a, &QAction::triggered, this, [this, anOrient] { myViewer->SetProjection(anOrient); });
    aViewActions << a;
  }

  myDisplayGroup        = new QActionGroup(this);
  QAction* aShadedEdges = new QAction("Shaded + Edges", myDisplayGroup);
  QAction* aShaded      = new QAction("Shaded", myDisplayGroup);
  QAction* aWire        = new QAction("Wireframe", myDisplayGroup);
  for (QAction* a : myDisplayGroup->actions())
    a->setCheckable(true);
  aShadedEdges->setChecked(true);
  connect(myDisplayGroup, &QActionGroup::triggered, this, &MainWindow::onDisplayModeChanged);

  myXRayAction = new QAction("X-Ray", this);
  myXRayAction->setCheckable(true);
  myXRayAction->setShortcut(Qt::Key_X);
  myXRayAction->setToolTip("Semi-transparent display to see inner geometry (X)");
  connect(myXRayAction, &QAction::toggled, this, &MainWindow::onDisplayModeChanged);

  myRulerAction = new QAction("Ruler", this);
  myRulerAction->setCheckable(true);
  myRulerAction->setShortcut(Qt::Key_R);
  myRulerAction->setToolTip("Ruler: click two points (on edges, corners or faces) to measure the distance between them (R)");
  connect(myRulerAction, &QAction::toggled, this, &MainWindow::onRulerToggled);

  QAction* aKeep = new QAction("Keep Measurement", this);
  aKeep->setShortcut(Qt::Key_K);
  aKeep->setToolTip("Keep the current dimension in the view (K)");
  connect(aKeep, &QAction::triggered, this, &MainWindow::onKeepMeasurement);

  QAction* aClear = new QAction("Clear Measurements", this);
  aClear->setShortcut(Qt::Key_Escape);
  aClear->setToolTip("Clear selection and dimensions (Esc)");
  connect(aClear, &QAction::triggered, this, &MainWindow::onClearMeasurements);

  // ---- menus
  QMenu* aFileMenu = menuBar()->addMenu("&File");
  aFileMenu->addAction(anOpen);
  aFileMenu->addSeparator();
  QAction* anExit = aFileMenu->addAction("E&xit");
  connect(anExit, &QAction::triggered, this, &QWidget::close);

  QMenu* aViewMenu = menuBar()->addMenu("&View");
  aViewMenu->addAction(aFit);
  aViewMenu->addSeparator();
  aViewMenu->addActions(aViewActions);
  aViewMenu->addSeparator();
  aViewMenu->addActions(myDisplayGroup->actions());
  aViewMenu->addAction(myXRayAction);

  QMenu* aMeasMenu = menuBar()->addMenu("&Measure");
  aMeasMenu->addAction(myRulerAction);
  aMeasMenu->addSeparator();
  aMeasMenu->addAction(aKeep);
  aMeasMenu->addAction(aClear);

  QMenu* aHelpMenu = menuBar()->addMenu("&Help");
  connect(aHelpMenu->addAction("&Mouse && Keyboard"), &QAction::triggered, this, &MainWindow::onControls);
  connect(aHelpMenu->addAction("&About"), &QAction::triggered, this, &MainWindow::onAbout);

  // ---- toolbar
  QToolBar* aTb = addToolBar("Main");
  aTb->setObjectName("MainToolbar");
  aTb->setMovable(false);
  aTb->setToolButtonStyle(Qt::ToolButtonTextOnly);
  aTb->addAction(anOpen);
  aTb->addSeparator();
  aTb->addAction(aFit);
  for (QAction* a : aViewActions)
    aTb->addAction(a);
  aTb->addSeparator();
  aTb->addActions(myDisplayGroup->actions());
  aTb->addAction(myXRayAction);
  aTb->addSeparator();

  aTb->addAction(myRulerAction);
  aTb->addSeparator();
  aTb->addWidget(new QLabel(" Pick: ", aTb));
  myFilterCombo = new QComboBox(aTb);
  myFilterCombo->addItems({"Auto (face/edge/point)", "Faces", "Edges", "Vertices", "Solids"});
  myFilterCombo->setToolTip("What a mouse click selects");
  connect(myFilterCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onSelectionFilterChanged);
  aTb->addWidget(myFilterCombo);

  aTb->addWidget(new QLabel("  Units: ", aTb));
  myUnitsCombo = new QComboBox(aTb);
  myUnitsCombo->addItems({"mm", "inch"});
  connect(myUnitsCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onUnitsChanged);
  aTb->addWidget(myUnitsCombo);
  aTb->addSeparator();
  aTb->addAction(aKeep);
  aTb->addAction(aClear);

  // ---- measure dock
  QWidget*     aPanel  = new QWidget(this);
  QVBoxLayout* aLayout = new QVBoxLayout(aPanel);
  aLayout->setContentsMargins(6, 6, 6, 6);

  myMeasureTitle = new QLabel(aPanel);
  QFont aTitleFont = myMeasureTitle->font();
  aTitleFont.setBold(true);
  aTitleFont.setPointSizeF(aTitleFont.pointSizeF() * 1.15);
  myMeasureTitle->setFont(aTitleFont);
  myMeasureTitle->setWordWrap(true);
  aLayout->addWidget(myMeasureTitle);

  auto makeTree = [aPanel]() {
    QTreeWidget* t = new QTreeWidget(aPanel);
    t->setColumnCount(2);
    t->setHeaderLabels({"Property", "Value"});
    t->setRootIsDecorated(false);
    t->setSelectionMode(QAbstractItemView::ExtendedSelection);
    t->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    t->header()->setStretchLastSection(true);
    return t;
  };
  myMeasureTree = makeTree();
  aLayout->addWidget(myMeasureTree, 3);

  QPushButton* aCopyBtn = new QPushButton("Copy to clipboard", aPanel);
  connect(aCopyBtn, &QPushButton::clicked, this, &MainWindow::onCopyResults);
  aLayout->addWidget(aCopyBtn);

  QLabel* aModelLbl = new QLabel("Model", aPanel);
  aModelLbl->setFont(aTitleFont);
  aLayout->addWidget(aModelLbl);
  myModelTree = makeTree();
  aLayout->addWidget(myModelTree, 2);

  QDockWidget* aDock = new QDockWidget("Measure", this);
  aDock->setObjectName("MeasureDock");
  aDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
  aDock->setWidget(aPanel);
  aDock->setMinimumWidth(300);
  addDockWidget(Qt::RightDockWidgetArea, aDock);

  // ---- status bar
  myStatusLabel = new QLabel("Open a STEP file (Ctrl+O) or drop one onto the window.", this);
  statusBar()->addWidget(myStatusLabel, 1);
  myProgress = new QProgressBar(this);
  myProgress->setRange(0, 0);
  myProgress->setMaximumWidth(160);
  myProgress->setVisible(false);
  statusBar()->addPermanentWidget(myProgress);

  showResult(MeasureResult(), myMeasureTree);
}

// ------------------------------------------------------------------ loading

void MainWindow::onOpen()
{
  QSettings     aSettings;
  const QString aPath = QFileDialog::getOpenFileName(this, "Open STEP file", aSettings.value("lastDir").toString(),
                                                     "STEP files (*.step *.stp *.p21);;All files (*.*)");
  if (!aPath.isEmpty())
  {
    aSettings.setValue("lastDir", QFileInfo(aPath).absolutePath());
    OpenFile(aPath);
  }
}

void MainWindow::OpenFile(const QString& thePath)
{
  if (myLoading)
    return;
  if (!QFileInfo::exists(thePath))
  {
    QMessageBox::warning(this, "STEP Viewer", "File not found:\n" + thePath);
    return;
  }
  myLoading = true;
  myProgress->setVisible(true);
  myStatusLabel->setText("Loading " + QFileInfo(thePath).fileName() + " ...");
  QApplication::setOverrideCursor(Qt::BusyCursor);

  auto          aModel = std::make_shared<StepModel>();
  const QString aPath  = thePath;
  auto          aTimer = std::make_shared<QElapsedTimer>();
  aTimer->start();

  QThread* aThread = QThread::create([aModel, aPath]() {
    if (!stepcore::LoadStepFile(aPath.toUtf8().toStdString(), *aModel))
      return;
    // pre-compute the tessellation in the background, with the same settings as the viewer uses
    try
    {
      Handle(Prs3d_Drawer) aDrawer = new Prs3d_Drawer();
      aDrawer->SetDeviationCoefficient(0.0008);
      aDrawer->SetDeviationAngle(12.0 * M_PI / 180.0);
      for (const TDF_Label& aLabel : aModel->Roots)
      {
        const TopoDS_Shape aShape = XCAFDoc_ShapeTool::GetShape(aLabel);
        if (aShape.IsNull())
          continue;
        const double aDefl = StdPrs_ToolTriangulatedShape::GetDeflection(aShape, aDrawer);
        BRepMesh_IncrementalMesh(aShape, aDefl, false, aDrawer->DeviationAngle());
      }
    }
    catch (...)
    {
    }
  });
  connect(aThread, &QThread::finished, this, [this, aThread, aModel, aPath, aTimer]() {
    aThread->deleteLater();
    onModelLoaded(aModel, aPath, aTimer->elapsed());
  });
  aThread->start();
}

void MainWindow::onModelLoaded(const std::shared_ptr<StepModel>& theModel, const QString& thePath, qint64 theMs)
{
  myLoading = false;
  myProgress->setVisible(false);
  QApplication::restoreOverrideCursor();

  if (theModel->IsNull())
  {
    myStatusLabel->setText("Failed to open " + QFileInfo(thePath).fileName());
    QMessageBox::warning(this, "STEP Viewer",
                         "Could not open\n" + thePath + "\n\n" + QString::fromStdString(theModel->Error));
    stepcore::ReleaseModel(*theModel);
    return;
  }

  clearModel();
  myModel    = theModel;
  myFilePath = thePath;
  setWindowTitle(QFileInfo(thePath).fileName() + " - STEP Viewer");
  displayModel();
  myStatusLabel->setText(QString("%1   (loaded in %2 s)")
                           .arg(QDir::toNativeSeparators(thePath))
                           .arg(theMs / 1000.0, 0, 'f', 2));
  QTimer::singleShot(0, this, &MainWindow::updateModelInfo);
}

void MainWindow::clearModel()
{
  if (myViewer != nullptr && !myViewer->Context().IsNull())
  {
    const Handle(AIS_InteractiveContext)& aCtx = myViewer->Context();
    aCtx->ClearSelected(false);
    for (const Handle(AIS_InteractiveObject)& anObj : myShapes)
      aCtx->Remove(anObj, false);
    for (const Handle(AIS_InteractiveObject)& anObj : myTransient)
      aCtx->Remove(anObj, false);
    for (const Handle(AIS_InteractiveObject)& anObj : myPinned)
      aCtx->Remove(anObj, false);
  }
  myShapes.Clear();
  myTransient.Clear();
  myPinned.Clear();
  clearRulerPreview();
  myRulerHasP1 = false;
  if (myModel)
  {
    stepcore::ReleaseModel(*myModel);
    myModel.reset();
  }
  if (myModelTree != nullptr)
    myModelTree->clear();
}

void MainWindow::displayModel()
{
  const Handle(AIS_InteractiveContext)& aCtx = myViewer->Context();
  if (aCtx.IsNull() || !myModel)
    return;

  for (const TDF_Label& aLabel : myModel->Roots)
  {
    Handle(XCAFPrs_AISObject) anObj = new ModelPrs(aLabel);
    anObj->Attributes()->SetWireAspect(
      new Prs3d_LineAspect(Quantity_Color(0.08, 0.27, 0.62, Quantity_TOC_sRGB), Aspect_TOL_SOLID, 2.0 * devicePixelRatioF()));
    myShapes.Append(anObj);
    // activate a selection mode at display time: this registers the object with the selector
    aCtx->Display(anObj, AIS_Shaded, AIS_Shape::SelectionMode(TopAbs_FACE), false);
  }
  onDisplayModeChanged();
  applySelectionFilter();
  myViewer->SetProjection(V3d_XposYnegZpos);
  showResult(MeasureResult(), myMeasureTree);
}

// ------------------------------------------------------------------ display options

void MainWindow::onDisplayModeChanged()
{
  const Handle(AIS_InteractiveContext)& aCtx = myViewer->Context();
  if (aCtx.IsNull())
    return;
  const QString aMode   = myDisplayGroup->checkedAction() ? myDisplayGroup->checkedAction()->text() : QString();
  const bool    isWire  = aMode == "Wireframe";
  const bool    isEdges = aMode == "Shaded + Edges";
  const bool    isXRay  = myXRayAction->isChecked();
  for (const Handle(AIS_InteractiveObject)& anObj : myShapes)
  {
    anObj->Attributes()->SetFaceBoundaryDraw(isEdges || isXRay);
    anObj->Attributes()->SetFaceBoundaryAspect(
      new Prs3d_LineAspect(Quantity_Color(0.12, 0.12, 0.14, Quantity_TOC_sRGB), Aspect_TOL_SOLID, 1.0));
    aCtx->SetDisplayMode(anObj, isWire ? AIS_WireFrame : AIS_Shaded, false);
    if (isXRay)
      aCtx->SetTransparency(anObj, 0.65, false);
    else
      aCtx->UnsetTransparency(anObj, false);
    aCtx->Redisplay(anObj, false);
  }
  myViewer->UpdateView();
}

void MainWindow::onSelectionFilterChanged()
{
  myFilter = static_cast<SelFilter>(myFilterCombo->currentIndex());
  applySelectionFilter();
}

void MainWindow::applySelectionFilter()
{
  const Handle(AIS_InteractiveContext)& aCtx = myViewer->Context();
  if (aCtx.IsNull())
    return;
  aCtx->ClearSelected(false);
  const bool isRuler = myRulerAction != nullptr && myRulerAction->isChecked();
  // the ruler picks corners, edges and faces; edges get a few pixels of tolerance so they are easy to hit
  aCtx->SetPixelTolerance(isRuler ? int(5 * devicePixelRatioF() + 0.5) : 2);
  for (const Handle(AIS_InteractiveObject)& anObj : myShapes)
  {
    aCtx->Deactivate(anObj);
    switch (isRuler ? SelFilter::Auto : myFilter)
    {
      case SelFilter::Auto:
        aCtx->Activate(anObj, AIS_Shape::SelectionMode(TopAbs_VERTEX));
        aCtx->Activate(anObj, AIS_Shape::SelectionMode(TopAbs_EDGE));
        aCtx->Activate(anObj, AIS_Shape::SelectionMode(TopAbs_FACE));
        break;
      case SelFilter::Face: aCtx->Activate(anObj, AIS_Shape::SelectionMode(TopAbs_FACE)); break;
      case SelFilter::Edge: aCtx->Activate(anObj, AIS_Shape::SelectionMode(TopAbs_EDGE)); break;
      case SelFilter::Vertex: aCtx->Activate(anObj, AIS_Shape::SelectionMode(TopAbs_VERTEX)); break;
      case SelFilter::Solid: aCtx->Activate(anObj, AIS_Shape::SelectionMode(TopAbs_SOLID)); break;
    }
  }
  onSelectionChanged();
}

void MainWindow::onUnitsChanged()
{
  myFormat.Unit = myUnitsCombo->currentIndex() == 1 ? LengthUnit::Inch : LengthUnit::Millimeter;
  QSettings().setValue("units", myUnitsCombo->currentIndex());
  onSelectionChanged();
  updateModelInfo();
}

// ------------------------------------------------------------------ measuring

void MainWindow::onSelectionChanged()
{
  if (myRulerAction != nullptr && myRulerAction->isChecked())
    return; // the ruler owns the measure panel
  clearTransient();
  const Handle(AIS_InteractiveContext)& aCtx = myViewer->Context();
  if (aCtx.IsNull())
    return;

  std::vector<TopoDS_Shape> aShapes;
  for (aCtx->InitSelected(); aCtx->MoreSelected(); aCtx->NextSelected())
  {
    if (aCtx->HasSelectedShape())
      aShapes.push_back(aCtx->SelectedShape());
  }

  MeasureResult aRes;
  if (aShapes.size() == 1)
  {
    aRes = Measure::Single(aShapes[0], myFormat);
  }
  else if (aShapes.size() == 2)
  {
    aRes = Measure::Pair(aShapes[0], aShapes[1], myFormat);
  }
  else if (aShapes.size() > 2)
  {
    aRes.Title = QString("%1 entities selected").arg(aShapes.size());
    double aLen = 0.0, anArea = 0.0;
    int    aNbEdges = 0, aNbFaces = 0;
    for (const TopoDS_Shape& aS : aShapes)
    {
      const MeasureResult r = Measure::Single(aS, UnitFormat{LengthUnit::Millimeter, 9});
      for (const auto& aRow : r.Rows)
      {
        if (aS.ShapeType() == TopAbs_EDGE && (aRow.first == "Length" || aRow.first == "Circumference" || aRow.first == "Arc length"))
        {
          aLen += aRow.second.split(' ').first().toDouble();
          ++aNbEdges;
        }
        if (aS.ShapeType() == TopAbs_FACE && aRow.first == "Area")
        {
          anArea += aRow.second.split(' ').first().toDouble();
          ++aNbFaces;
        }
      }
    }
    if (aNbEdges > 0)
      aRes.Add(QString("Total length (%1 edges)").arg(aNbEdges), myFormat.Length(aLen));
    if (aNbFaces > 0)
      aRes.Add(QString("Total area (%1 faces)").arg(aNbFaces), myFormat.Area(anArea));
    aRes.Add("Tip", "Select 1 or 2 entities to measure them.");
  }

  for (const MeasureGraphic& g : aRes.Graphics)
    addGraphic(g);
  myLastResult = aRes;
  showResult(aRes, myMeasureTree);
  myViewer->UpdateView();
}

void MainWindow::showResult(const MeasureResult& theRes, QTreeWidget* theTree)
{
  theTree->clear();
  if (theTree == myMeasureTree)
  {
    if (theRes.Title.isEmpty())
    {
      myMeasureTitle->setText("Nothing selected");
      auto* anItem = new QTreeWidgetItem(theTree, {"R", "ruler: click two points → distance"});
      new QTreeWidgetItem(theTree, {"Click", "a face, edge or vertex"});
      new QTreeWidgetItem(theTree, {"Shift+Click", "a second entity → distance / angle"});
      new QTreeWidgetItem(theTree, {"K", "keep dimension in view"});
      new QTreeWidgetItem(theTree, {"Esc", "clear"});
      for (int i = 0; i < theTree->topLevelItemCount(); ++i)
        theTree->topLevelItem(i)->setForeground(0, QBrush(Qt::gray));
      Q_UNUSED(anItem);
      return;
    }
    myMeasureTitle->setText(theRes.Title);
  }
  for (const auto& aRow : theRes.Rows)
  {
    auto* anItem = new QTreeWidgetItem(theTree, {aRow.first, aRow.second});
    anItem->setToolTip(1, aRow.second);
  }
}

void MainWindow::updateModelInfo()
{
  myModelTree->clear();
  if (!myModel || myModel->IsNull())
    return;
  MeasureResult aRes = Measure::Model(myModel->Shape, myFormat);
  aRes.Rows.insert(aRes.Rows.begin(), {"File", QFileInfo(myFilePath).fileName()});
  aRes.Rows.insert(aRes.Rows.begin() + 1, {"Top-level shapes", QString::number(myModel->Roots.Length())});
  showResult(aRes, myModelTree);
}

void MainWindow::displayAnnotation(const Handle(AIS_InteractiveObject)& theObj)
{
  const Handle(AIS_InteractiveContext)& aCtx = myViewer->Context();
  aCtx->Display(theObj, 0, -1, false);
  aCtx->SetZLayer(theObj, Graphic3d_ZLayerId_Topmost);
  myTransient.Append(theObj);
}

void MainWindow::addGraphic(const MeasureGraphic& theGraphic)
{
  const Handle(AIS_InteractiveContext)& aCtx = myViewer->Context();
  if (aCtx.IsNull())
    return;
  const double aDpr = devicePixelRatioF();

  auto aMarker = [&](const gp_Pnt& p) {
    Handle(AIS_Point) aPnt = new AIS_Point(new Geom_CartesianPoint(p));
    aPnt->SetMarker(Aspect_TOM_BALL);
    aPnt->SetColor(DIM_COLOR);
    aPnt->Attributes()->SetPointAspect(new Prs3d_PointAspect(Aspect_TOM_BALL, DIM_COLOR, 3.0 * aDpr));
    displayAnnotation(aPnt);
  };
  auto aLabel = [&](const gp_Pnt& p, const QString& theText) {
    Handle(AIS_TextLabel) aTxt = new AIS_TextLabel();
    aTxt->SetText(TCollection_ExtendedString(theText.toUtf8().constData(), true));
    aTxt->SetPosition(p);
    aTxt->SetHeight(15.0 * aDpr);
    aTxt->SetColor(Quantity_NOC_WHITE);
    aTxt->SetDisplayType(Aspect_TODT_SUBTITLE);
    aTxt->SetColorSubTitle(LABEL_BG);
    aTxt->SetFontAspect(Font_FA_Bold);
    aTxt->SetHJustification(Graphic3d_HTA_CENTER);
    aTxt->SetVJustification(Graphic3d_VTA_CENTER);
    displayAnnotation(aTxt);
  };

  switch (theGraphic.Type)
  {
    case MeasureGraphic::Line:
    {
      if (theGraphic.P1.Distance(theGraphic.P2) > 1.0e-7)
      {
        Handle(AIS_Shape) aLine = new AIS_Shape(BRepBuilderAPI_MakeEdge(theGraphic.P1, theGraphic.P2).Edge());
        aLine->SetColor(DIM_COLOR);
        aLine->SetWidth(2.5 * aDpr);
        displayAnnotation(aLine);
      }
      aMarker(theGraphic.P1);
      aMarker(theGraphic.P2);
      aLabel(gp_Pnt((theGraphic.P1.XYZ() + theGraphic.P2.XYZ()) * 0.5), theGraphic.Text);
      break;
    }
    case MeasureGraphic::Label: aLabel(theGraphic.P1, theGraphic.Text); break;
    case MeasureGraphic::Marker: aMarker(theGraphic.P1); break;
  }
}

void MainWindow::clearTransient()
{
  const Handle(AIS_InteractiveContext)& aCtx = myViewer->Context();
  if (!aCtx.IsNull())
  {
    for (const Handle(AIS_InteractiveObject)& anObj : myTransient)
      aCtx->Remove(anObj, false);
  }
  myTransient.Clear();
}

void MainWindow::onKeepMeasurement()
{
  for (const Handle(AIS_InteractiveObject)& anObj : myTransient)
    myPinned.Append(anObj);
  myTransient.Clear();
  if (myRulerAction->isChecked())
  {
    myStatusLabel->setText(QString("%1 dimension object(s) kept. Esc clears all.").arg(myPinned.Length()));
    return;
  }
  myViewer->Context()->ClearSelected(false);
  onSelectionChanged();
  myStatusLabel->setText(QString("%1 dimension object(s) kept. Esc clears all.").arg(myPinned.Length()));
}

void MainWindow::onClearMeasurements()
{
  const Handle(AIS_InteractiveContext)& aCtx = myViewer->Context();
  if (aCtx.IsNull())
    return;
  for (const Handle(AIS_InteractiveObject)& anObj : myPinned)
    aCtx->Remove(anObj, false);
  myPinned.Clear();
  aCtx->ClearSelected(false);
  myRulerHasP1 = false;
  clearRulerPreview();
  if (myRulerAction->isChecked())
  {
    clearTransient();
    onRulerToggled(true); // restart: show the ruler help again
    return;
  }
  onSelectionChanged();
}

void MainWindow::onCopyResults()
{
  QString aText = myMeasureTitle->text() + "\n";
  for (const auto& aRow : myLastResult.Rows)
    aText += aRow.first + "\t" + aRow.second + "\n";
  QApplication::clipboard()->setText(aText);
  myStatusLabel->setText("Measurement copied to clipboard.");
}

// ------------------------------------------------------------------ ruler (point to point)

void MainWindow::onRulerToggled(bool theOn)
{
  myViewer->SetPointPickMode(theOn);
  myRulerHasP1 = false;
  clearRulerPreview();
  clearTransient();
  applySelectionFilter(); // clears the selection and activates corner / edge / face picking
  if (theOn)
  {
    myFilterCombo->setEnabled(false);
    myViewer->setCursor(Qt::CrossCursor);
    MeasureResult aHelp;
    aHelp.Title = "Ruler: click the first point";
    aHelp.Add("Click", "first point, then second point");
    aHelp.Add("Snaps to", "corners, edge ends, edge midpoints (green ring)");
    aHelp.Add("Otherwise", "exact point on the edge / face under the cursor");
    aHelp.Add("Drag", "rotate the view as usual");
    aHelp.Add("K", "keep the dimension in the view");
    aHelp.Add("Esc / R", "restart / leave the ruler");
    myLastResult = aHelp;
    showResult(aHelp, myMeasureTree);
    myStatusLabel->setText("Ruler: click the first point (snaps to corners, edge ends and edge midpoints).");
  }
  else
  {
    myFilterCombo->setEnabled(true);
    myViewer->unsetCursor();
    onSelectionChanged();
    myStatusLabel->setText("Ruler off.");
  }
  // full redraw: removes the highlight of the selection that was just cleared
  if (!myViewer->View().IsNull())
    myViewer->View()->Invalidate();
  myViewer->UpdateView();
}

bool MainWindow::pickPoint(int theX, int theY, gp_Pnt& thePnt, QString& theKind) const
{
  const Handle(AIS_InteractiveContext)& aCtx  = myViewer->Context();
  const Handle(V3d_View)&               aView = myViewer->View();
  if (aCtx.IsNull() || aView.IsNull())
    return false;

  const Handle(StdSelect_ViewerSelector3d)& aSel = aCtx->MainSelector();
  aSel->Pick(theX, theY, aView);
  for (int aRank = 1; aRank <= aSel->NbPicked(); ++aRank)
  {
    Handle(StdSelect_BRepOwner) anOwner = Handle(StdSelect_BRepOwner)::DownCast(aSel->Picked(aRank));
    if (anOwner.IsNull() || !anOwner->HasShape())
      continue;
    bool isModel = false;
    for (const Handle(AIS_InteractiveObject)& anObj : myShapes)
      isModel = isModel || anObj.get() == anOwner->Selectable().get();
    if (!isModel)
      continue; // view cube, annotations

    const TopoDS_Shape aShape = anOwner->Shape().Located(anOwner->Location() * anOwner->Shape().Location());
    const gp_Pnt       aRaw   = aSel->PickedPoint(aRank);
    const double       aTol   = 12.0 * devicePixelRatioF(); // snap radius, pixels
    auto screenDist = [&](const gp_Pnt& p) {
      int aXp = 0, aYp = 0;
      aView->Convert(p.X(), p.Y(), p.Z(), aXp, aYp);
      return std::hypot(double(aXp - theX), double(aYp - theY));
    };
    auto project = [&](const TopoDS_Shape& theTarget, gp_Pnt& theRes) {
      BRepExtrema_DistShapeShape aDist(BRepBuilderAPI_MakeVertex(aRaw).Vertex(), theTarget);
      if (!aDist.IsDone() || aDist.NbSolution() < 1)
        return false;
      theRes = aDist.PointOnShape2(1);
      return true;
    };

    try
    {
      switch (aShape.ShapeType())
      {
        case TopAbs_VERTEX:
          thePnt  = BRep_Tool::Pnt(TopoDS::Vertex(aShape));
          theKind = "corner";
          return true;
        case TopAbs_EDGE:
        {
          const TopoDS_Edge& anEdge = TopoDS::Edge(aShape);
          // snap candidates: end points, then midpoint
          std::vector<std::pair<gp_Pnt, QString>> aCands;
          TopoDS_Vertex aV1, aV2;
          TopExp::Vertices(anEdge, aV1, aV2);
          if (!aV1.IsNull())
            aCands.push_back({BRep_Tool::Pnt(aV1), "edge end"});
          if (!aV2.IsNull())
            aCands.push_back({BRep_Tool::Pnt(aV2), "edge end"});
          if (!BRep_Tool::Degenerated(anEdge) && (aV1.IsNull() || aV2.IsNull() || !aV1.IsSame(aV2)))
          {
            BRepAdaptor_Curve aCurve(anEdge);
            aCands.push_back({aCurve.Value(0.5 * (aCurve.FirstParameter() + aCurve.LastParameter())), "edge midpoint"});
          }
          double aBest    = aTol;
          int    aBestIdx = -1;
          for (size_t i = 0; i < aCands.size(); ++i)
          {
            const double d = screenDist(aCands[i].first);
            if (d < aBest)
            {
              aBest    = d;
              aBestIdx = int(i);
            }
          }
          if (aBestIdx >= 0)
          {
            thePnt  = aCands[aBestIdx].first;
            theKind = aCands[aBestIdx].second;
            return true;
          }
          if (!project(anEdge, thePnt))
            thePnt = aRaw;
          theKind = "on edge";
          return true;
        }
        case TopAbs_FACE:
          if (!project(aShape, thePnt))
            thePnt = aRaw;
          theKind = "on face";
          return true;
        default:
          thePnt  = aRaw;
          theKind = "on model";
          return true;
      }
    }
    catch (...)
    {
      thePnt  = aRaw;
      theKind = "on model";
      return true;
    }
  }
  return false;
}

void MainWindow::clearRulerPreview()
{
  if (myViewer != nullptr && !myViewer->Context().IsNull())
  {
    for (const Handle(AIS_InteractiveObject)& anObj : myRulerPreview)
      myViewer->Context()->Remove(anObj, false);
  }
  myRulerPreview.Clear();
}

void MainWindow::showRulerPreview(const gp_Pnt& thePnt, const QString& theKind)
{
  const Handle(AIS_InteractiveContext)& aCtx = myViewer->Context();
  clearRulerPreview();
  const double aDpr     = devicePixelRatioF();
  const bool   isSnap   = isSnapKind(theKind);
  auto         aDisplay = [&](const Handle(AIS_InteractiveObject)& theObj) {
    aCtx->Display(theObj, 0, -1, false);
    aCtx->SetZLayer(theObj, Graphic3d_ZLayerId_Topmost);
    myRulerPreview.Append(theObj);
  };

  const Quantity_Color aCol = isSnap ? SNAP_COLOR : DIM_COLOR;
  Handle(AIS_Point)    aPnt = new AIS_Point(new Geom_CartesianPoint(thePnt));
  aPnt->SetColor(aCol);
  aPnt->Attributes()->SetPointAspect(
    new Prs3d_PointAspect(isSnap ? Aspect_TOM_RING1 : Aspect_TOM_BALL, aCol, (isSnap ? 3.0 : 1.6) * aDpr));
  aDisplay(aPnt);

  if (myRulerHasP1 && myRulerP1.Distance(thePnt) > 1.0e-7)
  {
    Handle(AIS_Shape) aLine = new AIS_Shape(BRepBuilderAPI_MakeEdge(myRulerP1, thePnt).Edge());
    aLine->SetColor(DIM_COLOR);
    aLine->Attributes()->SetWireAspect(new Prs3d_LineAspect(DIM_COLOR, Aspect_TOL_DASH, 1.5 * aDpr));
    aDisplay(aLine);
  }
}

void MainWindow::onRulerHover(int theX, int theY)
{
  if (!myRulerAction->isChecked() || !myModel)
    return;
  gp_Pnt  aPnt;
  QString aKind;
  if (pickPoint(theX, theY, aPnt, aKind))
  {
    showRulerPreview(aPnt, aKind);
    if (myRulerHasP1)
    {
      const gp_Vec d(myRulerP1, aPnt);
      myStatusLabel->setText(QString("Ruler: %1   (dX %2  dY %3  dZ %4)   [%5]  click to set the second point")
                               .arg(myFormat.Length(d.Magnitude()), myFormat.Length(std::abs(d.X())),
                                    myFormat.Length(std::abs(d.Y())), myFormat.Length(std::abs(d.Z())), aKind));
    }
    else
    {
      myStatusLabel->setText("Ruler: [" + aKind + "]  " + myFormat.Point(aPnt) + "   click to set the first point");
    }
  }
  else
  {
    clearRulerPreview();
  }
  myViewer->UpdateView();
}

void MainWindow::onRulerHoverLeft()
{
  clearRulerPreview();
  myViewer->UpdateView();
}

void MainWindow::onRulerClick(int theX, int theY)
{
  if (!myRulerAction->isChecked() || !myModel)
    return;
  gp_Pnt  aPnt;
  QString aKind;
  if (!pickPoint(theX, theY, aPnt, aKind))
  {
    myStatusLabel->setText("Ruler: nothing under the cursor - click on an edge, corner or face.");
    return;
  }
  clearRulerPreview();
  clearTransient(); // previous (not kept) ruler dimension or first-point marker

  if (!myRulerHasP1)
  {
    myRulerHasP1  = true;
    myRulerP1     = aPnt;
    myRulerP1Kind = aKind;
    MeasureGraphic aMark;
    aMark.Type = MeasureGraphic::Marker;
    aMark.P1   = aPnt;
    addGraphic(aMark);

    MeasureResult aRes;
    aRes.Title = "Ruler: click the second point";
    aRes.Add("Point 1 (" + aKind + ")", myFormat.Point(aPnt));
    myLastResult = aRes;
    showResult(aRes, myMeasureTree);
    myStatusLabel->setText("Ruler: click the second point.");
  }
  else
  {
    myRulerHasP1 = false;
    const gp_Vec d(myRulerP1, aPnt);

    MeasureResult aRes;
    aRes.Title = "Ruler: point to point";
    aRes.Add("Distance", myFormat.Length(d.Magnitude()));
    aRes.Add("ΔX", myFormat.Length(std::abs(d.X())));
    aRes.Add("ΔY", myFormat.Length(std::abs(d.Y())));
    aRes.Add("ΔZ", myFormat.Length(std::abs(d.Z())));
    aRes.Add("Point 1 (" + myRulerP1Kind + ")", myFormat.Point(myRulerP1));
    aRes.Add("Point 2 (" + aKind + ")", myFormat.Point(aPnt));

    MeasureGraphic aDim;
    aDim.Type = MeasureGraphic::Line;
    aDim.P1   = myRulerP1;
    aDim.P2   = aPnt;
    aDim.Text = myFormat.Length(d.Magnitude());
    addGraphic(aDim);

    myLastResult = aRes;
    showResult(aRes, myMeasureTree);
    myStatusLabel->setText("Ruler: " + aDim.Text + "   K keeps it in the view; click again to start a new measurement.");
  }
  myViewer->UpdateView();
}

// ------------------------------------------------------------------ misc events

void MainWindow::dragEnterEvent(QDragEnterEvent* theEvent)
{
  if (theEvent->mimeData()->hasUrls())
  {
    for (const QUrl& anUrl : theEvent->mimeData()->urls())
    {
      if (anUrl.isLocalFile() && isStepFile(anUrl.toLocalFile()))
      {
        theEvent->acceptProposedAction();
        return;
      }
    }
  }
}

void MainWindow::dropEvent(QDropEvent* theEvent)
{
  for (const QUrl& anUrl : theEvent->mimeData()->urls())
  {
    if (anUrl.isLocalFile() && isStepFile(anUrl.toLocalFile()))
    {
      OpenFile(anUrl.toLocalFile());
      return;
    }
  }
}

void MainWindow::keyPressEvent(QKeyEvent* theEvent)
{
  QMainWindow::keyPressEvent(theEvent);
}

void MainWindow::closeEvent(QCloseEvent* theEvent)
{
  if (myLoading)
  {
    theEvent->ignore();
    return;
  }
  QSettings aSettings;
  aSettings.setValue("window/geometry", saveGeometry());
  aSettings.setValue("window/state", saveState());
  QMainWindow::closeEvent(theEvent);
}

void MainWindow::onControls()
{
  QMessageBox::information(this, "Mouse & Keyboard",
                           "<b>Mouse</b><br>"
                           "Left drag: rotate (around the point under the cursor)<br>"
                           "Middle / right drag, or Ctrl + left drag: pan<br>"
                           "Wheel: zoom at cursor<br>"
                           "Click: select face / edge / vertex<br>"
                           "<b>Ruler (R)</b>: click a point, then a second point &rarr; distance with X/Y/Z components."
                           " Points snap to corners, edge end points and edge midpoints (green ring);"
                           " otherwise the exact point on the edge or face under the cursor is used.<br>"
                           "Shift + click: add a second entity to measure distance / angle<br>"
                           "Click the view cube: snap to that view<br><br>"
                           "<b>Keys</b><br>"
                           "Ctrl+O open &nbsp; F fit &nbsp; 0 iso &nbsp; 1-6 standard views<br>"
                           "R ruler &nbsp; X x-ray &nbsp; K keep dimension &nbsp; Esc clear");
}

void MainWindow::onAbout()
{
  QMessageBox::about(this, "About STEP Viewer",
                     "<b>STEP Viewer 1.0</b><br>A lightweight viewer and measuring tool for STEP (.step/.stp) files."
                     "<br><br>Built with Open CASCADE Technology 8.0 (LGPL 2.1 with exception) and Qt 6 (LGPL 3)."
                     "<br>License texts are in the <i>licenses</i> folder of the installation."
                     "<br><br>Free and open source (MIT):<br>"
                     "<a href=\"https://github.com/kartikddoshi/step-viewer\">github.com/kartikddoshi/step-viewer</a>");
}
