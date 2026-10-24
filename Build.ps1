param([switch]$Diagnostics)
$ErrorActionPreference = 'Stop'
$workspace = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$sdk = if ($env:PLUGIN_SDK_DIR) { $env:PLUGIN_SDK_DIR } else { Join-Path $workspace 'work\deps\plugin-sdk' }
$env:PLUGIN_SDK_DIR = $sdk
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ Build Tools not found' }
Push-Location $PSScriptRoot
try {
    $premakeArgs = @('vs2022')
    if ($Diagnostics) { $premakeArgs = @('--diagnostics') + $premakeArgs }
    & (Join-Path $sdk 'tools\premake\premake5.exe') @premakeArgs
    if ($LASTEXITCODE -ne 0) { throw 'Project generation failed' }
    & (Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe') 'build\ViceBounce.vcxproj' /p:Configuration=Release /p:Platform=Win32 /m:4 /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) { throw 'ASI build failed' }
} finally { Pop-Location }
