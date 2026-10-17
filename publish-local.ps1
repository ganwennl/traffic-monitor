#requires -Version 7.0
[CmdletBinding()]
param([string]$MfcRoot)
& (Join-Path $PSScriptRoot 'scripts/publish-local.ps1') @PSBoundParameters
