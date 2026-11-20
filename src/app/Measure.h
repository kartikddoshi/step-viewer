#pragma once

#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>

#include <QString>
#include <utility>
#include <vector>

enum class LengthUnit
{
  Millimeter,
  Inch
};

//! Formats values (internally always millimetres) in the selected display unit.
struct UnitFormat
{
  LengthUnit Unit     = LengthUnit::Millimeter;
  int        Decimals = 3;

  QString Length(double theMm) const;
  QString Area(double theMm2) const;
  QString Volume(double theMm3) const;
  QString Point(const gp_Pnt& theP) const;
  static QString Angle(double theRad);
};

//! A graphic annotation to show in the 3D view (dimension line or label).
struct MeasureGraphic
{
  enum Kind
  {
    Line,  //!< dimension line P1-P2 with label at midpoint
    Label, //!< text label at P1
    Marker //!< point marker at P1
  };
  Kind    Type = Label;
  gp_Pnt  P1, P2;
  QString Text;
};

struct MeasureResult
{
  QString                                  Title;
  std::vector<std::pair<QString, QString>> Rows;
  std::vector<MeasureGraphic>              Graphics;

  void Add(const QString& theName, const QString& theValue) { Rows.emplace_back(theName, theValue); }
};

namespace Measure
{
//! Properties of a single selected vertex / edge / face / solid.
MeasureResult Single(const TopoDS_Shape& theShape, const UnitFormat& theFmt);

//! Distance / angle between two selected entities.
MeasureResult Pair(const TopoDS_Shape& theA, const TopoDS_Shape& theB, const UnitFormat& theFmt);

//! Overall model statistics (bounding box, volume, area, counts).
MeasureResult Model(const TopoDS_Shape& theShape, const UnitFormat& theFmt);

QString TypeName(const TopoDS_Shape& theShape);
} // namespace Measure
