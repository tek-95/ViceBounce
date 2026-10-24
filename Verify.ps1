$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'Build.ps1')
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
& (Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe') (Join-Path $PSScriptRoot 'build\ViceBounceTests.vcxproj') /p:Configuration=Release /p:Platform=Win32 /m:4 /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { throw 'Test build failed' }
& (Join-Path $PSScriptRoot 'bin\ViceBounceTests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Test failure' }
& (Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe') (Join-Path $PSScriptRoot 'build\ViceBounceMusicTests.vcxproj') /p:Configuration=Release /p:Platform=Win32 /m:4 /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { throw 'Music test build failed' }
Push-Location (Join-Path $PSScriptRoot 'bin')
try {
    & '.\ViceBounceMusicTests.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Music playback test failure' }
} finally { Pop-Location }

& (Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe') (Join-Path $PSScriptRoot 'build\ViceBounceCompatibilityTests.vcxproj') /p:Configuration=Release /p:Platform=Win32 /m:4 /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { throw 'Compatibility test build failed' }
& (Join-Path $PSScriptRoot 'bin\ViceBounceCompatibilityTests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Compatibility test failure' }
