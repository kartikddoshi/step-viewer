#include "ThumbRenderer.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <Poly_Triangle.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <Quantity_Color.hxx>
#include <Quantity_ColorRGBA.hxx>
#include <RWMesh_FaceIterator.hxx>
#include <Standard_Failure.hxx>
#include <XCAFPrs_DocumentExplorer.hxx>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
struct V3
{
  float x = 0, y = 0, z = 0;
};
inline V3 operator+(const V3& a, const V3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(const V3& a, const V3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(const V3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(const V3& a, const V3& b)
{
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline V3 normalize(const V3& a)
{
  const float l = std::sqrt(dot(a, a));
  return l > 1e-20f ? a * (1.0f / l) : V3{0, 0, 1};
}

struct Tri
{
  V3    p[3]; //!< camera space: x right, y up, z towards the eye
  V3    n[3];
  V3    color;
};

//! Fixed isometric camera looking from (+X, -Y, +Z) with Z up.
struct Camera
{
  V3 right, up, back;
  Camera()
  {
    back  = normalize({1.0f, -1.0f, 0.8f});
    right = normalize(cross({0, 0, 1}, back));
    up    = cross(back, right);
  }
  V3 toCam(const gp_XYZ& p) const
  {
    const V3 w{(float)p.X(), (float)p.Y(), (float)p.Z()};
    return {dot(w, right), dot(w, up), dot(w, back)};
  }
  V3 dirToCam(const gp_XYZ& d) const { return toCam(d); }
};

V3 colorOf(const Quantity_Color& theColor)
{
  double r, g, b;
  theColor.Values(r, g, b, Quantity_TOC_sRGB);
  return {(float)r, (float)g, (float)b};
}

void collectFaces(RWMesh_FaceIterator& theIter, const Camera& theCam, const V3& theDefColor, std::vector<Tri>& theTris)
{
  std::vector<V3> aPnts, aNrms;
  for (; theIter.More(); theIter.Next())
  {
    if (theIter.IsEmpty())
    {
      continue;
    }
    const V3  aColor = theIter.HasFaceColor() ? colorOf(theIter.FaceColor().GetRGB()) : theDefColor;
    const int aLo    = theIter.NodeLower();
    const int aUp    = theIter.NodeUpper();
    aPnts.resize(aUp - aLo + 1);
    aNrms.resize(aUp - aLo + 1);
    for (int i = aLo; i <= aUp; ++i)
    {
      aPnts[i - aLo] = theCam.toCam(theIter.NodeTransformed(i).XYZ());
      aNrms[i - aLo] = theCam.dirToCam(theIter.NormalTransformed(i).XYZ());
    }
    for (int e = theIter.ElemLower(); e <= theIter.ElemUpper(); ++e)
    {
      int a, b, c;
      theIter.TriangleOriented(e).Get(a, b, c);
      Tri t;
      t.p[0] = aPnts[a - aLo];
      t.p[1] = aPnts[b - aLo];
      t.p[2] = aPnts[c - aLo];
      const V3 fn = normalize(cross(t.p[1] - t.p[0], t.p[2] - t.p[0]));
      for (int k = 0; k < 3; ++k)
      {
        const int idx = (k == 0 ? a : (k == 1 ? b : c)) - aLo;
        V3        n   = aNrms[idx];
        // guard against bad / missing surface normals
        if (dot(n, fn) < 0.2f)
        {
          n = fn;
        }
        t.n[k] = n;
      }
      t.color = aColor;
      theTris.push_back(t);
    }
  }
}
//! Collects wire-frame geometry (edges that do not bound any face: 3D curves, sketches, wire bodies) as polylines.
void collectFreeCurves(const TopoDS_Shape& theShape, const Camera& theCam, std::vector<std::vector<V3>>& theLines)
{
  Bnd_Box aBox;
  BRepBndLib::Add(theShape, aBox, false);
  if (aBox.IsVoid())
  {
    return;
  }
  const double aDefl = std::max(1.0e-6, 0.0015 * std::sqrt(aBox.SquareExtent()));
  for (TopExp_Explorer anExp(theShape, TopAbs_EDGE, TopAbs_FACE); anExp.More(); anExp.Next())
  {
    try
    {
      const TopoDS_Edge& anEdge = TopoDS::Edge(anExp.Current());
      BRepAdaptor_Curve  aCurve(anEdge);
      GCPnts_TangentialDeflection aDisc(aCurve, 0.12, aDefl, 2);
      if (aDisc.NbPoints() < 2)
      {
        continue;
      }
      std::vector<V3> aLine;
      aLine.reserve(aDisc.NbPoints());
      for (int i = 1; i <= aDisc.NbPoints(); ++i)
      {
        aLine.push_back(theCam.toCam(aDisc.Value(i).XYZ()));
      }
      theLines.push_back(std::move(aLine));
    }
    catch (...)
    {
    }
  }
}
} // namespace

namespace stepcore
{

bool RenderThumbnail(const StepModel& theModel, int theSize, ThumbImage& theImage)
{
  if (theModel.IsNull() || theSize < 8)
  {
    return false;
  }
  theSize = std::min(theSize, 2048);

  const Camera   aCam;
  const V3       aDefColor{0.66f, 0.70f, 0.76f};
  std::vector<Tri> aTris;
  std::vector<std::vector<V3>> aLines;
  try
  {
    MeshShape(theModel.Shape, 0.0025, 0.35);
    if (!theModel.Doc.IsNull())
    {
      for (XCAFPrs_DocumentExplorer anExp(theModel.Doc, XCAFPrs_DocumentExplorerFlags_OnlyLeafNodes);
           anExp.More(); anExp.Next())
      {
        const XCAFPrs_DocumentNode& aNode = anExp.Current();
        RWMesh_FaceIterator aFaceIter(aNode.RefLabel, aNode.Location, true, aNode.Style);
        collectFaces(aFaceIter, aCam, aDefColor, aTris);
      }
    }
    if (aTris.empty())
    {
      RWMesh_FaceIterator aFaceIter(theModel.Shape);
      collectFaces(aFaceIter, aCam, aDefColor, aTris);
    }
    collectFreeCurves(theModel.Shape, aCam, aLines);
  }
  catch (...)
  {
    return false;
  }
  if (aTris.empty() && aLines.empty())
  {
    return false;
  }

  // ---- fit to image
  float aMin[3] = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
  float aMax[3] = {-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};
  auto addToBox = [&](const V3& p) {
    aMin[0] = std::min(aMin[0], p.x); aMax[0] = std::max(aMax[0], p.x);
    aMin[1] = std::min(aMin[1], p.y); aMax[1] = std::max(aMax[1], p.y);
    aMin[2] = std::min(aMin[2], p.z); aMax[2] = std::max(aMax[2], p.z);
  };
  for (const Tri& t : aTris)
  {
    for (const V3& p : t.p)
      addToBox(p);
  }
  for (const std::vector<V3>& aLine : aLines)
  {
    for (const V3& p : aLine)
      addToBox(p);
  }
  const int   aSS    = theSize <= 512 ? 3 : (theSize <= 1024 ? 2 : 1); // supersampling for anti-aliasing
  const int   R      = theSize * aSS;
  const float aExtent = std::max(aMax[0] - aMin[0], aMax[1] - aMin[1]);
  if (!(aExtent > 0.0f))
  {
    return false;
  }
  const float aMargin = 0.05f;
  const float aScale  = R * (1.0f - 2.0f * aMargin) / aExtent;
  const float aCx     = 0.5f * (aMin[0] + aMax[0]);
  const float aCy     = 0.5f * (aMin[1] + aMax[1]);
  const float aDiag   = std::sqrt((aMax[0] - aMin[0]) * (aMax[0] - aMin[0]) + (aMax[1] - aMin[1]) * (aMax[1] - aMin[1])
                                + (aMax[2] - aMin[2]) * (aMax[2] - aMin[2]));

  // ---- rasterize (deferred shading: store depth / normal / color)
  const size_t       aNbPix = (size_t)R * R;
  std::vector<float> aDepth(aNbPix, -std::numeric_limits<float>::max());
  std::vector<V3>    aNormal(aNbPix);
  std::vector<V3>    aColor(aNbPix);

  for (const Tri& t : aTris)
  {
    float sx[3], sy[3];
    for (int k = 0; k < 3; ++k)
    {
      sx[k] = (t.p[k].x - aCx) * aScale + 0.5f * R;
      sy[k] = 0.5f * R - (t.p[k].y - aCy) * aScale;
    }
    const float anArea = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
    if (std::abs(anArea) < 1e-9f)
    {
      continue;
    }
    const float anInv = 1.0f / anArea;
    const int   x0    = std::max(0, (int)std::floor(std::min({sx[0], sx[1], sx[2]})));
    const int   x1    = std::min(R - 1, (int)std::ceil(std::max({sx[0], sx[1], sx[2]})));
    const int   y0    = std::max(0, (int)std::floor(std::min({sy[0], sy[1], sy[2]})));
    const int   y1    = std::min(R - 1, (int)std::ceil(std::max({sy[0], sy[1], sy[2]})));
    for (int y = y0; y <= y1; ++y)
    {
      const float py = y + 0.5f;
      for (int x = x0; x <= x1; ++x)
      {
        const float px = x + 0.5f;
        const float w0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) * anInv;
        const float w1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) * anInv;
        const float w2 = 1.0f - w0 - w1;
        if (w0 < -1e-5f || w1 < -1e-5f || w2 < -1e-5f)
        {
          continue;
        }
        const float  z   = w0 * t.p[0].z + w1 * t.p[1].z + w2 * t.p[2].z;
        const size_t idx = (size_t)y * R + x;
        if (z <= aDepth[idx])
        {
          continue;
        }
        aDepth[idx]  = z;
        aNormal[idx] = normalize(t.n[0] * w0 + t.n[1] * w1 + t.n[2] * w2);
        aColor[idx]  = t.color;
      }
    }
  }

  // ---- shade + edge detection
  const V3    aL1      = normalize({-0.45f, 0.60f, 0.70f});
  const V3    aL2      = normalize({0.70f, -0.10f, 0.35f});
  const V3    aH1      = normalize(aL1 + V3{0, 0, 1});
  const float aDepthTh = 0.02f * aDiag + 4.0f / aScale;
  const float NONE     = -std::numeric_limits<float>::max();

  std::vector<float> aRGBA(aNbPix * 4, 0.0f);
  for (int y = 0; y < R; ++y)
  {
    for (int x = 0; x < R; ++x)
    {
      const size_t idx = (size_t)y * R + x;
      if (aDepth[idx] == NONE)
      {
        continue;
      }
      V3 n = aNormal[idx];
      if (n.z < 0.0f)
      {
        n = n * -1.0f; // two-sided lighting
      }
      const float aDiff = 0.60f * std::max(0.0f, dot(n, aL1)) + 0.18f * std::max(0.0f, dot(n, aL2)) + 0.22f * n.z;
      const float aSpec = 0.20f * std::pow(std::max(0.0f, dot(n, aH1)), 40.0f);
      const V3&   c     = aColor[idx];
      float       aLum  = 0.27f + aDiff;

      // outline: silhouette, depth discontinuities and sharp creases
      float anEdge = 1.0f;
      const int    aNx[4] = {1, -1, 0, 0};
      const int    aNy[4] = {0, 0, 1, -1};
      for (int k = 0; k < 4; ++k)
      {
        const int nx = x + aNx[k], ny = y + aNy[k];
        if (nx < 0 || ny < 0 || nx >= R || ny >= R)
        {
          continue;
        }
        const size_t nidx = (size_t)ny * R + nx;
        if (aDepth[nidx] == NONE)
        {
          anEdge = std::min(anEdge, 0.35f);
        }
        else if (aDepth[idx] - aDepth[nidx] < -aDepthTh)
        {
          anEdge = std::min(anEdge, 0.35f); // neighbour is in front: we are behind an edge
        }
        else if (dot(aNormal[idx], aNormal[nidx]) < 0.80f && aDepth[idx] >= aDepth[nidx])
        {
          anEdge = std::min(anEdge, 0.55f);
        }
      }
      float* o = &aRGBA[idx * 4];
      o[0]     = std::min(1.0f, (c.x * aLum + aSpec) * anEdge);
      o[1]     = std::min(1.0f, (c.y * aLum + aSpec) * anEdge);
      o[2]     = std::min(1.0f, (c.z * aLum + aSpec) * anEdge);
      o[3]     = 1.0f;
    }
  }

  // ---- wire-frame curves: thick anti-aliased (via supersampling) lines, hidden where a surface is clearly in front
  if (!aLines.empty())
  {
    const V3    aLineColor = aTris.empty() ? V3{0.16f, 0.42f, 0.86f} : V3{0.10f, 0.25f, 0.55f};
    const float aRad       = (aTris.empty() ? 1.05f : 0.75f) * aSS; // line half-width in supersampled pixels
    const int   aRadI      = (int)std::ceil(aRad);
    auto stamp = [&](float px, float py, float pz) {
      const int cx = (int)std::floor(px), cy = (int)std::floor(py);
      for (int y = cy - aRadI; y <= cy + aRadI; ++y)
      {
        if (y < 0 || y >= R)
          continue;
        for (int x = cx - aRadI; x <= cx + aRadI; ++x)
        {
          if (x < 0 || x >= R)
            continue;
          const float dx = x + 0.5f - px, dy = y + 0.5f - py;
          if (dx * dx + dy * dy > aRad * aRad)
            continue;
          const size_t idx = (size_t)y * R + x;
          if (aDepth[idx] != NONE && pz < aDepth[idx] - 0.25f * aDepthTh)
            continue; // behind a surface
          float* o = &aRGBA[idx * 4];
          o[0] = aLineColor.x;
          o[1] = aLineColor.y;
          o[2] = aLineColor.z;
          o[3] = 1.0f;
        }
      }
    };
    for (const std::vector<V3>& aLine : aLines)
    {
      for (size_t i = 0; i + 1 < aLine.size(); ++i)
      {
        const float x0 = (aLine[i].x - aCx) * aScale + 0.5f * R, y0 = 0.5f * R - (aLine[i].y - aCy) * aScale;
        const float x1 = (aLine[i + 1].x - aCx) * aScale + 0.5f * R, y1 = 0.5f * R - (aLine[i + 1].y - aCy) * aScale;
        const float aLen   = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
        const int   aSteps = std::max(1, (int)std::ceil(aLen / 0.5f));
        for (int k = 0; k <= aSteps; ++k)
        {
          const float t = (float)k / aSteps;
          stamp(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, aLine[i].z + (aLine[i + 1].z - aLine[i].z) * t);
        }
      }
    }
  }

  // ---- downsample to premultiplied BGRA
  theImage.Width  = theSize;
  theImage.Height = theSize;
  theImage.Pixels.assign((size_t)theSize * theSize * 4, 0);
  const float anInvN = 1.0f / (aSS * aSS);
  for (int y = 0; y < theSize; ++y)
  {
    for (int x = 0; x < theSize; ++x)
    {
      float r = 0, g = 0, b = 0, a = 0;
      for (int sy = 0; sy < aSS; ++sy)
      {
        for (int sx = 0; sx < aSS; ++sx)
        {
          const float* s = &aRGBA[(((size_t)(y * aSS + sy)) * R + (x * aSS + sx)) * 4];
          r += s[0] * s[3];
          g += s[1] * s[3];
          b += s[2] * s[3];
          a += s[3];
        }
      }
      uint8_t* d = &theImage.Pixels[((size_t)y * theSize + x) * 4];
      d[0]       = (uint8_t)std::lround(std::min(1.0f, b * anInvN) * 255.0f);
      d[1]       = (uint8_t)std::lround(std::min(1.0f, g * anInvN) * 255.0f);
      d[2]       = (uint8_t)std::lround(std::min(1.0f, r * anInvN) * 255.0f);
      d[3]       = (uint8_t)std::lround(std::min(1.0f, a * anInvN) * 255.0f);
    }
  }
  return true;
}

} // namespace stepcore
