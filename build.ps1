# Build the engine. Enters the Visual Studio environment automatically.
#   .\build.ps1                 # debug
#   .\build.ps1 -Config release -Run
#   .\build.ps1 -Clean -Run

[CmdletBinding()]
param(
    [ValidateSet('debug', 'release')]
    [string]$Config = 'debug',
    [switch]$Clean,
    [switch]$Run,
    [switch]$Reconfigure
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "vswhere not found. Run .\setup.ps1 first."
}

$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) {
    throw "MSVC C++ tools missing. Run .\setup.ps1 (elevated) to add the NativeDesktop workload."
}

$devcmd = Join-Path $vsPath 'Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path $devcmd)) { throw "VsDevCmd.bat not found at $devcmd" }

$buildDir = Join-Path $root "build\$Config"
if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "removing $buildDir" -ForegroundColor Yellow
    Remove-Item $buildDir -Recurse -Force
}

$sw = [System.Diagnostics.Stopwatch]::StartNew()

$cmakeCmd = "cmake --preset $Config"
if (-not (Test-Path (Join-Path $buildDir 'build.ninja'))) { $cmakeCmd = "cmake --preset $Config" }

$cmd = "call `"$devcmd`" -arch=x64 -host_arch=x64 -no_logo && $cmakeCmd && cmake --build --preset $Config"
cmd /c $cmd
if ($LASTEXITCODE -ne 0) { throw "build failed (exit $LASTEXITCODE)" }

$sw.Stop()
$exe = Join-Path $buildDir 'horror.exe'
Write-Host ("`nbuilt in {0:N1}s -> {1}" -f $sw.Elapsed.TotalSeconds, $exe) -ForegroundColor Green

if ($Run) {
    if (-not (Test-Path $exe)) { throw "executable not found: $exe" }
    Push-Location $buildDir
    try { & $exe } finally { Pop-Location }
}
