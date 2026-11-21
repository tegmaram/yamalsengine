# No elevation or machine-wide installation. Only fetch the pinned header archive.
$ErrorActionPreference = 'Stop'
$rev = 'd277a0a3cda9c682708c93279f0c72cd6b0ae3f3'
$out = Join-Path $PSScriptRoot 'build\deps'
if (Test-Path "$out\Vulkan-Headers-$rev\include\vulkan\vulkan.h") { return }
New-Item -ItemType Directory -Force $out | Out-Null
$zip = Join-Path $out 'headers.zip'
Invoke-WebRequest "https://codeload.github.com/KhronosGroup/Vulkan-Headers/zip/$rev" -OutFile $zip -UseBasicParsing
if ((Get-FileHash $zip).Hash -ne '72d63f4f67cb8fa21627be3abaa0c1174229c66d702f48ff310f5a654ffc3efd') { throw 'Header archive checksum mismatch' }
Expand-Archive -Force $zip $out
Remove-Item $zip
