// Headless self-test:
//  1. builds a colored 2-part assembly with known dimensions and writes it as STEP (samples/test_assembly.step)
//  2. reloads it through the same loader the app uses
//  3. checks measurement results against the known geometry
//  4. renders a thumbnail
#include "Measure.h"
#include "StepModel.h"
#include "ThumbRenderer.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <Quantity_Color.hxx>
#include <gp_Ax2.hxx>

#include <QStringList>

#include <cstdio>
#include <string>
#include <vector>

static int g_failures = 0;

static void check(const MeasureResult& theRes, const QString& theRow, double theExpected)
{
  for (const auto& aRow : theRes.Rows)
  {
    if (aRow.first == theRow)
    {
      const double aVal = aRow.second.split(' ').first().toDouble();
      const bool   isOk = std::abs(aVal - theExpected) < 1.0e-3;
      std::printf("  [%s] %-28s = %-14s (expected %.3f)\n", isOk ? " OK " : "FAIL", theRow.toUtf8().constData(),
                  aRow.second.toUtf8().constData(), theExpected);
      if (!isOk)
        ++g_failures;
      return;
    }
  }
  std::printf("  [FAIL] row '%s' missing in '%s'\n", theRow.toUtf8().constData(), theRes.Title.toUtf8().constData());
  ++g_failures;
}

int main(int argc, char** argv)
{
  const std::string anOut = argc > 1 ? argv[1] : "test_assembly.step";

  // ---- 1. build: plate 100x60x10 with two D10 through-holes 60 apart + a D20x30 boss
  TopoDS_Shape aPlate = BRepPrimAPI_MakeBox(100.0, 60.0, 10.0).Shape();
  for (double x : {20.0, 80.0})
  {
    TopoDS_Shape aHole = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x, 30.0, -1.0), gp::DZ()), 5.0, 12.0).Shape();
    aPlate             = BRepAlgoAPI_Cut(aPlate, aHole).Shape();
  }
  TopoDS_Shape aBoss = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(50.0, 30.0, 10.0), gp::DZ()), 10.0, 30.0).Shape();

  Handle(TDocStd_Document) aDoc;
  XCAFApp_Application::GetApplication()->NewDocument("BinXCAF", aDoc);
  Handle(XCAFDoc_ShapeTool) aShapeTool = XCAFDoc_DocumentTool::ShapeTool(aDoc->Main());
  Handle(XCAFDoc_ColorTool) aColorTool = XCAFDoc_DocumentTool::ColorTool(aDoc->Main());
  TDF_Label anAsm   = aShapeTool->NewShape();
  TDF_Label aPlateL = aShapeTool->AddShape(aPlate, false);
  TDF_Label aBossL  = aShapeTool->AddShape(aBoss, false);
  aShapeTool->AddComponent(anAsm, aPlateL, TopLoc_Location());
  aShapeTool->AddComponent(anAsm, aBossL, TopLoc_Location());
  aShapeTool->UpdateAssemblies();
  aColorTool->SetColor(aPlateL, Quantity_Color(0.85, 0.35, 0.15, Quantity_TOC_sRGB), XCAFDoc_ColorSurf);
  aColorTool->SetColor(aBossL, Quantity_Color(0.20, 0.45, 0.85, Quantity_TOC_sRGB), XCAFDoc_ColorSurf);

  STEPCAFControl_Writer aWriter;
  if (!aWriter.Transfer(aDoc, STEPControl_AsIs) || aWriter.Write(anOut.c_str()) != IFSelect_RetDone)
  {
    std::printf("FAIL: cannot write %s\n", anOut.c_str());
    return 1;
  }
  std::printf("wrote %s\n", anOut.c_str());

  // ---- 2. reload
  StepModel aModel;
  if (!stepcore::LoadStepFile(anOut, aModel))
  {
    std::printf("FAIL: reload: %s\n", aModel.Error.c_str());
    return 1;
  }

  // ---- 3. measurements
  std::vector<TopoDS_Shape> aHoleFaces, aHoleTopEdges, aPlateZFaces;
  for (TopExp_Explorer anExp(aModel.Shape, TopAbs_FACE); anExp.More(); anExp.Next())
  {
    BRepAdaptor_Surface aSurf(TopoDS::Face(anExp.Current()));
    if (aSurf.GetType() == GeomAbs_Cylinder && std::abs(aSurf.Cylinder().Radius() - 5.0) < 1e-6)
      aHoleFaces.push_back(anExp.Current());
    if (aSurf.GetType() == GeomAbs_Plane && std::abs(std::abs(aSurf.Plane().Axis().Direction().Z()) - 1.0) < 1e-9)
    {
      // plate bottom (z=0) and plate top (z=10, the large one - the boss bottom disk is also at z=10)
      const double z = aSurf.Plane().Location().Z();
      GProp_GProps aProps;
      BRepGProp::SurfaceProperties(anExp.Current(), aProps);
      if ((std::abs(z) < 1e-6 || std::abs(z - 10.0) < 1e-6) && aProps.Mass() > 1000.0)
        aPlateZFaces.push_back(anExp.Current());
    }
  }
  for (TopExp_Explorer anExp(aModel.Shape, TopAbs_EDGE); anExp.More(); anExp.Next())
  {
    BRepAdaptor_Curve aCurve(TopoDS::Edge(anExp.Current()));
    if (aCurve.GetType() == GeomAbs_Circle && std::abs(aCurve.Circle().Radius() - 5.0) < 1e-6
        && std::abs(aCurve.Circle().Location().Z() - 10.0) < 1e-6)
      aHoleTopEdges.push_back(anExp.Current());
  }

  const UnitFormat aFmt;
  std::printf("Model:\n");
  const MeasureResult aModelRes = Measure::Model(aModel.Shape, aFmt);
  check(aModelRes, "Size X", 100.0);
  check(aModelRes, "Size Y", 60.0);
  check(aModelRes, "Size Z", 40.0);
  check(aModelRes, "Volume", 100.0 * 60.0 * 10.0 - 2 * M_PI * 25.0 * 10.0 + M_PI * 100.0 * 30.0);

  std::printf("Hole face (single), found %d:\n", (int)aHoleFaces.size());
  if (!aHoleFaces.empty())
    check(Measure::Single(aHoleFaces[0], aFmt), "Diameter", 10.0);

  std::printf("Hole to hole (cylinder faces):\n");
  if (aHoleFaces.size() >= 2)
    check(Measure::Pair(aHoleFaces[0], aHoleFaces[1], aFmt), "Axis to axis distance", 60.0);
  else
    ++g_failures;

  std::printf("Hole edge to hole edge (circles), found %d:\n", (int)aHoleTopEdges.size());
  if (aHoleTopEdges.size() >= 2)
  {
    const MeasureResult r = Measure::Pair(aHoleTopEdges[0], aHoleTopEdges[1], aFmt);
    check(r, "Center distance", 60.0);
    check(r, "Minimum distance", 50.0);
  }
  else
    ++g_failures;

  std::printf("Plate top/bottom faces, found %d:\n", (int)aPlateZFaces.size());
  if (aPlateZFaces.size() >= 2)
    check(Measure::Pair(aPlateZFaces[0], aPlateZFaces[1], aFmt), "Perpendicular distance", 10.0);

  std::printf("Inch formatting:\n");
  {
    UnitFormat anInch;
    anInch.Unit = LengthUnit::Inch;
    if (!aHoleFaces.empty())
      check(Measure::Single(aHoleFaces[0], anInch), "Diameter", 10.0 / 25.4);
  }

  // ---- 4. thumbnail
  ThumbImage anImg;
  const bool isThumb = stepcore::RenderThumbnail(aModel, 128, anImg);
  std::printf("Thumbnail: %s\n", isThumb ? "OK" : "FAIL");
  if (!isThumb)
    ++g_failures;

  stepcore::ReleaseModel(aModel);
  std::printf("\n%s (%d failure(s))\n", g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", g_failures);
  return g_failures == 0 ? 0 : 1;
}
