#include <windows.h> // first: OCCT headers may define NOUSER

#include "OcctWidget.h"

#include <Aspect_DisplayConnection.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <WNT_Window.hxx>

#include <QMouseEvent>
#include <QWheelEvent>

namespace
{
Aspect_VKeyMouse toVKeys(Qt::MouseButtons theButtons)
{
  Aspect_VKeyMouse aButtons = Aspect_VKeyMouse_NONE;
  if (theButtons & Qt::LeftButton)
    aButtons |= Aspect_VKeyMouse_LeftButton;
  if (theButtons & Qt::MiddleButton)
    aButtons |= Aspect_VKeyMouse_MiddleButton;
  if (theButtons & Qt::RightButton)
    aButtons |= Aspect_VKeyMouse_RightButton;
  return aButtons;
}

Aspect_VKeyFlags toVKeys(Qt::KeyboardModifiers theMods)
{
  Aspect_VKeyFlags aFlags = Aspect_VKeyFlags_NONE;
  if (theMods & Qt::ShiftModifier)
    aFlags |= Aspect_VKeyFlags_SHIFT;
  if (theMods & Qt::ControlModifier)
    aFlags |= Aspect_VKeyFlags_CTRL;
  if (theMods & Qt::AltModifier)
    aFlags |= Aspect_VKeyFlags_ALT;
  return aFlags;
}
} // namespace

OcctWidget::OcctWidget(QWidget* theParent)
    : QWidget(theParent)
{
  setAttribute(Qt::WA_NativeWindow);
  setAttribute(Qt::WA_PaintOnScreen);
  setAttribute(Qt::WA_NoSystemBackground);
  setAttribute(Qt::WA_OpaquePaintEvent);
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  setMinimumSize(200, 200);

  // CAD-style mouse mapping
  ChangeMouseGestureMap().Clear();
  ChangeMouseGestureMap().Bind(Aspect_VKeyMouse_LeftButton, AIS_MouseGesture_RotateOrbit);
  ChangeMouseGestureMap().Bind(Aspect_VKeyMouse_MiddleButton, AIS_MouseGesture_Pan);
  ChangeMouseGestureMap().Bind(Aspect_VKeyMouse_RightButton, AIS_MouseGesture_Pan);
  ChangeMouseGestureMap().Bind(Aspect_VKeyMouse_LeftButton | Aspect_VKeyFlags_CTRL, AIS_MouseGesture_Pan);
  ChangeMouseGestureMap().Bind(Aspect_VKeyMouse_MiddleButton | Aspect_VKeyFlags_CTRL, AIS_MouseGesture_Zoom);
  ChangeMouseGestureMap().Bind(Aspect_VKeyMouse_LeftButton | Aspect_VKeyFlags_SHIFT, AIS_MouseGesture_RotateOrbit);

  ChangeMouseSelectionSchemes().Clear();
  ChangeMouseSelectionSchemes().Bind(Aspect_VKeyMouse_LeftButton, AIS_SelectionScheme_Replace);
  ChangeMouseSelectionSchemes().Bind(Aspect_VKeyMouse_LeftButton | Aspect_VKeyFlags_SHIFT, AIS_SelectionScheme_XOR);
  ChangeMouseSelectionSchemes().Bind(Aspect_VKeyMouse_LeftButton | Aspect_VKeyFlags_CTRL, AIS_SelectionScheme_XOR);

  SetRotationMode(AIS_RotationMode_PickCenter);
  SetAllowDragging(false);
}

OcctWidget::~OcctWidget()
{
  if (!myContext.IsNull())
  {
    myContext->RemoveAll(false);
  }
  if (!myView.IsNull())
  {
    myView->Remove();
  }
}

void OcctWidget::initViewer()
{
  if (!myView.IsNull())
  {
    return;
  }
  Handle(Aspect_DisplayConnection) aDisp   = new Aspect_DisplayConnection();
  Handle(OpenGl_GraphicDriver)     aDriver = new OpenGl_GraphicDriver(aDisp, false);
  aDriver->ChangeOptions().buffersNoSwap = false;

  myViewer = new V3d_Viewer(aDriver);
  myViewer->SetDefaultLights();
  myViewer->SetLightOn();
  myViewer->SetDefaultTypeOfView(V3d_ORTHOGRAPHIC);

  myContext = new AIS_InteractiveContext(myViewer);
  myContext->SetAutomaticHilight(true);

  // highlight styles
  {
    Handle(Prs3d_Drawer) aDyn = myContext->HighlightStyle(Prs3d_TypeOfHighlight_LocalDynamic);
    aDyn->SetColor(Quantity_Color(0.10, 0.75, 1.00, Quantity_TOC_sRGB));
    aDyn->SetDisplayMode(1);
    Handle(Prs3d_Drawer) aSel = myContext->HighlightStyle(Prs3d_TypeOfHighlight_LocalSelected);
    aSel->SetColor(Quantity_Color(1.00, 0.55, 0.05, Quantity_TOC_sRGB));
    aSel->SetDisplayMode(1);
    myContext->HighlightStyle(Prs3d_TypeOfHighlight_Dynamic)->SetColor(Quantity_Color(0.10, 0.75, 1.00, Quantity_TOC_sRGB));
    myContext->HighlightStyle(Prs3d_TypeOfHighlight_Selected)->SetColor(Quantity_Color(1.00, 0.55, 0.05, Quantity_TOC_sRGB));
  }

  // default presentation: smoother tessellation than OCCT defaults
  myContext->DefaultDrawer()->SetDeviationCoefficient(0.0008);
  myContext->DefaultDrawer()->SetDeviationAngle(12.0 * M_PI / 180.0);
  myContext->DefaultDrawer()->SetFaceBoundaryDraw(true);
  myContext->DefaultDrawer()->SetFaceBoundaryAspect(
    new Prs3d_LineAspect(Quantity_Color(0.12, 0.12, 0.14, Quantity_TOC_sRGB), Aspect_TOL_SOLID, 1.0));

  myView = myViewer->CreateView();
  myView->SetImmediateUpdate(false);
  myView->SetBgGradientColors(Quantity_Color(0.93, 0.95, 0.97, Quantity_TOC_sRGB),
                              Quantity_Color(0.66, 0.72, 0.80, Quantity_TOC_sRGB),
                              Aspect_GradientFillMethod_Vertical);
  myView->ChangeRenderingParams().IsAntialiasingEnabled = true;
  myView->ChangeRenderingParams().NbMsaaSamples         = 4;
  myView->ChangeRenderingParams().Resolution            = (unsigned int)(96.0 * devicePixelRatioF() + 0.5);

  Handle(WNT_Window) aWindow = new WNT_Window((Aspect_Handle)winId());
  myView->SetWindow(aWindow);
  if (!aWindow->IsMapped())
  {
    aWindow->Map();
  }
  myView->SetProj(V3d_XposYnegZpos);
  myView->MustBeResized();

  // navigation cube (top-right)
  myViewCube = new AIS_ViewCube();
  myViewCube->SetViewAnimation(myViewAnimation);
  myViewCube->SetFixedAnimationLoop(false);
  myViewCube->SetAutoStartAnimation(true);
  myViewCube->SetSize(55.0 * devicePixelRatioF());
  myViewCube->SetFontHeight(12.0 * devicePixelRatioF());
  myViewCube->SetBoxColor(Quantity_Color(0.85, 0.88, 0.92, Quantity_TOC_sRGB));
  myViewCube->SetTransformPersistence(new Graphic3d_TransformPers(
    Graphic3d_TMF_TriedronPers, Aspect_TOTP_RIGHT_UPPER,
    Graphic3d_Vec2i(int(90 * devicePixelRatioF()), int(90 * devicePixelRatioF()))));
  myContext->Display(myViewCube, false);

  emit viewerReady();
}

Graphic3d_Vec2i OcctWidget::toPixels(const QPointF& thePos) const
{
  const double aRatio = devicePixelRatioF();
  return Graphic3d_Vec2i(int(thePos.x() * aRatio), int(thePos.y() * aRatio));
}

void OcctWidget::UpdateView()
{
  update();
}

void OcctWidget::FitAll()
{
  if (myView.IsNull())
    return;
  myView->FitAll(0.05, false);
  myView->ZFitAll();
  UpdateView();
}

void OcctWidget::SetProjection(V3d_TypeOfOrientation theOrient)
{
  if (myView.IsNull())
    return;
  myView->SetProj(theOrient);
  myView->FitAll(0.05, false);
  myView->ZFitAll();
  UpdateView();
}

void OcctWidget::showEvent(QShowEvent* theEvent)
{
  QWidget::showEvent(theEvent);
  initViewer();
}

void OcctWidget::paintEvent(QPaintEvent*)
{
  if (myView.IsNull())
  {
    initViewer();
    if (myView.IsNull())
      return;
  }
  // keep the GL viewport in sync with the native window size
  // (the view may be created before the final layout; Qt does not always send a resize event afterwards)
  RECT aRect = {};
  GetClientRect((HWND)winId(), &aRect);
  const Graphic3d_Vec2i aSize(aRect.right - aRect.left, aRect.bottom - aRect.top);
  if (aSize != myViewSize)
  {
    myViewSize = aSize;
    myView->MustBeResized();
    myView->Invalidate();
  }
  myView->InvalidateImmediate();
  FlushViewEvents(myContext, myView, true);
}

void OcctWidget::resizeEvent(QResizeEvent*)
{
  if (!myView.IsNull())
  {
    myView->MustBeResized();
    myView->Invalidate();
    update();
  }
}

void OcctWidget::mousePressEvent(QMouseEvent* theEvent)
{
  setFocus();
  if (!myView.IsNull()
      && UpdateMouseButtons(toPixels(theEvent->position()), toVKeys(theEvent->buttons()), toVKeys(theEvent->modifiers()), false))
  {
    UpdateView();
  }
}

void OcctWidget::mouseReleaseEvent(QMouseEvent* theEvent)
{
  if (!myView.IsNull()
      && UpdateMouseButtons(toPixels(theEvent->position()), toVKeys(theEvent->buttons()), toVKeys(theEvent->modifiers()), false))
  {
    UpdateView();
  }
}

void OcctWidget::mouseMoveEvent(QMouseEvent* theEvent)
{
  if (myPointPickMode && !myView.IsNull() && theEvent->buttons() == Qt::NoButton)
  {
    const Graphic3d_Vec2i aPnt = toPixels(theEvent->position());
    emit pointHovered(aPnt.x(), aPnt.y());
  }
  if (!myView.IsNull()
      && UpdateMousePosition(toPixels(theEvent->position()), toVKeys(theEvent->buttons()), toVKeys(theEvent->modifiers()), false))
  {
    UpdateView();
  }
}

void OcctWidget::wheelEvent(QWheelEvent* theEvent)
{
  if (myView.IsNull())
    return;
  const Graphic3d_Vec2i aPos = toPixels(theEvent->position());
  if (UpdateZoom(Aspect_ScrollDelta(aPos, double(theEvent->angleDelta().y()) / 8.0)))
  {
    UpdateView();
  }
}

void OcctWidget::leaveEvent(QEvent* theEvent)
{
  QWidget::leaveEvent(theEvent);
  if (myPointPickMode)
  {
    emit pointHoverLeft();
  }
  if (!myView.IsNull())
  {
    myContext->ClearDetected(false);
    myView->Redraw();
  }
}

void OcctWidget::OnSelectionChanged(const Handle(AIS_InteractiveContext)&, const Handle(V3d_View)&)
{
  emit selectionChanged();
}

void OcctWidget::handleViewRedraw(const Handle(AIS_InteractiveContext)& theCtx, const Handle(V3d_View)& theView)
{
  AIS_ViewController::handleViewRedraw(theCtx, theView);
  if (toAskNextFrame())
  {
    update(); // continue animation (view cube, inertia)
  }
}

bool OcctWidget::UpdateMouseClick(const Graphic3d_Vec2i& thePoint, Aspect_VKeyMouse theButton,
                                  Aspect_VKeyFlags theModifiers, bool theIsDoubleClick)
{
  if (myPointPickMode && theButton == Aspect_VKeyMouse_LeftButton && !myContext.IsNull())
  {
    // clicks on the view cube keep their normal behaviour
    if (!(myContext->HasDetected() && myContext->DetectedInteractive() == myViewCube))
    {
      const Graphic3d_Vec2i aPnt = thePoint;
      // deliver after the current event is processed (the selector is in use during event handling)
      QMetaObject::invokeMethod(this, [this, aPnt]() { emit pointClicked(aPnt.x(), aPnt.y()); }, Qt::QueuedConnection);
      return false;
    }
  }
  return AIS_ViewController::UpdateMouseClick(thePoint, theButton, theModifiers, theIsDoubleClick);
}
