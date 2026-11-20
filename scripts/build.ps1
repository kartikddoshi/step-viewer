# Builds STEP Viewer (Release) with MSVC + Ninja.
# Usage:  powershell -ExecutionPolicy Bypass -File scripts\build.ps1 [-Clean] [-Installer]
param(
  [switch]$Clean,
  [switch]$Installer
)
$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'

# --- locate tools
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath  = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'Visual Studio C++ Build Tools not found.' }
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) { $cmake = (python -c "import cmake; print(cmake.CMAKE_BIN_DIR)") + '\cmake.exe' }
$ninja = (Get-Command ninja -ErrorAction SilentlyContinue).Source
if (-not $ninja) { $ninja = (python -c "import ninja,os; print(os.path.join(ninja.BIN_DIR,'ninja.exe'))") }

# --- import the MSVC developer environment into this PowerShell session
cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
  if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2]) }
}

if ($Clean -and (Test-Path $build)) { Remove-Item -Recurse -Force $build }

& $cmake -S $root -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_MAKE_PROGRAM=$ninja"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
& $cmake --build $build --config Release
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }

# --- MSVC runtime next to the binaries (app-local deployment)
$redist = Get-ChildItem (Join-Path $vsPath 'VC\Redist\MSVC') -Directory | Where-Object { $_.Name -match '^\d' } |
          Sort-Object Name -Descending | Select-Object -First 1
$crt = Get-ChildItem (Join-Path $redist.FullName 'x64') -Directory -Filter 'Microsoft.VC*.CRT' | Select-Object -First 1
Copy-Item (Join-Path $crt.FullName '*.dll') (Join-Path $build 'out') -Force

Write-Host "`nBuild OK -> $build\out\StepViewer.exe" -ForegroundColor Green

if ($Installer) {
  & (Join-Path $PSScriptRoot 'make-installer.ps1')
}
