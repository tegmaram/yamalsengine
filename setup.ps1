# Bootstrap script for the horror engine toolchain.
# Idempotent. Run from an elevated PowerShell:
#   Set-ExecutionPolicy -Scope Process Bypass -Force; .\setup.ps1
#
# Installs: MSVC C++ workload, CMake, Ninja, Git, LLVM/Clang, Vulkan SDK.

[CmdletBinding()]
param(
    [switch]$SkipWinget,
    [switch]$CheckOnly
)

$ErrorActionPreference = 'Stop'

function Info($msg)  { Write-Host "  $msg" -ForegroundColor Gray }
function Ok($msg)    { Write-Host "  [ok] $msg" -ForegroundColor Green }
function Warn($msg)  { Write-Host "  [!]  $msg" -ForegroundColor Yellow }
function Fail($msg)  { Write-Host "  [x]  $msg" -ForegroundColor Red }

function Refresh-Path {
    $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' +
                [Environment]::GetEnvironmentVariable('Path', 'User')
}

function Test-Cmd($name) { return [bool](Get-Command $name -ErrorAction SilentlyContinue) }

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)

Write-Host "`n=== horror engine setup ===" -ForegroundColor Cyan
if (-not $isAdmin) { Warn "not elevated - VS build tools install will prompt for UAC" }

# ---------------------------------------------------------------- 1. winget packages
function Install-Winget($id, $label) {
    if ($CheckOnly) { Info "$label ($id) - check only"; return }
    Write-Host "  installing $label ..." -ForegroundColor White
    winget install --id $id --exact --silent --accept-package-agreements --accept-source-agreements --disable-interactivity
    if ($LASTEXITCODE -ne 0) { Warn "$label install returned $LASTEXITCODE (may already be installed)" }
    else { Ok "$label" }
}

if (-not $SkipWinget) {
    if (-not (Test-Cmd 'winget')) {
        Fail 'winget not found. Install App Installer from the Microsoft Store, then re-run.'
    } else {
        Write-Host "`n[1/4] base tools" -ForegroundColor Cyan
        if (-not (Test-Cmd 'cmake')) { Install-Winget 'Kitware.CMake' 'CMake' }
        else { Ok "cmake $((cmake --version | Select-Object -First 1) -replace 'cmake version ','')" }

        if (-not (Test-Cmd 'ninja')) { Install-Winget 'Ninja-build.Ninja' 'Ninja' }
        else { Ok "ninja present" }

        if (-not (Test-Cmd 'git')) { Install-Winget 'Git.Git' 'Git' } else { Ok 'git present' }

        if (-not (Test-Cmd 'clang-cl')) { Install-Winget 'LLVM.LLVM' 'LLVM / Clang' }
        else { Ok 'clang-cl present' }

        # Vulkan SDK: winget has no reliable id for it, use LunarG's direct installer.
        $vkOk = (Test-Path "$env:VULKAN_SDK\Bin\glslc.exe") -or (Test-Path 'C:\VulkanSDK')
        if (-not $vkOk) {
            if ($CheckOnly) {
                Info 'Vulkan SDK missing (https://vulkan.lunarg.com/sdk/home#windows)'
            } else {
                Write-Host '  downloading Vulkan SDK ...' -ForegroundColor White
                $vkExe = Join-Path $env:TEMP 'vulkan_sdk.exe'
                try {
                    Invoke-WebRequest -Uri 'https://sdk.lunarg.com/sdk/download/latest/windows/vulkan_sdk.exe' `
                        -OutFile $vkExe -UseBasicParsing
                    Start-Process -FilePath $vkExe -ArgumentList '/S' -Wait
                    $latest = Get-ChildItem 'C:\VulkanSDK' -Directory | Sort-Object Name -Descending |
                              Select-Object -First 1
                    if ($latest) { $env:VULKAN_SDK = $latest.FullName; Ok "Vulkan SDK $($latest.Name)" }
                } catch {
                    Fail "Vulkan SDK download failed: $($_.Exception.Message)"
                }
            }
        } else { Ok "Vulkan SDK present ($env:VULKAN_SDK)" }
        Refresh-Path
    }
}

# ---------------------------------------------------------------- 2. MSVC C++ workload
Write-Host "`n[2/4] MSVC C++ toolchain" -ForegroundColor Cyan
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    Fail 'vswhere not found - install Visual Studio Build Tools first: https://aka.ms/vs/stable/vs_buildtools.exe'
} else {
    $withCpp = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($withCpp) {
        Ok "MSVC found: $withCpp"
    } else {
        $any = & $vswhere -latest -products * -property installationPath
        if ($any -and -not $CheckOnly) {
            Warn "VS present without C++ tools, adding NativeDesktop workload to $any"
            $setup = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\setup.exe"
            $args = @('modify', '--installPath', $any,
                      '--add', 'Microsoft.VisualStudio.Workload.NativeDesktop',
                      '--includeRecommended', '--passive', '--norestart')
            Start-Process -FilePath $setup -ArgumentList $args -Verb RunAs -Wait
        } elseif (-not $CheckOnly) {
            Warn 'no Visual Studio found, installing Build Tools'
            Install-Winget 'Microsoft.VisualStudio.2022.BuildTools' 'VS Build Tools'
        }
    }
}

# ---------------------------------------------------------------- 3. Vulkan SDK check
Write-Host "`n[3/4] Vulkan SDK" -ForegroundColor Cyan
$sdk = $env:VULKAN_SDK
if (-not $sdk) {
    $candidate = Get-ChildItem 'C:\VulkanSDK' -Directory -ErrorAction SilentlyContinue |
                 Sort-Object Name -Descending | Select-Object -First 1
    if ($candidate) { $sdk = $candidate.FullName }
}
if ($sdk -and (Test-Path "$sdk\Bin\glslc.exe")) {
    Ok "Vulkan SDK: $sdk"
} else {
    Fail 'Vulkan SDK not found. Download from https://vulkan.lunarg.com/sdk/home#windows (1.4.x)'
}

# ---------------------------------------------------------------- 4. verify
Write-Host "`n[4/4] verification" -ForegroundColor Cyan
Refresh-Path
$checks = @{
    'cmake'      = (Test-Cmd 'cmake')
    'ninja'      = (Test-Cmd 'ninja')
    'git'        = (Test-Cmd 'git')
    'clang-cl'   = (Test-Cmd 'clang-cl')
    'glslc'      = (Test-Cmd 'glslc') -or [bool]$sdk
    'MSVC'       = [bool]$withCpp
}
foreach ($k in $checks.Keys) {
    if ($checks[$k]) { Ok $k } else { Fail $k }
}

Write-Host "`nNext:  .\build.ps1 -Config debug -Run" -ForegroundColor Cyan
Write-Host "If cl.exe is not found in your shell, that is expected: build.ps1 enters the VS environment itself.`n"
