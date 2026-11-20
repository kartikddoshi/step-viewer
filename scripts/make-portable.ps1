# Packs build\out (plus license texts) into dist\StepViewer-<version>-portable.zip.
# Run after scripts\build.ps1 and scripts\make-installer.ps1 (which collects the license texts).
param([string]$Version = '1.0.0')
$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$name  = "StepViewer-$Version-portable"
$stage = Join-Path $root "build\$name"
$app   = Join-Path $stage 'StepViewer'

if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $app | Out-Null
Copy-Item (Join-Path $root 'build\out\*') $app -Recurse
Copy-Item (Join-Path $root 'build\licenses') (Join-Path $app 'licenses') -Recurse
@"
STEP Viewer $Version - portable
https://github.com/kartikddoshi/step-viewer

Run StepViewer.exe. No installation needed. You can also drag a .step/.stp file onto it.

Explorer thumbnails are optional in the portable version. To turn them on for your user account,
open a Command Prompt in this folder and run:
    regsvr32 StepThumb.dll
To turn them off again (do this before deleting the folder):
    regsvr32 /u StepThumb.dll
For file associations and thumbnails set up automatically, use StepViewer-Setup-$Version.exe instead.

Mouse: left drag rotate, middle/right drag pan, wheel zoom, click select, Shift+click a second item.
Keys:  R ruler (click two points), F fit, 0 iso, 1-6 views, X x-ray, K keep dimension, Esc clear.
"@ | Set-Content -Encoding utf8 (Join-Path $app 'README.txt')

New-Item -ItemType Directory -Force (Join-Path $root 'dist') | Out-Null
$zip = Join-Path $root "dist\$name.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path $app -DestinationPath $zip -CompressionLevel Optimal
Remove-Item -Recurse -Force $stage
Get-Item $zip
