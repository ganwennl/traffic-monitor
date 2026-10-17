#requires -Version 7.0
[CmdletBinding()]
param([string]$SourceRoot = $PSScriptRoot, [string]$MfcRoot, [string]$BuildRoot, [string]$OutputDir, [switch]$ExecutableOnly)
& (Join-Path $PSScriptRoot 'scripts/build.ps1') @PSBoundParameters
