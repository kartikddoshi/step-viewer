#include "Measure.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <gp_Circ.hxx>
#include <gp_Cone.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Elips.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <gp_Sphere.hxx>
#include <gp_Torus.hxx>

#include <cmath>
#include <optional>

// ------------------------------------------------------------------ formatting

namespace
{
constexpr double MM_PER_INCH = 25.4;
const QChar      SQ(0x00B2);  // ²
const QChar      CUBE(0x00B3); // ³
const QChar      DIA(0x00D8);  // Ø
const QChar      DEG(0x00B0);  // °
} // namespace

QString UnitFormat::Length(double theMm) const
{
  if (Unit == LengthUnit::Inch)
    return QString::number(theMm / MM_PER_INCH, 'f', Decimals + 1) + " in";
  return QString::number(theMm, 'f', Decimals) + " mm";
}

QString UnitFormat::Area(double theMm2) const
{
  if (Unit == LengthUnit::Inch)
    return QString::number(theMm2 / (MM_PER_INCH * MM_PER_INCH), 'f', Decimals + 1) + " in" + SQ;
  return QString::number(theMm2, 'f', Decimals) + " mm" + SQ;
}

QString UnitFormat::Volume(double theMm3) const
{
  if (Unit == LengthUnit::Inch)
    return QString::number(theMm3 / (MM_PER_INCH * MM_PER_INCH * MM_PER_INCH), 'f', Decimals + 1) + " in" + CUBE;
  return QString::number(theMm3, 'f', Decimals) + " mm" + CUBE;
}

QString UnitFormat::Point(const gp_Pnt& theP) const
{
  const double k = Unit == LengthUnit::Inch ? 1.0 / MM_PER_INCH : 1.0;
  const int    d = Unit == LengthUnit::Inch ? Decimals + 1 : Decimals;
  return QString("%1, %2, %3").arg(theP.X() * k, 0, 'f', d).arg(theP.Y() * k, 0, 'f', d).arg(theP.Z() * k, 0, 'f', d);
}

QString UnitFormat::Angle(double theRad)
{
  return QString::number(theRad * 180.0 / M_PI, 'f', 3) + DEG;
}

// ------------------------------------------------------------------ geometric features

namespace
{
QString dirText(const gp_Dir& theD)
{
  return QString("%1, %2, %3").arg(theD.X(), 0, 'f', 4).arg(theD.Y(), 0, 'f', 4).arg(theD.Z(), 0, 'f', 4);
}

double edgeLength(const TopoDS_Edge& theEdge)
{
  if (BRep_Tool::Degenerated(theEdge))
    return 0.0;
  BRepAdaptor_Curve aCurve(theEdge);
  return GCPnts_AbscissaPoint::Length(aCurve);
}

struct LineFeature
{
  gp_Lin Line;
};
struct PlaneFeature
{
  gp_Pln Plane;
  gp_Dir Normal; //!< outward normal (respects face orientation)
  gp_Pnt Center;
};
struct AxisFeature
{
  gp_Ax1 Axis;
  gp_Pnt Center;     //!< circle center (or point on cylinder axis)
  double Radius = 0;
  bool   IsCircle = false;
};

std::optional<LineFeature> asLine(const TopoDS_Shape& theS)
{
  if (theS.ShapeType() != TopAbs_EDGE)
    return std::nullopt;
  BRepAdaptor_Curve aCurve(TopoDS::Edge(theS));
  if (aCurve.GetType() != GeomAbs_Line)
    return std::nullopt;
  return LineFeature{aCurve.Line()};
}

std::optional<PlaneFeature> asPlane(const TopoDS_Shape& theS)
{
  if (theS.ShapeType() != TopAbs_FACE)
    return std::nullopt;
  const TopoDS_Face&  aFace = TopoDS::Face(theS);
  BRepAdaptor_Surface aSurf(aFace, true);
  if (aSurf.GetType() != GeomAbs_Plane)
    return std::nullopt;
  PlaneFeature aRes;
  aRes.Plane  = aSurf.Plane();
  aRes.Normal = aRes.Plane.Axis().Direction();
  if (aFace.Orientation() == TopAbs_REVERSED)
    aRes.Normal.Reverse();
  GProp_GProps aProps;
  BRepGProp::SurfaceProperties(aFace, aProps);
  aRes.Center = aProps.CentreOfMass();
  return aRes;
}

std::optional<AxisFeature> asAxis(const TopoDS_Shape& theS)
{
  if (theS.ShapeType() == TopAbs_EDGE)
  {
    BRepAdaptor_Curve aCurve(TopoDS::Edge(theS));
    if (aCurve.GetType() == GeomAbs_Circle)
    {
      const gp_Circ c = aCurve.Circle();
      return AxisFeature{c.Axis(), c.Location(), c.Radius(), true};
    }
  }
  else if (theS.ShapeType() == TopAbs_FACE)
  {
    BRepAdaptor_Surface aSurf(TopoDS::Face(theS), true);
    if (aSurf.GetType() == GeomAbs_Cylinder)
    {
      const gp_Cylinder c = aSurf.Cylinder();
      GProp_GProps      aProps;
      BRepGProp::SurfaceProperties(theS, aProps);
      // project the face centroid onto the axis to get a meaningful axis point
      const gp_Lin anAxisLin(c.Axis());
      const gp_Pnt aCentroid = aProps.CentreOfMass();
      const double t         = gp_Vec(anAxisLin.Location(), aCentroid).Dot(gp_Vec(anAxisLin.Direction()));
      const gp_Pnt anOnAxis  = anAxisLin.Location().Translated(gp_Vec(anAxisLin.Direction()) * t);
      return AxisFeature{c.Axis(), anOnAxis, c.Radius(), false};
    }
    if (aSurf.GetType() == GeomAbs_Cone)
    {
      const gp_Cone c = aSurf.Cone();
      return AxisFeature{c.Axis(), c.Location(), c.RefRadius(), false};
    }
  }
  return std::nullopt;
}

//! Angle between two directions folded to [0, 90] degrees.
double acuteAngle(const gp_Dir& theA, const gp_Dir& theB)
{
  double a = theA.Angle(theB);
  if (a > M_PI / 2)
    a = M_PI - a;
  return a;
}

constexpr double PARALLEL_TOL = 1.0e-6; // radians

void addBox(MeasureResult& theRes, const TopoDS_Shape& theShape, const UnitFormat& theFmt)
{
  Bnd_Box aBox;
  BRepBndLib::AddOptimal(theShape, aBox, false, false); // exact geometry, not mesh
  if (aBox.IsVoid())
    return;
  double x0, y0, z0, x1, y1, z1;
  aBox.Get(x0, y0, z0, x1, y1, z1);
  theRes.Add("Size X", theFmt.Length(x1 - x0));
  theRes.Add("Size Y", theFmt.Length(y1 - y0));
  theRes.Add("Size Z", theFmt.Length(z1 - z0));
  theRes.Add("Box min", theFmt.Point(gp_Pnt(x0, y0, z0)));
  theRes.Add("Box max", theFmt.Point(gp_Pnt(x1, y1, z1)));
}
} // namespace

// ------------------------------------------------------------------ public API

QString Measure::TypeName(const TopoDS_Shape& theShape)
{
  switch (theShape.ShapeType())
  {
    case TopAbs_VERTEX: return "Vertex";
    case TopAbs_EDGE: return "Edge";
    case TopAbs_WIRE: return "Wire";
    case TopAbs_FACE: return "Face";
    case TopAbs_SHELL: return "Shell";
    case TopAbs_SOLID: return "Solid";
    case TopAbs_COMPSOLID: return "Compsolid";
    case TopAbs_COMPOUND: return "Compound";
    default: return "Shape";
  }
}

MeasureResult Measure::Single(const TopoDS_Shape& theShape, const UnitFormat& theFmt)
{
  MeasureResult aRes;
  if (theShape.IsNull())
    return aRes;
  try
  {
    switch (theShape.ShapeType())
    {
      case TopAbs_VERTEX:
      {
        const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(theShape));
        aRes.Title     = "Point";
        aRes.Add("Position (X, Y, Z)", theFmt.Point(p));
        aRes.Graphics.push_back({MeasureGraphic::Marker, p, p, QString()});
        break;
      }
      case TopAbs_EDGE:
      {
        const TopoDS_Edge& anEdge = TopoDS::Edge(theShape);
        BRepAdaptor_Curve  aCurve(anEdge);
        const double       aLen = edgeLength(anEdge);
        const double       u0 = aCurve.FirstParameter(), u1 = aCurve.LastParameter();
        const gp_Pnt       aMid = aCurve.Value(0.5 * (u0 + u1));
        switch (aCurve.GetType())
        {
          case GeomAbs_Line:
          {
            aRes.Title = "Line edge";
            aRes.Add("Length", theFmt.Length(aLen));
            aRes.Add("Direction", dirText(aCurve.Line().Direction()));
            aRes.Graphics.push_back({MeasureGraphic::Label, aMid, aMid, theFmt.Length(aLen)});
            break;
          }
          case GeomAbs_Circle:
          {
            const gp_Circ c        = aCurve.Circle();
            const double  aSpan    = u1 - u0;
            const bool    isClosed = std::abs(aSpan - 2.0 * M_PI) < 1.0e-6;
            aRes.Title             = isClosed ? "Circle" : "Arc";
            aRes.Add("Diameter", theFmt.Length(2.0 * c.Radius()));
            aRes.Add("Radius", theFmt.Length(c.Radius()));
            aRes.Add(isClosed ? "Circumference" : "Arc length", theFmt.Length(aLen));
            if (!isClosed)
              aRes.Add("Arc angle", UnitFormat::Angle(aSpan));
            aRes.Add("Center", theFmt.Point(c.Location()));
            aRes.Add("Axis", dirText(c.Axis().Direction()));
            aRes.Graphics.push_back({MeasureGraphic::Marker, c.Location(), c.Location(), QString()});
            aRes.Graphics.push_back({MeasureGraphic::Label, c.Location(), c.Location(),
                                     (isClosed ? QString(DIA) + " " + theFmt.Length(2.0 * c.Radius())
                                               : "R " + theFmt.Length(c.Radius()))});
            break;
          }
          case GeomAbs_Ellipse:
          {
            const gp_Elips e = aCurve.Ellipse();
            aRes.Title       = "Ellipse";
            aRes.Add("Major radius", theFmt.Length(e.MajorRadius()));
            aRes.Add("Minor radius", theFmt.Length(e.MinorRadius()));
            aRes.Add("Length", theFmt.Length(aLen));
            aRes.Add("Center", theFmt.Point(e.Location()));
            break;
          }
          default:
          {
            aRes.Title = "Curve edge";
            aRes.Add("Length", theFmt.Length(aLen));
            aRes.Graphics.push_back({MeasureGraphic::Label, aMid, aMid, theFmt.Length(aLen)});
            break;
          }
        }
        if (!BRep_Tool::IsClosed(anEdge))
        {
          TopoDS_Vertex v0, v1;
          TopExp::Vertices(anEdge, v0, v1, true);
          if (!v0.IsNull() && !v1.IsNull())
          {
            const gp_Pnt p0 = BRep_Tool::Pnt(v0), p1 = BRep_Tool::Pnt(v1);
            aRes.Add("Start point", theFmt.Point(p0));
            aRes.Add("End point", theFmt.Point(p1));
            if (aCurve.GetType() != GeomAbs_Line)
              aRes.Add("Chord length", theFmt.Length(p0.Distance(p1)));
          }
        }
        break;
      }
      case TopAbs_FACE:
      {
        const TopoDS_Face&  aFace = TopoDS::Face(theShape);
        BRepAdaptor_Surface aSurf(aFace, true);
        GProp_GProps        aSProps, aLProps;
        BRepGProp::SurfaceProperties(aFace, aSProps);
        BRepGProp::LinearProperties(aFace, aLProps);
        const gp_Pnt aCentroid = aSProps.CentreOfMass();
        switch (aSurf.GetType())
        {
          case GeomAbs_Plane:
          {
            aRes.Title = "Planar face";
            gp_Dir n   = aSurf.Plane().Axis().Direction();
            if (aFace.Orientation() == TopAbs_REVERSED)
              n.Reverse();
            aRes.Add("Normal", dirText(n));
            break;
          }
          case GeomAbs_Cylinder:
          {
            const gp_Cylinder c = aSurf.Cylinder();
            aRes.Title          = "Cylindrical face";
            aRes.Add("Diameter", theFmt.Length(2.0 * c.Radius()));
            aRes.Add("Radius", theFmt.Length(c.Radius()));
            aRes.Add("Axis direction", dirText(c.Axis().Direction()));
            aRes.Add("Axis point", theFmt.Point(c.Axis().Location()));
            aRes.Graphics.push_back({MeasureGraphic::Label, aCentroid, aCentroid,
                                     QString(DIA) + " " + theFmt.Length(2.0 * c.Radius())});
            break;
          }
          case GeomAbs_Cone:
          {
            const gp_Cone c = aSurf.Cone();
            aRes.Title      = "Conical face";
            aRes.Add("Half angle", UnitFormat::Angle(std::abs(c.SemiAngle())));
            aRes.Add("Included angle", UnitFormat::Angle(2.0 * std::abs(c.SemiAngle())));
            aRes.Add("Reference diameter", theFmt.Length(2.0 * c.RefRadius()));
            aRes.Add("Axis direction", dirText(c.Axis().Direction()));
            break;
          }
          case GeomAbs_Sphere:
          {
            const gp_Sphere s = aSurf.Sphere();
            aRes.Title        = "Spherical face";
            aRes.Add("Diameter", theFmt.Length(2.0 * s.Radius()));
            aRes.Add("Radius", theFmt.Length(s.Radius()));
            aRes.Add("Center", theFmt.Point(s.Location()));
            aRes.Graphics.push_back({MeasureGraphic::Label, aCentroid, aCentroid,
                                     "S" + QString(DIA) + " " + theFmt.Length(2.0 * s.Radius())});
            break;
          }
          case GeomAbs_Torus:
          {
            const gp_Torus t = aSurf.Torus();
            aRes.Title       = "Toroidal face";
            aRes.Add("Major radius", theFmt.Length(t.MajorRadius()));
            aRes.Add("Minor radius", theFmt.Length(t.MinorRadius()));
            aRes.Add("Center", theFmt.Point(t.Location()));
            break;
          }
          default: aRes.Title = "Freeform face"; break;
        }
        aRes.Add("Area", theFmt.Area(aSProps.Mass()));
        aRes.Add("Perimeter", theFmt.Length(aLProps.Mass()));
        aRes.Add("Centroid", theFmt.Point(aCentroid));
        break;
      }
      default:
      {
        aRes = Model(theShape, theFmt);
        aRes.Title = TypeName(theShape);
        break;
      }
    }
  }
  catch (const Standard_Failure& anErr)
  {
    aRes.Add("Error", QString::fromUtf8(anErr.what()));
  }
  return aRes;
}

MeasureResult Measure::Pair(const TopoDS_Shape& theA, const TopoDS_Shape& theB, const UnitFormat& theFmt)
{
  MeasureResult aRes;
  aRes.Title = TypeName(theA) + " ↔ " + TypeName(theB);
  try
  {
    // --- minimum distance (always)
    BRepExtrema_DistShapeShape aDist(theA, theB);
    bool                       hasMin = aDist.IsDone() && aDist.NbSolution() > 0;
    gp_Pnt                     p1, p2;
    double                     aMinDist = 0.0;
    if (hasMin)
    {
      aMinDist = aDist.Value();
      p1       = aDist.PointOnShape1(1);
      p2       = aDist.PointOnShape2(1);
    }

    bool hasPrimaryGraphic = false;

    // --- circle/cylinder axes: center distance, axis distance
    const auto anAxA = asAxis(theA);
    const auto anAxB = asAxis(theB);
    if (anAxA && anAxB)
    {
      const double anAng = acuteAngle(anAxA->Axis.Direction(), anAxB->Axis.Direction());
      if (anAxA->IsCircle && anAxB->IsCircle)
      {
        const gp_Pnt c1 = anAxA->Center, c2 = anAxB->Center;
        const double d  = c1.Distance(c2);
        aRes.Title      = "Center to center";
        aRes.Add("Center distance", theFmt.Length(d));
        aRes.Add("ΔX", theFmt.Length(std::abs(c2.X() - c1.X())));
        aRes.Add("ΔY", theFmt.Length(std::abs(c2.Y() - c1.Y())));
        aRes.Add("ΔZ", theFmt.Length(std::abs(c2.Z() - c1.Z())));
        aRes.Graphics.push_back({MeasureGraphic::Line, c1, c2, theFmt.Length(d)});
        hasPrimaryGraphic = true;
      }
      if (anAng < PARALLEL_TOL)
      {
        const gp_Lin aLin(anAxA->Axis);
        const double d = aLin.Distance(anAxB->Axis.Location());
        if (aRes.Title.startsWith(TypeName(theA)))
          aRes.Title = "Parallel axes";
        aRes.Add("Axis to axis distance", theFmt.Length(d));
        if (!hasPrimaryGraphic)
        {
          const gp_Pnt q2 = anAxB->Center;
          const gp_Lin aLinA(anAxA->Axis);
          const double t  = gp_Vec(aLinA.Location(), q2).Dot(gp_Vec(aLinA.Direction()));
          const gp_Pnt q1 = aLinA.Location().Translated(gp_Vec(aLinA.Direction()) * t);
          aRes.Graphics.push_back({MeasureGraphic::Line, q1, q2, theFmt.Length(d)});
          hasPrimaryGraphic = true;
        }
      }
      else
      {
        aRes.Add("Angle between axes", UnitFormat::Angle(anAng));
      }
    }

    // --- planes
    const auto aPlA = asPlane(theA);
    const auto aPlB = asPlane(theB);
    if (aPlA && aPlB)
    {
      const double anAng = aPlA->Normal.Angle(aPlB->Normal);
      aRes.Add("Angle between normals", UnitFormat::Angle(anAng));
      aRes.Add("Angle between faces", UnitFormat::Angle(M_PI - anAng));
      if (acuteAngle(aPlA->Normal, aPlB->Normal) < PARALLEL_TOL)
      {
        const double d = aPlA->Plane.Distance(aPlB->Center);
        aRes.Title     = "Parallel faces";
        aRes.Add("Perpendicular distance", theFmt.Length(d));
        // dimension line from centroid of B dropped onto plane A
        const gp_Dir n  = aPlA->Plane.Axis().Direction();
        const double s  = gp_Vec(aPlA->Plane.Location(), aPlB->Center).Dot(gp_Vec(n));
        const gp_Pnt q1 = aPlB->Center.Translated(gp_Vec(n) * -s);
        aRes.Graphics.push_back({MeasureGraphic::Line, q1, aPlB->Center, theFmt.Length(d)});
        hasPrimaryGraphic = true;
      }
      else
      {
        aRes.Title = "Face angle";
      }
    }

    // --- lines
    const auto aLnA = asLine(theA);
    const auto aLnB = asLine(theB);
    if (aLnA && aLnB)
    {
      const double anAng = acuteAngle(aLnA->Line.Direction(), aLnB->Line.Direction());
      aRes.Add("Angle between lines", UnitFormat::Angle(anAng));
      if (anAng < PARALLEL_TOL)
      {
        aRes.Title = "Parallel lines";
        aRes.Add("Line to line distance", theFmt.Length(aLnA->Line.Distance(aLnB->Line.Location())));
      }
      else
      {
        aRes.Title = "Line angle";
      }
    }

    // --- line vs plane
    const auto aLinePlane = [&](const std::optional<LineFeature>& theL, const std::optional<PlaneFeature>& theP) {
      if (theL && theP)
      {
        const double a = acuteAngle(theL->Line.Direction(), theP->Plane.Axis().Direction());
        aRes.Add("Angle edge to face", UnitFormat::Angle(M_PI / 2 - a));
        if (std::abs(M_PI / 2 - a) < PARALLEL_TOL)
          aRes.Add("Edge to plane distance", theFmt.Length(theP->Plane.Distance(theL->Line.Location())));
      }
    };
    aLinePlane(aLnA, aPlB);
    aLinePlane(aLnB, aPlA);

    // --- axis vs plane (e.g. hole to face)
    const auto anAxisPlane = [&](const std::optional<AxisFeature>& theAx, const std::optional<PlaneFeature>& theP) {
      if (theAx && theP)
      {
        const double a = acuteAngle(theAx->Axis.Direction(), theP->Plane.Axis().Direction());
        if (std::abs(M_PI / 2 - a) < PARALLEL_TOL)
          aRes.Add("Axis to plane distance", theFmt.Length(theP->Plane.Distance(theAx->Center)));
        else if (a < PARALLEL_TOL && theAx->IsCircle)
          aRes.Add("Center to plane distance", theFmt.Length(theP->Plane.Distance(theAx->Center)));
      }
    };
    anAxisPlane(anAxA, aPlB);
    anAxisPlane(anAxB, aPlA);

    // --- point vs plane / axis
    if (theA.ShapeType() == TopAbs_VERTEX || theB.ShapeType() == TopAbs_VERTEX)
    {
      const TopoDS_Shape& aV     = theA.ShapeType() == TopAbs_VERTEX ? theA : theB;
      const gp_Pnt        p      = BRep_Tool::Pnt(TopoDS::Vertex(aV));
      const auto&         aPl    = theA.ShapeType() == TopAbs_VERTEX ? aPlB : aPlA;
      const auto&         anAx   = theA.ShapeType() == TopAbs_VERTEX ? anAxB : anAxA;
      if (aPl)
        aRes.Add("Point to plane distance", theFmt.Length(aPl->Plane.Distance(p)));
      if (anAx)
      {
        aRes.Add("Point to axis distance", theFmt.Length(gp_Lin(anAx->Axis).Distance(p)));
        if (anAx->IsCircle)
          aRes.Add("Point to center distance", theFmt.Length(p.Distance(anAx->Center)));
      }
      if (theA.ShapeType() == TopAbs_VERTEX && theB.ShapeType() == TopAbs_VERTEX)
        aRes.Title = "Point to point";
    }

    if (hasMin)
    {
      std::vector<std::pair<QString, QString>> aRows;
      aRows.emplace_back("Minimum distance", theFmt.Length(aMinDist));
      if (!hasPrimaryGraphic || theA.ShapeType() == TopAbs_VERTEX || theB.ShapeType() == TopAbs_VERTEX)
      {
        aRows.emplace_back("ΔX", theFmt.Length(std::abs(p2.X() - p1.X())));
        aRows.emplace_back("ΔY", theFmt.Length(std::abs(p2.Y() - p1.Y())));
        aRows.emplace_back("ΔZ", theFmt.Length(std::abs(p2.Z() - p1.Z())));
      }
      aRes.Rows.insert(aRes.Rows.begin(), aRows.begin(), aRows.end());
      if (!hasPrimaryGraphic && aMinDist > 1.0e-7)
      {
        aRes.Graphics.push_back({MeasureGraphic::Line, p1, p2, theFmt.Length(aMinDist)});
      }
      if (aRes.Title.contains(QString::fromUtf8("↔")))
        aRes.Title = "Distance";
    }
  }
  catch (const Standard_Failure& anErr)
  {
    aRes.Add("Error", QString::fromUtf8(anErr.what()));
  }
  return aRes;
}

MeasureResult Measure::Model(const TopoDS_Shape& theShape, const UnitFormat& theFmt)
{
  MeasureResult aRes;
  aRes.Title = "Model";
  if (theShape.IsNull())
    return aRes;
  try
  {
    TopTools_IndexedMapOfShape aSolids, aFaces, anEdges, aVerts;
    TopExp::MapShapes(theShape, TopAbs_SOLID, aSolids);
    TopExp::MapShapes(theShape, TopAbs_FACE, aFaces);
    TopExp::MapShapes(theShape, TopAbs_EDGE, anEdges);
    TopExp::MapShapes(theShape, TopAbs_VERTEX, aVerts);

    addBox(aRes, theShape, theFmt);

    if (aSolids.Extent() > 0)
    {
      GProp_GProps aVProps;
      BRepGProp::VolumeProperties(theShape, aVProps, true);
      aRes.Add("Volume", theFmt.Volume(aVProps.Mass()));
      aRes.Add("Center of mass", theFmt.Point(aVProps.CentreOfMass()));
    }
    GProp_GProps aSProps;
    BRepGProp::SurfaceProperties(theShape, aSProps);
    aRes.Add("Surface area", theFmt.Area(aSProps.Mass()));

    aRes.Add("Solids", QString::number(aSolids.Extent()));
    aRes.Add("Faces", QString::number(aFaces.Extent()));
    aRes.Add("Edges", QString::number(anEdges.Extent()));
    aRes.Add("Vertices", QString::number(aVerts.Extent()));
  }
  catch (const Standard_Failure& anErr)
  {
    aRes.Add("Error", QString::fromUtf8(anErr.what()));
  }
  return aRes;
}
