# Build and publish only runtime files to the user's fixed local product directory.
#requires -Version 7.0
[CmdletBinding()]
param([string]$MfcRoot)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$utf8 = [Text.UTF8Encoding]::new($false)
$OutputEncoding = $utf8
[Console]::OutputEncoding = $utf8

function Assert-NoLinks([string]$Path) {
    $current = [IO.Path]::GetFullPath($Path)
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Links are not supported: $current" }
        }
        $parent = [IO.Path]::GetDirectoryName($current)
        if ($parent -eq $current) { break }
        $current = $parent
    }
}
function Assert-Under([string]$Path, [string]$Root) {
    $full = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    $base = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    if (-not $full.StartsWith($base + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "Path escapes its expected directory: $full" }
    Assert-NoLinks $full
}
function New-SafeDirectory([string]$Path) {
    Assert-NoLinks $Path
    $null = [IO.Directory]::CreateDirectory($Path)
    Assert-NoLinks $Path
}
function Get-SafeFiles([string]$Root) {
    Assert-NoLinks $Root
    $pending = [Collections.Generic.Stack[string]]::new()
    $pending.Push($Root)
    while ($pending.Count) {
        foreach ($item in Get-ChildItem -LiteralPath $pending.Pop() -Force) {
            Assert-Under $item.FullName $Root
            if ($item.PSIsContainer) { $pending.Push($item.FullName) } else { $item }
        }
    }
}
function Remove-SafeTree([string]$Path, [string]$Root) {
    Assert-Under $Path $Root
    if (-not (Test-Path -LiteralPath $Path)) { return }
    $resolved = (Resolve-Path -LiteralPath $Path).Path
    Assert-Under $resolved $Root
    $null = @(Get-SafeFiles $resolved)
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
function Get-Sha([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Write-Json([string]$Path, $Value) {
    Assert-NoLinks $Path
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 12), $utf8)
}
function Assert-Bytes([string]$Path, [long]$Bytes, [string]$Sha) {
    Assert-NoLinks $Path
    if ((Get-Item -LiteralPath $Path).Length -ne $Bytes -or (Get-Sha $Path) -ne $Sha) { throw "File verification failed: $Path" }
}
function Set-AtomicFile([string]$Temporary, [string]$Destination) {
    Assert-NoLinks $Destination
    if (Test-Path -LiteralPath $Destination) {
        [IO.File]::Replace($Temporary, $Destination, [NullString]::Value, $true)
    } else { [IO.File]::Move($Temporary, $Destination) }
}

if (-not $env:USERPROFILE) { throw 'USERPROFILE is not set.' }
$chatRoot = Join-Path $env:USERPROFILE 'Documents\ChatGPT'
$product = [IO.Path]::GetFullPath((Join-Path $chatRoot 'TrafficMonitor\成品'))
$maintenance = [IO.Path]::GetFullPath((Join-Path $chatRoot 'TrafficMonitor-维护'))
$workParent = Join-Path $maintenance 'work'
$work = Join-Path $workParent ([guid]::NewGuid().ToString('N'))
$build = Join-Path $work 'build'
$package = Join-Path $work 'package'
$rollback = Join-Path $maintenance 'rollback'
$evidence = Join-Path $maintenance 'evidence\current'
$releaseZip = Join-Path $maintenance 'releases\TrafficMonitor-Windows-x64-Lite.zip'
$buildScript = Join-Path $PSScriptRoot 'build.ps1'
Assert-NoLinks $PSScriptRoot
Assert-NoLinks $buildScript
Assert-Under $product $chatRoot
Assert-Under $maintenance $chatRoot
New-SafeDirectory $maintenance
$publishLock = $null
$temporaries = [Collections.Generic.List[string]]::new()
$applied = [Collections.Generic.List[object]]::new()
$success = $false
try {
    try {
        Assert-NoLinks (Join-Path $maintenance 'publish.lock')
        $publishLock = [IO.File]::Open((Join-Path $maintenance 'publish.lock'), [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    } catch { throw 'Another local publication is running; no product files were changed.' }
    New-SafeDirectory $work
    $buildParameters = @{ BuildRoot = $build; OutputDir = $package }
    if ($MfcRoot) {
        Assert-NoLinks $MfcRoot
        $buildParameters.MfcRoot = $MfcRoot
    } else {
        $msvc = Join-Path $maintenance 'toolchain\VC\Tools\MSVC'
        Assert-NoLinks $msvc
        if (Test-Path -LiteralPath $msvc -PathType Container) {
            $candidates = @(Get-ChildItem -LiteralPath $msvc -Directory | Where-Object Name -Match '^\d+(?:\.\d+)+$' | Sort-Object { [version]$_.Name } -Descending)
            foreach ($candidate in $candidates) {
                $candidateMfc = Join-Path $candidate.FullName 'atlmfc'
                Assert-NoLinks $candidateMfc
                $valid = $true
                foreach ($required in @('include\afxwin.h', 'lib\x64\mfcs140u.lib', 'lib\x64\uafxcw.lib', 'lib\x64\atls.lib')) {
                    $candidateFile = Join-Path $candidateMfc $required
                    Assert-NoLinks $candidateFile
                    if (-not (Test-Path -LiteralPath $candidateFile -PathType Leaf)) { $valid = $false; break }
                }
                if ($valid) { $buildParameters.MfcRoot = $candidateMfc; break }
            }
        }
    }
    # Do not request ExecutableOnly: fresh defaults and both runtime databases are required.
    & $buildScript @buildParameters
    if (-not $?) { throw 'The isolated build failed.' }
    $files = @(Get-SafeFiles $package)
    $rootFiles = @('TrafficMonitor.exe', 'config.ini', 'global_cfg.ini', 'LICENSE', 'LICENSE_CN', 'README.md', 'README_en-us.md', 'Help.md', 'Help_en-us.md', 'CHANGELOG.md', 'CHANGELOG_en-us.md', 'THIRD_PARTY_NOTICES.md', 'BUILD-INFO.json', 'SOURCE-INPUTS.json')
    foreach ($required in @('TrafficMonitor.exe', 'data\ip2region_v4.xdb', 'data\ip2region_v6.xdb', 'BUILD-INFO.json', 'SOURCE-INPUTS.json', 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'third_party\ip2region\LICENSE.md')) {
        if (-not (Test-Path -LiteralPath (Join-Path $package $required) -PathType Leaf)) { throw "Missing package file: $required" }
    }
    $info = Get-Content -LiteralPath (Join-Path $package 'BUILD-INFO.json') -Encoding UTF8 -Raw | ConvertFrom-Json
    if ((Get-Sha (Join-Path $package 'TrafficMonitor.exe')) -ne $info.executableSha256) { throw 'Executable differs from BUILD-INFO.json.' }
    $databaseInfo = Get-Content -LiteralPath (Join-Path $package 'third_party\ip2region\DATA_MANIFEST.json') -Encoding UTF8 -Raw | ConvertFrom-Json
    foreach ($ip in @(4, 6)) {
        $relative = "data/ip2region_v$ip.xdb"
        $spec = @($databaseInfo.datasets | Where-Object path -eq $relative)
        if ($spec.Count -ne 1) { throw "Invalid runtime database manifest: $relative" }
        Assert-Bytes (Join-Path $package $relative) $spec[0].bytes $spec[0].sha256
    }
    # Prepare evidence and a clean portable ZIP from fresh build defaults, before local replacement.
    $nextEvidence = Join-Path $work 'evidence-next'
    New-SafeDirectory $nextEvidence
    foreach ($name in @('BUILD-MANIFEST.json', 'SOURCE-INPUTS.json', 'STAGED-INPUTS.json', 'build.log', 'local-build.props')) {
        $source = Join-Path $build $name
        Assert-Under $source $build
        if (Test-Path -LiteralPath $source -PathType Leaf) { Copy-Item -LiteralPath $source -Destination (Join-Path $nextEvidence $name) }
        elseif ($name -ne 'local-build.props') { throw "Missing build evidence: $name" }
    }
    $nextZip = Join-Path $work 'portable.zip'
    [IO.Compression.ZipFile]::CreateFromDirectory($package, $nextZip)
    $archive = [IO.Compression.ZipFile]::OpenRead($nextZip)
    try {
        $zippedFiles = @($archive.Entries | Where-Object { $_.Name })
        if ($zippedFiles.Count -ne $files.Count) { throw 'Portable ZIP has an unexpected file count.' }
        foreach ($zippedFile in $zippedFiles) {
            $source = Join-Path $package $zippedFile.FullName
            Assert-Under $source $package
            $stream = $zippedFile.Open()
            try { $sha = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream)).ToLowerInvariant() } finally { $stream.Dispose() }
            Assert-Bytes $source $zippedFile.Length $sha
        }
    } finally { $archive.Dispose() }
    New-SafeDirectory $product
    $entries = [Collections.Generic.List[object]]::new()
    foreach ($file in $files) {
        $relative = [IO.Path]::GetRelativePath($package, $file.FullName).Replace('\', '/')
        if ($relative -notin $rootFiles -and $relative -notin @('data/ip2region_v4.xdb', 'data/ip2region_v6.xdb') -and -not $relative.StartsWith('third_party/ip2region/') -and -not $relative.StartsWith('skins/')) { throw "Unexpected package file: $relative" }
        $target = Join-Path $product $relative
        Assert-Under $target $product
        if ((Test-Path -LiteralPath $target) -and ($relative -in @('config.ini', 'global_cfg.ini') -or $relative.StartsWith('skins/'))) { continue }
        $sha = Get-Sha $file.FullName
        $exists = Test-Path -LiteralPath $target -PathType Leaf
        $oldSha = $null
        if ($exists) {
            try { $oldSha = Get-Sha $target }
            catch { throw "Product file is in use or unreadable; old files are retained. Close TrafficMonitor and retry: $target" }
            if ($oldSha -eq $sha) { continue }
        }
        $entries.Add([pscustomobject]@{ path = $relative; source = $file.FullName; target = $target; bytes = $file.Length; sha256 = $sha; existed = $exists; oldSha256 = $oldSha; oldBytes = $(if ($exists) { (Get-Item -LiteralPath $target).Length } else { 0 }) })
    }
    # Check all replacements before touching any current product file, especially a running EXE.
    foreach ($entry in $entries) {
        if (-not $entry.existed) { continue }
        try {
            $probe = [IO.File]::Open($entry.target, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
            $probe.Dispose()
        } catch { throw "Product file is in use or not writable; old files are retained. Close TrafficMonitor and retry: $($entry.target)" }
    }
    if ($entries.Count) {
        $nextRollback = Join-Path $work 'rollback-next'
        New-SafeDirectory $nextRollback
        foreach ($entry in $entries) {
            if ($entry.existed) {
                # Configurations and existing skins were excluded above, so no personal files are backed up.
                $backup = Join-Path $nextRollback $entry.path
                Assert-Under $backup $nextRollback
                New-SafeDirectory (Split-Path -Parent $backup)
                Copy-Item -LiteralPath $entry.target -Destination $backup
                Assert-Bytes $backup $entry.oldBytes $entry.oldSha256
            }
            New-SafeDirectory (Split-Path -Parent $entry.target)
            $temporary = Join-Path (Split-Path -Parent $entry.target) ('.publish-' + [guid]::NewGuid().ToString('N') + '.tmp')
            Assert-Under $temporary $product
            $temporaries.Add($temporary)
            [IO.File]::Copy($entry.source, $temporary, $false)
            Assert-Bytes $temporary $entry.bytes $entry.sha256
            $entry | Add-Member -NotePropertyName temporary -NotePropertyValue $temporary
        }
        Write-Json (Join-Path $nextRollback 'RECOVERY.json') ([ordered]@{ productDirectory = $product; createdAt = (Get-Date).ToString('o'); files = @($entries | Select-Object path, existed, oldBytes, oldSha256) })
        Remove-SafeTree $rollback $maintenance
        Assert-Under $nextRollback $work
        [IO.Directory]::Move($nextRollback, $rollback)
        try {
            foreach ($entry in $entries) {
                Set-AtomicFile $entry.temporary $entry.target
                $applied.Add($entry)
                Assert-Bytes $entry.target $entry.bytes $entry.sha256
            }
        } catch {
            $publicationError = $_
            $recoveryErrors = [Collections.Generic.List[string]]::new()
            for ($i = $applied.Count - 1; $i -ge 0; $i--) {
                $entry = $applied[$i]
                try {
                    Assert-Under $entry.target $product
                    if ($entry.existed) {
                        $restore = Join-Path (Split-Path -Parent $entry.target) ('.publish-' + [guid]::NewGuid().ToString('N') + '.tmp')
                        $temporaries.Add($restore)
                        [IO.File]::Copy((Join-Path $rollback $entry.path), $restore, $false)
                        Set-AtomicFile $restore $entry.target
                        Assert-Bytes $entry.target $entry.oldBytes $entry.oldSha256
                    } else { Remove-Item -LiteralPath $entry.target -Force }
                } catch { $recoveryErrors.Add("$($entry.path): $($_.Exception.Message)") }
            }
            if ($recoveryErrors.Count) { throw "Publication failed. Some files need recovery from ${rollback}: $($recoveryErrors -join '; '). Original error: $publicationError" }
            throw "Publication failed; changed files were restored. $publicationError"
        }
    }
    # Private build paths and compiler metadata stay outside the product directory.
    $published = @($files | ForEach-Object {
        $relative = [IO.Path]::GetRelativePath($package, $_.FullName).Replace('\', '/')
        if ($relative -in @('config.ini', 'global_cfg.ini') -or $relative.StartsWith('skins/')) { return }
        $target = Join-Path $product $relative
        Assert-Under $target $product
        Assert-Bytes $target $_.Length (Get-Sha $_.FullName)
        [ordered]@{ path = $relative; bytes = $_.Length; sha256 = Get-Sha $target }
    })
    $zipSha = Get-Sha $nextZip
    Write-Json (Join-Path $nextEvidence 'LOCAL-PUBLISH.json') ([ordered]@{ version = $info.version; productDirectory = $product; rollbackDirectory = $rollback; portableZip = $releaseZip; portableZipSha256 = $zipSha; publishedAt = (Get-Date).ToString('o'); files = $published })
    New-SafeDirectory (Split-Path -Parent $releaseZip)
    Assert-Under $releaseZip $maintenance
    Set-AtomicFile $nextZip $releaseZip
    if ((Get-Sha $releaseZip) -ne $zipSha) { throw 'Published portable ZIP differs from the verified bytes.' }
    New-SafeDirectory (Split-Path -Parent $evidence)
    Remove-SafeTree $evidence (Join-Path $maintenance 'evidence')
    Assert-Under $nextEvidence $work
    [IO.Directory]::Move($nextEvidence, $evidence)
    Remove-SafeTree $build $work
    Remove-SafeTree $package $work
    if (@(Get-ChildItem -LiteralPath $work -Force).Count -eq 0) {
        Assert-Under $work $workParent
        Remove-Item -LiteralPath $work
    }
    $success = $true
    [pscustomobject]@{ ProductDirectory = $product; Version = $info.version; SHA256 = Get-Sha (Join-Path $product 'TrafficMonitor.exe'); PortableZip = $releaseZip; PortableZipSha256 = $zipSha; EvidenceDirectory = $evidence; RollbackDirectory = $rollback } | ConvertTo-Json
} finally {
    foreach ($temporary in $temporaries) {
        Assert-Under $temporary $product
        if (Test-Path -LiteralPath $temporary -PathType Leaf) { Remove-Item -LiteralPath $temporary -Force }
    }
    if ($publishLock) { $publishLock.Dispose() }
    if (-not $success -and (Test-Path -LiteralPath $work)) { Write-Warning "Publication did not finish; diagnostic files are retained: $work" }
}
