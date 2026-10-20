$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'Build.ps1')   # builds SABounce.asi and the unit tests and runs them
if (-not (Test-Path (Join-Path $PSScriptRoot 'bin\SABounce.asi'))) { throw 'SABounce.asi was not built' }
Write-Host 'OK: SABounce.asi built, unit tests passed.'
