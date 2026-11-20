#include "StepModel.h"

#include <BRepBndLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS_Compound.hxx>
#include <UnitsMethods_LengthUnit.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <algorithm>
#include <cmath>
#include <functional>

namespace stepcore
{

std::mutex& ReaderMutex()
{
  static std::mutex THE_MUTEX;
  return THE_MUTEX;
}

namespace
{
bool loadWith(StepModel& theModel, const std::function<IFSelect_ReturnStatus(STEPCAFControl_Reader&)>& theRead)
{
  std::lock_guard<std::mutex> aLock(ReaderMutex());
  try
  {
    STEPCAFControl_Reader aReader;
    aReader.SetColorMode(true);
    aReader.SetNameMode(true);
    aReader.SetLayerMode(false);
    aReader.SetPropsMode(false);
    aReader.SetGDTMode(false);
    aReader.SetMatMode(false);
    aReader.SetViewMode(false);

    if (theRead(aReader) != IFSelect_RetDone)
    {
      theModel.Error = "The file could not be read as STEP (ISO 10303-21).";
      return false;
    }

    Handle(XCAFApp_Application) anApp = XCAFApp_Application::GetApplication();
    anApp->NewDocument("BinXCAF", theModel.Doc);
    XCAFDoc_DocumentTool::SetLengthUnit(theModel.Doc, 1.0, UnitsMethods_LengthUnit_Millimeter);

    if (!aReader.Transfer(theModel.Doc))
    {
      theModel.Error = "The STEP file contains no transferable geometry.";
      return false;
    }

    Handle(XCAFDoc_ShapeTool) aShapeTool = XCAFDoc_DocumentTool::ShapeTool(theModel.Doc->Main());
    aShapeTool->GetFreeShapes(theModel.Roots);

    TopoDS_Compound aComp;
    BRep_Builder    aBuilder;
    aBuilder.MakeCompound(aComp);
    int aNb = 0;
    for (const TDF_Label& aLabel : theModel.Roots)
    {
      TopoDS_Shape aShape = XCAFDoc_ShapeTool::GetShape(aLabel);
      if (!aShape.IsNull())
      {
        aBuilder.Add(aComp, aShape);
        ++aNb;
      }
    }
    if (aNb == 0)
    {
      theModel.Error = "The STEP file contains no shapes.";
      return false;
    }
    theModel.Shape = aComp;
    return true;
  }
  catch (const Standard_Failure& anErr)
  {
    theModel.Error = std::string("Geometry kernel error: ") + anErr.what();
  }
  catch (const std::exception& anErr)
  {
    theModel.Error = anErr.what();
  }
  catch (...)
  {
    theModel.Error = "Unknown error while reading the STEP file.";
  }
  return false;
}
} // namespace

bool LoadStepFile(const std::string& theUtf8Path, StepModel& theModel)
{
  return loadWith(theModel, [&](STEPCAFControl_Reader& theReader) {
    return theReader.ReadFile(theUtf8Path.c_str());
  });
}

bool LoadStepStream(std::istream& theStream, StepModel& theModel)
{
  return loadWith(theModel, [&](STEPCAFControl_Reader& theReader) {
    return theReader.ReadStream("stream.step", theStream);
  });
}

void ReleaseModel(StepModel& theModel)
{
  if (!theModel.Doc.IsNull())
  {
    std::lock_guard<std::mutex> aLock(ReaderMutex());
    Handle(XCAFApp_Application) anApp = XCAFApp_Application::GetApplication();
    if (theModel.Doc->IsOpened())
    {
      anApp->Close(theModel.Doc);
    }
  }
  theModel.Doc.Nullify();
  theModel.Roots.Clear();
  theModel.Shape.Nullify();
}

void MeshShape(const TopoDS_Shape& theShape, double theRelDeflection, double theAngDeflectionRad)
{
  Bnd_Box aBox;
  BRepBndLib::Add(theShape, aBox);
  if (aBox.IsVoid())
  {
    return;
  }
  double aXmin, aYmin, aZmin, aXmax, aYmax, aZmax;
  aBox.Get(aXmin, aYmin, aZmin, aXmax, aYmax, aZmax);
  const double aMaxDim = std::max({aXmax - aXmin, aYmax - aYmin, aZmax - aZmin});
  const double aDefl   = std::max(aMaxDim * theRelDeflection, 1.0e-4);
  BRepMesh_IncrementalMesh aMesher(theShape, aDefl, false, theAngDeflectionRad, true);
}

} // namespace stepcore
