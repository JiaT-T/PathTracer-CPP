# Build PathTracer-CPP (Release x64 by default) with the newest installed Visual Studio.
# Usage: powershell -ExecutionPolicy Bypass -File tools/build.ps1 [-Configuration Release|Debug]
param(
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found; install Visual Studio 2022 or newer." }

$msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\amd64\MSBuild.exe" | Select-Object -First 1
if (-not $msbuild) { throw "MSBuild not found." }

$project = Join-Path $PSScriptRoot "..\PathTracer-CPP\PathTracer-CPP.vcxproj"
& $msbuild $project /p:Configuration=$Configuration /p:Platform=x64 /m /nologo /v:minimal
exit $LASTEXITCODE
