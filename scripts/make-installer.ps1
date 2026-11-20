# Collects license files and compiles the Inno Setup installer into dist\.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$lic  = Join-Path $root 'build\licenses'
$occt = Join-Path $root 'deps\occt\opencascade-8.0.1-vc14-64'
$tp   = Join-Path $root 'deps\occt\3rdparty-vc14-64'

New-Item -ItemType Directory -Force $lic | Out-Null
Copy-Item (Join-Path $root 'LICENSE') "$lic\StepViewer-LICENSE.txt" -Force
Copy-Item "$occt\LICENSE_LGPL_21.txt"     "$lic\OpenCASCADE-LGPL-2.1.txt" -Force
Copy-Item "$occt\OCCT_LGPL_EXCEPTION.txt" "$lic\OpenCASCADE-LGPL-exception.txt" -Force
Copy-Item (Get-ChildItem "$tp\freetype-*\LICENSE.TXT").FullName "$lic\FreeType-LICENSE.txt" -Force
Copy-Item (Get-ChildItem "$tp\tbb-*\LICENSE.txt").FullName      "$lic\oneTBB-LICENSE.txt" -Force
Copy-Item (Get-ChildItem "$tp\freeimage-*\license-fi.txt").FullName "$lic\FreeImage-LICENSE.txt" -Force
foreach ($f in @('lgpl-3.0.txt', 'gpl-3.0.txt')) {
  $dst = Join-Path $lic "Qt-$f"
  if (-not (Test-Path $dst)) { curl.exe -sSL -o $dst "https://www.gnu.org/licenses/$f" }
}
@"
STEP Viewer uses the following open-source components, dynamically linked:

  Open CASCADE Technology 8.0.1  - LGPL 2.1 with OCCT exception   https://dev.opencascade.org
  Qt 6.8 (Qt Core/Gui/Widgets)   - LGPL 3.0                        https://www.qt.io  (source: https://download.qt.io)
  FreeType                       - FreeType License
  oneTBB                         - Apache 2.0
  FreeImage                      - FreeImage Public License
  jemalloc                       - BSD 2-clause
  FFmpeg 3.3 (avcodec/avformat/avutil/swscale) - LGPL 2.1+   https://ffmpeg.org (linked by OCCT, unused)
  OpenVR                         - BSD 3-clause (linked by OCCT, unused)

You may replace the LGPL libraries (TK*.dll, Qt6*.dll) with your own compatible builds.
"@ | Set-Content -Encoding utf8 (Join-Path $lic 'THIRD-PARTY-NOTICES.txt')

$iscc = Join-Path $root 'deps\inno\ISCC.exe'
& $iscc (Join-Path $root 'installer\StepViewer.iss')
if ($LASTEXITCODE -ne 0) { throw 'Installer build failed' }
Get-ChildItem (Join-Path $root 'dist')
