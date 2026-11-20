# Downloads the third-party SDKs needed to build STEP Viewer into deps\ (about 1.5 GB on disk).
#
#   OpenCASCADE 8.0.1 prebuilt (MSVC x64)  -> deps\occt\opencascade-8.0.1-vc14-64, deps\occt\3rdparty-vc14-64
#   Qt 6.8.3 msvc2022_64 (via aqtinstall)  -> deps\qt\6.8.3\msvc2022_64
#   Inno Setup 6.7.4 (installer compiler)  -> deps\inno            (skip with -NoInstaller)
#
# Usage:  powershell -ExecutionPolicy Bypass -File scripts\setup-deps.ps1 [-NoInstaller] [-DepsDir <path>]
# Requires: Python 3 on PATH (for aqtinstall, cmake and ninja).
param(
  [switch]$NoInstaller,
  [string]$DepsDir = (Join-Path (Split-Path -Parent $PSScriptRoot) 'deps')
)
$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue' # Invoke-WebRequest is very slow with the progress bar
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$OcctUrl = 'https://github.com/Open-Cascade-SAS/OCCT/releases/download/V8.0.1/occt-combined-release-no-pch.zip'
$InnoUrl = 'https://github.com/jrsoftware/issrc/releases/download/is-6_7_4/innosetup-6.7.4.exe'

New-Item -ItemType Directory -Force $DepsDir | Out-Null
$dl = Join-Path $DepsDir 'dl'
New-Item -ItemType Directory -Force $dl | Out-Null

function Get-File([string]$Url, [string]$Out) {
  if (-not (Test-Path $Out)) {
    Write-Host "Downloading $Url"
    Invoke-WebRequest -Uri $Url -OutFile "$Out.part" -UseBasicParsing
    Move-Item "$Out.part" $Out
  }
}

# ---- Python tools: cmake, ninja, aqtinstall
Write-Host '== Python build tools (cmake, ninja, aqtinstall)'
python -m pip install --quiet --upgrade cmake ninja aqtinstall
if ($LASTEXITCODE -ne 0) { throw 'pip install failed - is Python 3 on PATH?' }

# ---- OpenCASCADE
$occtDir = Join-Path $DepsDir 'occt'
if (-not (Test-Path (Join-Path $occtDir 'opencascade-8.0.1-vc14-64\cmake'))) {
  Write-Host '== OpenCASCADE 8.0.1'
  $zip = Join-Path $dl 'occt-combined-release-no-pch.zip'
  Get-File $OcctUrl $zip
  $tmp = Join-Path $dl 'occt-extract'
  if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
  New-Item -ItemType Directory -Force $tmp | Out-Null
  # tar.exe (built into Windows 10/11) is much faster and more robust than Expand-Archive
  tar -xf $zip -C $tmp
  if ($LASTEXITCODE -ne 0) { throw "Cannot extract $zip" }
  # the release asset is a zip that contains another zip (opencascade-8.0.1-vc14-64-combined.zip)
  foreach ($inner in @(Get-ChildItem $tmp -Recurse -Filter *.zip)) {
    tar -xf $inner.FullName -C $tmp
    if ($LASTEXITCODE -ne 0) { throw "Cannot extract $($inner.Name)" }
    Remove-Item $inner.FullName
  }
  New-Item -ItemType Directory -Force $occtDir | Out-Null
  # the archive layout has changed between releases: find the two folders wherever they are
  foreach ($name in 'opencascade-8.0.1-vc14-64', '3rdparty-vc14-64') {
    $src = Get-ChildItem $tmp -Recurse -Directory -Filter $name | Select-Object -First 1
    if (-not $src) { throw "Folder $name not found in $zip" }
    $dst = Join-Path $occtDir $name
    if (Test-Path $dst) { Remove-Item -Recurse -Force $dst }
    Move-Item $src.FullName $dst
  }
  Remove-Item -Recurse -Force $tmp
} else { Write-Host '== OpenCASCADE: already present' }

# ---- Qt
if (-not (Test-Path (Join-Path $DepsDir 'qt\6.8.3\msvc2022_64\bin\windeployqt.exe'))) {
  Write-Host '== Qt 6.8.3 (msvc2022_64)'
  Push-Location $DepsDir # aqtinstall writes aqtinstall.log into the current directory
  try {
    python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O (Join-Path $DepsDir 'qt')
    if ($LASTEXITCODE -ne 0) { throw 'Qt download failed' }
  } finally { Pop-Location }
} else { Write-Host '== Qt: already present' }

# ---- Inno Setup (only for building the installer)
if (-not $NoInstaller) {
  $innoDir = Join-Path $DepsDir 'inno'
  if (-not (Test-Path (Join-Path $innoDir 'ISCC.exe'))) {
    Write-Host '== Inno Setup 6.7.4'
    $exe = Join-Path $dl 'innosetup-6.7.4.exe'
    Get-File $InnoUrl $exe
    $sig = Get-AuthenticodeSignature $exe
    if ($sig.Status -ne 'Valid') { throw "Inno Setup download has no valid signature ($($sig.Status))" }
    $p = Start-Process $exe -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER', '/NOICONS',
                                         "/DIR=$innoDir" -Wait -PassThru
    if ($p.ExitCode -ne 0) { throw "Inno Setup install failed ($($p.ExitCode))" }
  } else { Write-Host '== Inno Setup: already present' }
}

Write-Host "`nDependencies ready in $DepsDir" -ForegroundColor Green
Write-Host 'Next:  powershell -ExecutionPolicy Bypass -File scripts\build.ps1'
