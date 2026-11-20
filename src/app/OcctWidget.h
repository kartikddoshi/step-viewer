#pragma once

#include <AIS_InteractiveContext.hxx>
#include <AIS_ViewController.hxx>
#include <AIS_ViewCube.hxx>
#include <Graphic3d_Vec2.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>

#include <QWidget>

//! Native 3D viewport hosting an OCCT V3d_View inside a Qt widget.
//! Mouse: left-drag rotate, middle/right-drag pan, wheel zoom, click select, Shift+click add to selection.
class OcctWidget : public QWidget, public AIS_ViewController
{
  Q_OBJECT
public:
  explicit OcctWidget(QWidget* theParent = nullptr);
  ~OcctWidget() override;

  const Handle(AIS_InteractiveContext)& Context() const { return myContext; }
  const Handle(V3d_View)&               View() const { return myView; }

  //! Request a redraw of the viewport.
  void UpdateView();

  void FitAll();
  void SetProjection(V3d_TypeOfOrientation theOrient);

  //! Point-pick mode (ruler): a left click reports the clicked pixel through pointClicked() instead of selecting,
  //! mouse moves are reported through pointHovered(); dragging still rotates the view.
  void SetPointPickMode(bool theOn) { myPointPickMode = theOn; }
  bool IsPointPickMode() const { return myPointPickMode; }
  const Handle(AIS_ViewCube)& ViewCube() const { return myViewCube; }

  QPaintEngine* paintEngine() const override { return nullptr; }

signals:
  void selectionChanged();
  void viewerReady();
  void pointClicked(int theX, int theY); //!< physical pixels
  void pointHovered(int theX, int theY); //!< physical pixels
  void pointHoverLeft();

protected:
  void paintEvent(QPaintEvent*) override;
  void resizeEvent(QResizeEvent*) override;
  void showEvent(QShowEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void wheelEvent(QWheelEvent*) override;
  void leaveEvent(QEvent*) override;

  // AIS_ViewController
  void OnSelectionChanged(const Handle(AIS_InteractiveContext)& theCtx, const Handle(V3d_View)& theView) override;
  void handleViewRedraw(const Handle(AIS_InteractiveContext)& theCtx, const Handle(V3d_View)& theView) override;
  bool UpdateMouseClick(const Graphic3d_Vec2i& thePoint, Aspect_VKeyMouse theButton, Aspect_VKeyFlags theModifiers,
                        bool theIsDoubleClick) override;

private:
  void initViewer();
  Graphic3d_Vec2i toPixels(const QPointF& thePos) const;

private:
  Handle(V3d_Viewer)             myViewer;
  Handle(V3d_View)               myView;
  Handle(AIS_InteractiveContext) myContext;
  Handle(AIS_ViewCube)           myViewCube;
  Graphic3d_Vec2i                myViewSize;
  bool                           myPointPickMode = false;
};
