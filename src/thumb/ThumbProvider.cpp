// Windows Explorer thumbnail handler for STEP files (.step / .stp).
// In-process COM server implementing IInitializeWithStream + IThumbnailProvider.
// Explorer hosts it in an isolated surrogate process (dllhost.exe), so a crash on a bad file cannot take Explorer down.

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <thumbcache.h>

#include <atomic>
#include <new>
#include <sstream>
#include <string>

#include "StepModel.h"
#include "ThumbRenderer.h"

namespace
{
// {7B1E6A52-3C1F-4F0B-9D2E-5A4C8E61B0D3}
const CLSID   CLSID_StepThumbProvider = {0x7b1e6a52, 0x3c1f, 0x4f0b, {0x9d, 0x2e, 0x5a, 0x4c, 0x8e, 0x61, 0xb0, 0xd3}};
const wchar_t kClsidStr[]             = L"{7B1E6A52-3C1F-4F0B-9D2E-5A4C8E61B0D3}";
const wchar_t kThumbHandlerKey[]      = L"{e357fccd-a995-4576-b01f-234630154e96}";
const ULONGLONG kMaxFileBytes         = 512ull * 1024 * 1024; // skip absurdly large files

HMODULE           g_hModule = nullptr;
std::atomic<long> g_nbObjects{0};
std::atomic<long> g_nbLocks{0};

class StepThumbProvider final : public IInitializeWithStream, public IThumbnailProvider
{
public:
  StepThumbProvider() { ++g_nbObjects; }

  // IUnknown
  IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override
  {
    static const QITAB qit[] = {
      QITABENT(StepThumbProvider, IInitializeWithStream),
      QITABENT(StepThumbProvider, IThumbnailProvider),
      {nullptr, 0},
    };
    return QISearch(this, qit, riid, ppv);
  }
  IFACEMETHODIMP_(ULONG) AddRef() override { return ++myRef; }
  IFACEMETHODIMP_(ULONG) Release() override
  {
    const long aRef = --myRef;
    if (aRef == 0)
    {
      delete this;
    }
    return aRef;
  }

  // IInitializeWithStream
  IFACEMETHODIMP Initialize(IStream* theStream, DWORD) override
  {
    if (myStream != nullptr)
    {
      return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
    }
    return theStream->QueryInterface(IID_PPV_ARGS(&myStream));
  }

  // IThumbnailProvider
  IFACEMETHODIMP GetThumbnail(UINT theSize, HBITMAP* theBitmap, WTS_ALPHATYPE* theAlpha) override
  {
    if (myStream == nullptr || theBitmap == nullptr || theAlpha == nullptr)
    {
      return E_INVALIDARG;
    }
    *theBitmap = nullptr;

    std::string aData;
    HRESULT     hr = readAll(aData);
    if (FAILED(hr))
    {
      return hr;
    }

    ThumbImage anImage;
    try
    {
      StepModel aModel;
      {
        std::istringstream aStream(std::move(aData));
        if (!stepcore::LoadStepStream(aStream, aModel))
        {
          stepcore::ReleaseModel(aModel);
          return E_FAIL;
        }
      }
      const bool isOk = stepcore::RenderThumbnail(aModel, (int)theSize, anImage);
      stepcore::ReleaseModel(aModel);
      if (!isOk)
      {
        return E_FAIL;
      }
    }
    catch (...)
    {
      return E_FAIL;
    }

    BITMAPINFO aBmi              = {};
    aBmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    aBmi.bmiHeader.biWidth       = anImage.Width;
    aBmi.bmiHeader.biHeight      = -anImage.Height; // top-down
    aBmi.bmiHeader.biPlanes      = 1;
    aBmi.bmiHeader.biBitCount    = 32;
    aBmi.bmiHeader.biCompression = BI_RGB;
    void*   aBits                = nullptr;
    HBITMAP aBmp                 = CreateDIBSection(nullptr, &aBmi, DIB_RGB_COLORS, &aBits, nullptr, 0);
    if (aBmp == nullptr)
    {
      return E_OUTOFMEMORY;
    }
    memcpy(aBits, anImage.Pixels.data(), anImage.Pixels.size());
    *theBitmap = aBmp;
    *theAlpha  = WTSAT_ARGB;
    return S_OK;
  }

private:
  ~StepThumbProvider()
  {
    if (myStream != nullptr)
    {
      myStream->Release();
    }
    --g_nbObjects;
  }

  HRESULT readAll(std::string& theData)
  {
    STATSTG aStat = {};
    if (SUCCEEDED(myStream->Stat(&aStat, STATFLAG_NONAME)))
    {
      if (aStat.cbSize.QuadPart > kMaxFileBytes)
      {
        return E_FAIL;
      }
      theData.reserve((size_t)aStat.cbSize.QuadPart);
    }
    LARGE_INTEGER aZero = {};
    myStream->Seek(aZero, STREAM_SEEK_SET, nullptr);
    char aBuf[1 << 16];
    for (;;)
    {
      ULONG   aRead = 0;
      HRESULT hr    = myStream->Read(aBuf, sizeof(aBuf), &aRead);
      if (FAILED(hr))
      {
        return hr;
      }
      theData.append(aBuf, aRead);
      if (theData.size() > kMaxFileBytes)
      {
        return E_FAIL;
      }
      if (hr == S_FALSE || aRead == 0)
      {
        break;
      }
    }
    return theData.empty() ? E_FAIL : S_OK;
  }

private:
  std::atomic<long> myRef{1};
  IStream*          myStream = nullptr;
};

class ClassFactory final : public IClassFactory
{
public:
  IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override
  {
    static const QITAB qit[] = {QITABENT(ClassFactory, IClassFactory), {nullptr, 0}};
    return QISearch(this, qit, riid, ppv);
  }
  IFACEMETHODIMP_(ULONG) AddRef() override { return 2; }
  IFACEMETHODIMP_(ULONG) Release() override { return 1; }

  IFACEMETHODIMP CreateInstance(IUnknown* theOuter, REFIID riid, void** ppv) override
  {
    *ppv = nullptr;
    if (theOuter != nullptr)
    {
      return CLASS_E_NOAGGREGATION;
    }
    StepThumbProvider* aProv = new (std::nothrow) StepThumbProvider();
    if (aProv == nullptr)
    {
      return E_OUTOFMEMORY;
    }
    HRESULT hr = aProv->QueryInterface(riid, ppv);
    aProv->Release();
    return hr;
  }
  IFACEMETHODIMP LockServer(BOOL theLock) override
  {
    theLock ? ++g_nbLocks : --g_nbLocks;
    return S_OK;
  }
};

ClassFactory g_factory;

// ---- self registration (per-user, HKCU) - used for development; the installer writes the same keys.
HRESULT setRegString(HKEY theRoot, const std::wstring& theKey, const wchar_t* theName, const std::wstring& theValue)
{
  HKEY    aKey = nullptr;
  LSTATUS st   = RegCreateKeyExW(theRoot, theKey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &aKey, nullptr);
  if (st == ERROR_SUCCESS)
  {
    st = RegSetValueExW(aKey, theName, 0, REG_SZ, (const BYTE*)theValue.c_str(), (DWORD)((theValue.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(aKey);
  }
  return HRESULT_FROM_WIN32(st);
}

} // namespace

BOOL APIENTRY DllMain(HMODULE theModule, DWORD theReason, LPVOID)
{
  if (theReason == DLL_PROCESS_ATTACH)
  {
    g_hModule = theModule;
    DisableThreadLibraryCalls(theModule);
  }
  return TRUE;
}

STDAPI DllGetClassObject(REFCLSID theClsid, REFIID riid, void** ppv)
{
  if (!IsEqualCLSID(theClsid, CLSID_StepThumbProvider))
  {
    return CLASS_E_CLASSNOTAVAILABLE;
  }
  return g_factory.QueryInterface(riid, ppv);
}

STDAPI DllCanUnloadNow()
{
  return (g_nbObjects == 0 && g_nbLocks == 0) ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer()
{
  wchar_t aPath[MAX_PATH] = {};
  GetModuleFileNameW(g_hModule, aPath, MAX_PATH);

  const std::wstring aClsidKey = std::wstring(L"Software\\Classes\\CLSID\\") + kClsidStr;
  HRESULT            hr        = setRegString(HKEY_CURRENT_USER, aClsidKey, nullptr, L"STEP Thumbnail Provider");
  if (SUCCEEDED(hr))
    hr = setRegString(HKEY_CURRENT_USER, aClsidKey + L"\\InprocServer32", nullptr, aPath);
  if (SUCCEEDED(hr))
    hr = setRegString(HKEY_CURRENT_USER, aClsidKey + L"\\InprocServer32", L"ThreadingModel", L"Apartment");
  for (const wchar_t* anExt : {L".step", L".stp"})
  {
    if (SUCCEEDED(hr))
      hr = setRegString(HKEY_CURRENT_USER, std::wstring(L"Software\\Classes\\") + anExt + L"\\ShellEx\\" + kThumbHandlerKey, nullptr, kClsidStr);
    if (SUCCEEDED(hr))
      hr = setRegString(HKEY_CURRENT_USER, std::wstring(L"Software\\Classes\\SystemFileAssociations\\") + anExt + L"\\ShellEx\\" + kThumbHandlerKey, nullptr, kClsidStr);
  }
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  return hr;
}

STDAPI DllUnregisterServer()
{
  RegDeleteTreeW(HKEY_CURRENT_USER, (std::wstring(L"Software\\Classes\\CLSID\\") + kClsidStr).c_str());
  for (const wchar_t* anExt : {L".step", L".stp"})
  {
    RegDeleteTreeW(HKEY_CURRENT_USER, (std::wstring(L"Software\\Classes\\") + anExt + L"\\ShellEx\\" + kThumbHandlerKey).c_str());
    RegDeleteTreeW(HKEY_CURRENT_USER, (std::wstring(L"Software\\Classes\\SystemFileAssociations\\") + anExt + L"\\ShellEx\\" + kThumbHandlerKey).c_str());
  }
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  return S_OK;
}

//! Test helper (called via ctypes from scripts/test_thumb.py): renders a STEP file to a 32-bit BMP.
extern "C" __declspec(dllexport) int RenderStepToBmp(const wchar_t* theIn, const wchar_t* theOut, int theSize)
{
  char aUtf8[4 * MAX_PATH] = {};
  WideCharToMultiByte(CP_UTF8, 0, theIn, -1, aUtf8, sizeof(aUtf8), nullptr, nullptr);
  StepModel aModel;
  if (!stepcore::LoadStepFile(aUtf8, aModel))
  {
    return 1;
  }
  ThumbImage anImg;
  const bool isOk = stepcore::RenderThumbnail(aModel, theSize, anImg);
  stepcore::ReleaseModel(aModel);
  if (!isOk)
  {
    return 2;
  }
  BITMAPFILEHEADER aFh = {};
  BITMAPV5HEADER   aIh = {};
  aIh.bV5Size          = sizeof(aIh);
  aIh.bV5Width         = anImg.Width;
  aIh.bV5Height        = -anImg.Height;
  aIh.bV5Planes        = 1;
  aIh.bV5BitCount      = 32;
  aIh.bV5Compression   = BI_BITFIELDS;
  aIh.bV5RedMask       = 0x00FF0000;
  aIh.bV5GreenMask     = 0x0000FF00;
  aIh.bV5BlueMask      = 0x000000FF;
  aIh.bV5AlphaMask     = 0xFF000000;
  aIh.bV5CSType        = LCS_sRGB;
  aFh.bfType           = 0x4D42;
  aFh.bfOffBits        = sizeof(aFh) + sizeof(aIh);
  aFh.bfSize           = aFh.bfOffBits + (DWORD)anImg.Pixels.size();
  HANDLE aFile = CreateFileW(theOut, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (aFile == INVALID_HANDLE_VALUE)
  {
    return 3;
  }
  DWORD aWritten = 0;
  WriteFile(aFile, &aFh, sizeof(aFh), &aWritten, nullptr);
  WriteFile(aFile, &aIh, sizeof(aIh), &aWritten, nullptr);
  WriteFile(aFile, anImg.Pixels.data(), (DWORD)anImg.Pixels.size(), &aWritten, nullptr);
  CloseHandle(aFile);
  return 0;
}
