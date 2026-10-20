# Build the current locally maintained tree with the installed toolchain only.
#requires -Version 7.0
[CmdletBinding()]
param(
    [string]$SourceRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$MfcRoot,
    [string]$BuildRoot = (Join-Path ([IO.Path]::GetTempPath()) ('TrafficMonitor-build-' + [guid]::NewGuid().ToString('N'))),
    [string]$OutputDir,
    [switch]$ExecutableOnly
)
$ErrorActionPreference = 'Stop'
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$utf8 = [System.Text.UTF8Encoding]::new($false)
$buildScriptSha256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()
$SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path.TrimEnd('\')
$BuildRoot = [IO.Path]::GetFullPath($BuildRoot).TrimEnd('\')
$versionText = Get-Content -LiteralPath (Join-Path $SourceRoot 'src\TrafficMonitor\stdafx.h') -Encoding UTF8 -Raw
$versionMatch = [regex]::Match($versionText, '(?m)^\s*#define\s+VERSION\s+L"([^"]+)"')
if (-not $versionMatch.Success) { throw 'VERSION was not found in TrafficMonitor\stdafx.h.' }
$version = $versionMatch.Groups[1].Value
if ($version -notmatch '^[0-9]+(?:\.[0-9]+)*(?:-[A-Za-z0-9.-]+)?$') { throw "Unsafe version for output path: $version" }
if (-not $OutputDir) {
    $OutputDir = Join-Path ([IO.Path]::GetTempPath()) ('TrafficMonitor-' + $version + '-Windows-x64-Lite-' + [guid]::NewGuid().ToString('N'))
}
$OutputDir = [IO.Path]::GetFullPath($OutputDir).TrimEnd('\')
foreach ($path in @($BuildRoot, $OutputDir)) {
    if ($path.StartsWith($SourceRoot + '\', [StringComparison]::OrdinalIgnoreCase) -or $path -eq $SourceRoot) {
        throw 'BuildRoot and OutputDir must be outside the maintained source tree.'
    }
    if ($SourceRoot.StartsWith($path + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'BuildRoot and OutputDir must not contain the maintained source tree.'
    }
    if (Test-Path -LiteralPath $path) { throw "Directory already exists; use a new directory: $path" }
}
if ($OutputDir -eq $BuildRoot -or $OutputDir.StartsWith($BuildRoot + '\', [StringComparison]::OrdinalIgnoreCase) -or $BuildRoot.StartsWith($OutputDir + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'BuildRoot and OutputDir must be separate directories.'
}
function Get-SourceFiles([string]$Root) {
    $pending = [Collections.Generic.Stack[string]]::new()
    $pending.Push($Root)
    while ($pending.Count -gt 0) {
        foreach ($item in Get-ChildItem -LiteralPath $pending.Pop() -Force) {
            if ($item.Name -in @('.git', '.vs', 'bin', 'obj', 'Debug', 'Release', 'Debug (lite)', 'Release (lite)', 'x64', 'ARM64EC')) { continue }
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Source links are not supported: $($item.FullName)" }
            if ($item.PSIsContainer) { $pending.Push($item.FullName); continue }
            if ($item.Extension -in @('.dll', '.exe', '.user', '.obj', '.pdb', '.pch', '.ilk', '.idb', '.tlog', '.log')) { continue }
            $item
        }
    }
}
function Get-FileEntries([string]$Root) {
    @(Get-SourceFiles $Root | Sort-Object FullName | ForEach-Object {
        [ordered]@{
            path = [IO.Path]::GetRelativePath($Root, $_.FullName).Replace('\', '/')
            bytes = $_.Length
            sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    })
}
function Write-JsonFile([string]$Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 8), $utf8)
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) { throw 'Visual Studio Installer vswhere.exe was not found.' }
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'Visual Studio C++ Build Tools were not found.' }
if (-not $MfcRoot) {
    $installedMfc = @(Get-ChildItem -LiteralPath (Join-Path $vsRoot 'VC\Tools\MSVC') -Directory |
        Sort-Object { [version]$_.Name } -Descending |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'atlmfc\lib\x64\uafxcw.lib') -PathType Leaf })
    if ($installedMfc.Count -eq 0) { throw 'Installed x64 MFC/ATL was not found; pass -MfcRoot to an existing atlmfc directory.' }
    $MfcRoot = Join-Path $installedMfc[0].FullName 'atlmfc'
}
$MfcRoot = (Resolve-Path -LiteralPath $MfcRoot).Path
foreach ($file in @('include\afxwin.h', 'lib\x64\mfcs140u.lib', 'lib\x64\uafxcw.lib', 'lib\x64\atls.lib')) {
    if (-not (Test-Path -LiteralPath (Join-Path $MfcRoot $file) -PathType Leaf)) { throw "Missing installed MFC file: $file" }
}
$msbuild = Join-Path $vsRoot 'MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path -LiteralPath $msbuild -PathType Leaf)) { throw "MSBuild was not found: $msbuild" }
# Source archives are supported; actual source hashes identify every build.
$gitRoot = $null
$gitCommit = $null
$gitStatus = @()
$gitPathspecs = @()
if (Get-Command git -ErrorAction SilentlyContinue) {
    $gitRoot = (& git -C $SourceRoot rev-parse --show-toplevel 2>$null)
    if ($LASTEXITCODE -eq 0 -and $gitRoot) {
        $gitCommit = (& git -C $SourceRoot rev-parse HEAD)
        if ($LASTEXITCODE -ne 0) { throw 'Cannot read the local Git commit.' }
        $sourceGitPath = [IO.Path]::GetRelativePath($gitRoot, $SourceRoot).Replace('\', '/')
        $gitPathspecs = @(if ($sourceGitPath -eq '.') { '.' } else { $sourceGitPath })
        $gitStatus = @(& git -C $gitRoot -c core.quotepath=false status --porcelain=v1 --untracked-files=all -- @gitPathspecs)
        if ($LASTEXITCODE -ne 0) { throw 'Cannot read the scoped local Git status.' }
    } else { $gitRoot = $null }
}
$historicalOrigin = [ordered]@{
    url = 'https://github.com/zhongyang219/TrafficMonitor'; tag = 'V1.86'
    commit = '02a817a069bac6bf4d263b5209d9c1b07fe2f950'
    note = 'Historical origin only; the maintained tree contains local changes.'
}
$inputEntries = @(Get-FileEntries $SourceRoot)
if ($inputEntries.Count -eq 0) { throw 'No maintained source inputs were found.' }
$ipDatabaseSpecs = @((Get-Content -LiteralPath (Join-Path $SourceRoot 'third_party\ip2region\DATA_MANIFEST.json') -Encoding UTF8 -Raw | ConvertFrom-Json).datasets)
if ($ipDatabaseSpecs.Count -ne 2) { throw 'Expected two offline IP database specifications.' }
$ipDataEntries = @()
foreach ($ipVersion in @(4, 6)) {
    $requiredPath = "data/ip2region_v$ipVersion.xdb"
    $spec = @($ipDatabaseSpecs | Where-Object path -eq $requiredPath)
    if ($spec.Count -ne 1 -or $spec[0].bytes -le 0 -or $spec[0].bytes -gt 64MB -or $spec[0].sha256 -notmatch '^[a-f0-9]{64}$') {
        throw "Invalid offline IP database specification: $requiredPath"
    }
    $spec = $spec[0]
    if ($spec.PSObject.Properties['source_storage']) {
        if ($ipVersion -ne 6 -or $spec.source_storage.format -ne 'concatenated-parts' -or $spec.source_storage.parts.Count -ne 2) {
            throw 'Unsupported IP database source storage.'
        }
        $expectedOffset = 0L
        $partNumber = 0
        foreach ($part in $spec.source_storage.parts) {
            $partNumber++
            $entry = @($inputEntries | Where-Object path -eq $part.path)
            if ($part.path -ne "$requiredPath.part$partNumber" -or $part.offset -ne $expectedOffset -or $entry.Count -ne 1 -or
                $part.bytes -le 0 -or $part.bytes -gt 20MB -or $entry[0].bytes -ne $part.bytes -or $entry[0].sha256 -ne $part.sha256) {
                throw 'IP database source part differs from its manifest.'
            }
            $expectedOffset += $part.bytes
        }
        if ($expectedOffset -ne $spec.bytes) { throw 'IP database parts have the wrong total size.' }
    } else {
        $entry = @($inputEntries | Where-Object path -eq $requiredPath)
        if ($entry.Count -ne 1 -or $entry[0].bytes -ne $spec.bytes -or $entry[0].sha256 -ne $spec.sha256) {
            throw "IP database differs from its manifest: $requiredPath"
        }
    }
    $ipDataEntries += [ordered]@{path=$spec.path;bytes=$spec.bytes;sha256=$spec.sha256}
}
New-Item -ItemType Directory -Path $BuildRoot | Out-Null
$stagedSource = Join-Path $BuildRoot 'source'
New-Item -ItemType Directory -Path $stagedSource | Out-Null
$inputManifestPath = Join-Path $BuildRoot 'SOURCE-INPUTS.json'
Write-JsonFile $inputManifestPath $inputEntries
foreach ($entry in $inputEntries) {
    $sourceFile = Join-Path $SourceRoot $entry.path
    $destination = Join-Path $stagedSource $entry.path
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $sourceFile -Destination $destination
    if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256 -or
        (Get-FileHash -LiteralPath $sourceFile -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256) {
        throw "Source changed while staging or copied bytes differ: $($entry.path)"
    }
}
# Restore the official IPv6 bytes from bounded source parts in the build copy only.
foreach ($spec in $ipDatabaseSpecs) {
    if (!$spec.PSObject.Properties['source_storage']) { continue }
    $restoredPath = Join-Path $stagedSource $spec.path
    $restored = [IO.File]::Open($restoredPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try {
        foreach ($part in $spec.source_storage.parts) {
            $partStream = [IO.File]::OpenRead((Join-Path $stagedSource $part.path))
            try { $partStream.CopyTo($restored) } finally { $partStream.Dispose() }
        }
    } finally { $restored.Dispose() }
    if ((Get-Item -LiteralPath $restoredPath).Length -ne $spec.bytes -or
        (Get-FileHash -LiteralPath $restoredPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $spec.sha256) {
        throw 'Restored IPv6 database differs from the official bytes.'
    }
}
# Normalize only the disposable build copy; retain the maintained source bytes.
$strictUtf8 = [System.Text.UTF8Encoding]::new($false, $true)
$legacyChinese = [System.Text.Encoding]::GetEncoding(936, [System.Text.EncoderFallback]::ExceptionFallback, [System.Text.DecoderFallback]::ExceptionFallback)
$normalizedFiles = @()
Get-ChildItem -LiteralPath (Join-Path $stagedSource 'src/TrafficMonitor') -Recurse -File |
    Where-Object { $_.Extension -in @('.cpp', '.h', '.hpp') } | ForEach-Object {
        $sourceFile = $_.FullName
        $bytes = [IO.File]::ReadAllBytes($sourceFile)
        if ($bytes.Length -ge 2 -and (($bytes[0] -eq 255 -and $bytes[1] -eq 254) -or ($bytes[0] -eq 254 -and $bytes[1] -eq 255))) {
            $utf16 = [System.Text.UnicodeEncoding]::new(($bytes[0] -eq 254), $false, $true)
            [IO.File]::WriteAllText($sourceFile, $utf16.GetString($bytes, 2, $bytes.Length - 2), $utf8)
            $normalizedFiles += [IO.Path]::GetRelativePath($stagedSource, $sourceFile).Replace('\', '/')
            return
        }
        try { $null = $strictUtf8.GetString($bytes) }
        catch [System.Text.DecoderFallbackException] {
            [IO.File]::WriteAllText($sourceFile, $legacyChinese.GetString($bytes), $utf8)
            $normalizedFiles += [IO.Path]::GetRelativePath($stagedSource, $sourceFile).Replace('\', '/')
        }
    }
$stagedVersionText = Get-Content -LiteralPath (Join-Path $stagedSource 'src\TrafficMonitor\stdafx.h') -Encoding UTF8 -Raw
$stagedVersionMatch = [regex]::Match($stagedVersionText, '(?m)^\s*#define\s+VERSION\s+L"([^"]+)"')
if (-not $stagedVersionMatch.Success -or $stagedVersionMatch.Groups[1].Value -ne $version) {
    throw 'VERSION changed while the maintained source tree was being staged.'
}
$mfcInclude = [Security.SecurityElement]::Escape((Join-Path $MfcRoot 'include'))
$mfcLib = [Security.SecurityElement]::Escape((Join-Path $MfcRoot 'lib\x64'))
$localProps = Join-Path $BuildRoot 'local-build.props'
$props = @'
<?xml version="1.0" encoding="utf-8"?>
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemDefinitionGroup>
    <ClCompile>
      <AdditionalIncludeDirectories>__MFC_INCLUDE__;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
      <RuntimeLibrary>MultiThreaded</RuntimeLibrary>
      <AdditionalOptions>/utf-8 %(AdditionalOptions)</AdditionalOptions>
    </ClCompile>
    <ResourceCompile>
      <AdditionalIncludeDirectories>__MFC_INCLUDE__;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
    </ResourceCompile>
    <Link>
      <AdditionalLibraryDirectories>__MFC_LIB__;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
      <AdditionalOptions>/PDBALTPATH:%_PDB% %(AdditionalOptions)</AdditionalOptions>
      <UACExecutionLevel>RequireAdministrator</UACExecutionLevel>
    </Link>
  </ItemDefinitionGroup>
</Project>
'@
[IO.File]::WriteAllText($localProps, $props.Replace('__MFC_INCLUDE__', $mfcInclude).Replace('__MFC_LIB__', $mfcLib), $utf8)
# Avoid the upstream prebuild command and its locale-dependent date formatting.
[IO.File]::WriteAllText((Join-Path $stagedSource 'src\TrafficMonitor\compile_time.txt'), ((Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + " `r`n"), $utf8)
$stagedManifestPath = Join-Path $BuildRoot 'STAGED-INPUTS.json'
Write-JsonFile $stagedManifestPath @(Get-FileEntries $stagedSource)
$binDir = Join-Path $BuildRoot 'bin'
$intDir = Join-Path $BuildRoot 'obj'
$log = Join-Path $BuildRoot 'build.log'
$buildArgs = @(
    (Join-Path $stagedSource 'src\TrafficMonitor\TrafficMonitor.vcxproj'), '/nologo', '/m:4', '/t:Rebuild', '/v:minimal',
    '/p:Configuration=Release (lite)', '/p:Platform=x64', '/p:PlatformToolset=v143',
    '/p:UseOfMfc=Static', '/p:PreBuildEventUseInBuild=false', '/p:PostBuildEventUseInBuild=false', '/p:BuildProjectReferences=false',
    ('/p:SolutionDir=' + $stagedSource + '\'), ('/p:OutDir=' + $binDir + '\'), ('/p:IntDir=' + $intDir + '\'),
    ('/p:MFC_KeyFile=' + (Join-Path $MfcRoot 'lib\x64\mfcs140u.lib')),
    ('/p:ATL_KeyFile=' + (Join-Path $MfcRoot 'lib\x64\atls.lib')),
    ('/p:ForceImportBeforeCppTargets=' + $localProps)
)
& $msbuild @buildArgs *> $log
if ($LASTEXITCODE -ne 0) {
    Get-Content -LiteralPath $log -Encoding UTF8 | Select-Object -Last 45
    throw "Build failed; see $log"
}
$exe = Join-Path $binDir 'TrafficMonitor.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw 'No executable was produced.' }
$executableSha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
New-Item -ItemType Directory -Path $OutputDir | Out-Null
$outputExe = Join-Path $OutputDir 'TrafficMonitor.exe'
Copy-Item -LiteralPath $exe -Destination $outputExe
if ((Get-FileHash -LiteralPath $outputExe -Algorithm SHA256).Hash.ToLowerInvariant() -ne $executableSha256) {
    throw 'Copied executable bytes differ from the build output.'
}
if (-not $ExecutableOnly) {
    New-Item -ItemType Directory -Path (Join-Path $OutputDir 'data') | Out-Null
    foreach ($entry in $ipDataEntries) {
        Copy-Item -LiteralPath (Join-Path $stagedSource $entry.path) -Destination (Join-Path $OutputDir $entry.path)
        if ((Get-FileHash -LiteralPath (Join-Path $OutputDir $entry.path) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256) {
            throw "Copied IP database bytes differ: $($entry.path)"
        }
    }
    $ipNotices = Join-Path $OutputDir 'third_party'
    New-Item -ItemType Directory -Path $ipNotices | Out-Null
    Copy-Item -LiteralPath (Join-Path $stagedSource 'third_party\ip2region') -Destination $ipNotices -Recurse
    Copy-Item -LiteralPath (Join-Path $stagedSource 'src\TrafficMonitor\skins') -Destination $OutputDir -Recurse
    foreach ($document in @('LICENSE', 'LICENSE_CN', 'README.md', 'CHANGELOG.md', 'CHANGELOG_en-us.md', 'THIRD_PARTY_NOTICES.md')) {
        Copy-Item -LiteralPath (Join-Path $stagedSource $document) -Destination $OutputDir
    }
    $docs = Join-Path $OutputDir 'docs'
    New-Item -ItemType Directory -Path $docs | Out-Null
    foreach ($document in @('usage.md', 'usage.en.md', 'Help.md', 'Help_en-us.md', 'development.md')) {
        Copy-Item -LiteralPath (Join-Path $stagedSource 'docs' $document) -Destination $docs
    }
    [IO.File]::WriteAllText((Join-Path $OutputDir 'global_cfg.ini'), "[config]`r`nportable_mode=true`r`n", $utf8)
    [IO.File]::WriteAllText((Join-Path $OutputDir 'config.ini'), "[general]`r`nlanguage=2052`r`nshow_process_traffic=true`r`nauto_elevate_process_traffic=false`r`n[config]`r`nshow_cpu_memory=false`r`nshow_task_bar_wnd=false`r`n", $utf8)
}
$manifest = [ordered]@{
    version = $version; sourceRoot = $SourceRoot; historicalOrigin = $historicalOrigin
    localGit = [ordered]@{ repository = $gitRoot; commit = $gitCommit; dirty = ($gitStatus.Count -gt 0); paths = $gitPathspecs; status = $gitStatus }
    sourceInputs = [ordered]@{ manifest = $inputManifestPath; sha256 = (Get-FileHash -LiteralPath $inputManifestPath -Algorithm SHA256).Hash.ToLowerInvariant(); files = $inputEntries.Count }
    stagedInputs = [ordered]@{ manifest = $stagedManifestPath; sha256 = (Get-FileHash -LiteralPath $stagedManifestPath -Algorithm SHA256).Hash.ToLowerInvariant(); normalizedEncodings = $normalizedFiles }
    buildScript = [ordered]@{ path = $PSCommandPath; sha256 = $buildScriptSha256 }
    invocation = [ordered]@{ sourceRoot = $SourceRoot; mfcRoot = $MfcRoot; buildRoot = $BuildRoot; outputDir = $OutputDir; executableOnly = [bool]$ExecutableOnly }
    buildProperties = [ordered]@{ path = $localProps; sha256 = (Get-FileHash -LiteralPath $localProps -Algorithm SHA256).Hash.ToLowerInvariant() }
    configuration = 'Release (lite)'; platform = 'x64'; mfc = 'Static'; crt = 'MultiThreaded'; uac = 'RequireAdministrator'
    executable = $outputExe; executableSha256 = $executableSha256; executableOnly = [bool]$ExecutableOnly
    ipDatabases = @($ipDataEntries); ipDatabasesIncluded = !$ExecutableOnly
    buildLog = $log; buildDirectory = $BuildRoot; compiler = $vsRoot; mfcRoot = $MfcRoot; msbuild = $msbuild; msbuildArguments = $buildArgs
    builtAt = (Get-Date).ToString('o')
    notes = @('Built from the current local maintenance tree, not an unmodified upstream snapshot.', 'No network dependency restore requested.', 'No hardware-monitor DLL, kernel driver or plugin DLL bundled.')
}
$manifestPath = Join-Path $BuildRoot 'BUILD-MANIFEST.json'
Write-JsonFile $manifestPath $manifest
if (-not $ExecutableOnly) {
    Copy-Item -LiteralPath $inputManifestPath -Destination $OutputDir
    $publicInfo = [ordered]@{
        version = $version; builtAt = $manifest.builtAt
        sourceInputsSha256 = $manifest.sourceInputs.sha256; sourceFiles = $inputEntries.Count
        executableSha256 = $executableSha256; buildScriptSha256 = $buildScriptSha256
        configuration = 'Release (lite)'; platform = 'x64'; mfc = 'Static'; crt = 'MultiThreaded'; uac = 'RequireAdministrator'
        msbuildVersion = (& $msbuild /nologo /version | Select-Object -Last 1)
        historicalOrigin = $historicalOrigin
        notes = @('Source input hashes identify the build, including local modifications.', 'No network dependency restore requested.', 'No hardware-monitor DLL, kernel driver or plug-in DLL bundled.', 'The application requests administrator permission at startup; process traffic normally runs in the main process without a separate elevation prompt.')
    }
    Write-JsonFile (Join-Path $OutputDir 'BUILD-INFO.json') $publicInfo
}

[pscustomobject]@{Executable=$outputExe;Version=$version;SHA256=$executableSha256;BuildManifest=$manifestPath;BuildLog=$log;BuildRoot=$BuildRoot} | ConvertTo-Json
