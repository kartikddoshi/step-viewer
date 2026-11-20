#pragma once

#include "StepModel.h"

#include <cstdint>
#include <vector>

//! Premultiplied BGRA, top-down rows.
struct ThumbImage
{
  int                  Width  = 0;
  int                  Height = 0;
  std::vector<uint8_t> Pixels;
};

namespace stepcore
{
//! Render an isometric shaded preview of the model with a transparent background.
//! Does not need a GPU / OpenGL context (safe inside the Explorer thumbnail host process).
bool RenderThumbnail(const StepModel& theModel, int theSize, ThumbImage& theImage);
} // namespace stepcore
