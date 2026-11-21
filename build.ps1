# Portable Windows x64 release: one compiler, no SDK install or shader tools.
param([string]$Zig='zig', [string]$Headers, [switch]$Run)
$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
    if (!(Get-Command $Zig -ErrorAction SilentlyContinue)) { throw 'Supply -Zig path\to\zig.exe (tested with Zig 0.13.0).' }
    if (!$Headers -and $env:VULKAN_SDK) { $Headers=Join-Path $env:VULKAN_SDK 'Include' }
    if (!$Headers) { & .\setup.ps1; $Headers='build\deps\Vulkan-Headers-d277a0a3cda9c682708c93279f0c72cd6b0ae3f3\include' }
    $out='build\portable'
    New-Item -ItemType Directory -Force "$out\shaders" | Out-Null
    & $Zig dlltool -m i386:x86-64 -d tools/vulkan-1.def -l "$out\vulkan.lib"
    if ($LASTEXITCODE) { throw 'Import library generation failed' }
    $src=(Get-ChildItem src/*.cpp).FullName
    & $Zig c++ -target x86_64-windows-gnu -std=c++17 -Os -s -DNDEBUG -DNOMINMAX -DWIN32_LEAN_AND_MEAN "-I$Headers" @src "$out\vulkan.lib" -luser32 -lgdi32 -lwinmm -lole32 -lxaudio2_8 -o "$out\horror.exe"
    if ($LASTEXITCODE) { throw 'Compile failed' }
    Copy-Item shaders/compiled/*.spv "$out\shaders" -Force
    Write-Host "Game: $out\horror.exe"
    if ($Run) { & "$out\horror.exe"; if ($LASTEXITCODE) { throw "Game exited $LASTEXITCODE" } }
} finally { Pop-Location }
