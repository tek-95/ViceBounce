$ErrorActionPreference = 'Stop'
$sdk = if ($env:PLUGIN_SDK_DIR) { $env:PLUGIN_SDK_DIR } else { Join-Path $env:USERPROFILE 'sa-bounce-ref\plugin-sdk' }
$env:PLUGIN_SDK_DIR = $sdk
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ Build Tools not found' }
Push-Location $PSScriptRoot
try {
    & (Join-Path $sdk 'tools\premake\premake5.exe') vs2022
    if ($LASTEXITCODE -ne 0) { throw 'Project generation failed' }
    $msbuild = Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe'
    & $msbuild 'build\SABounce.sln' /p:Configuration=Release /p:Platform=Win32 /m:4 /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
    & '.\bin\SABounceTests.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Unit tests failed' }
} finally { Pop-Location }
