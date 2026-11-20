#pragma once

#include <TDocStd_Document.hxx>
#include <TDF_LabelSequence.hxx>
#include <TopoDS_Shape.hxx>

#include <istream>
#include <mutex>
#include <string>

//! A STEP file loaded into an XCAF document (keeps names, colors, assembly structure).
//! All geometry is converted to millimetres.
struct StepModel
{
  Handle(TDocStd_Document) Doc;
  TDF_LabelSequence        Roots;  //!< free (top-level) shape labels
  TopoDS_Shape             Shape;  //!< compound of all root shapes
  std::string              Error;

  bool IsNull() const { return Shape.IsNull(); }
};

namespace stepcore
{
//! Load a STEP file from disk (UTF-8 path).
bool LoadStepFile(const std::string& theUtf8Path, StepModel& theModel);

//! Load a STEP file from a stream (used by the Explorer thumbnail handler).
bool LoadStepStream(std::istream& theStream, StepModel& theModel);

//! Close the XCAF document and free memory.
void ReleaseModel(StepModel& theModel);

//! Triangulate the model. theRelDeflection is relative to the model size.
void MeshShape(const TopoDS_Shape& theShape, double theRelDeflection, double theAngDeflectionRad);

//! STEP translator uses process-wide static parameters, so readers must be serialized.
std::mutex& ReaderMutex();
} // namespace stepcore
