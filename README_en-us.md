**[简体中文](./README.md) | English**

# TrafficMonitor Personal Edition

A Windows tool for network speed, CPU, RAM and traffic history. It can sit in a floating window or on the taskbar, with skins and network adapter selection.

I wanted a few things for my own use, so I keep my own version with a few small changes. It is based on [TrafficMonitor V1.86](https://github.com/zhongyang219/TrafficMonitor), originally written by **zhongyang219**.

Current version: **1.86.4-personal · Windows x64 Lite**. Update checks use only [my repository's Releases](https://github.com/ganwennl/traffic-monitor/releases). They can be turned off. When a new version is available, the program asks before opening the download page.

## Download and install

1. Open [Releases](https://github.com/ganwennl/traffic-monitor/releases) and download the latest `TrafficMonitor-<version>-Windows-x64-Lite-portable.zip`. `Source code` downloads are source archives, not runnable applications.
2. Extract the whole ZIP to a directory writable by your account, such as `C:\Tools\TrafficMonitor`. Do not run from the ZIP or copy only its EXE for a fresh installation.
3. Run `TrafficMonitor.exe`. The main program needs no installer or administrator permission. MFC/CRT are statically linked, so no separate runtime installation is needed.

The full package includes the EXE, all bundled skins, initial settings, Chinese/English documentation, changelogs, licenses and file verification information. Icons and language resources are embedded in the EXE. No hardware-monitor DLL, kernel driver or plug-in DLL is bundled.

This build was checked on Windows 11 x64. The program targets Windows 10/11 x64; Windows 10 and other taskbar layouts were not exercised for this release. Native x86/ARM64 builds and temperature/GPU/disk-sensor monitoring are not provided.

## Use

- Right-click the floating window or notification icon for the menu. Enable Show Taskbar Window for taskbar speed monitoring.
- Choose the active adapter under Network Connections or use automatic selection. Refresh after changing Wi-Fi or VPN connections.
- Enable Show More Information for CPU and RAM; use Display Settings to select taskbar items.
- Use Options for fonts, colors, units, startup and configuration storage. Change Skin selects a folder under `skins`.
- Hover over the taskbar speed display or floating window for about 400 ms to see the eight processes using the most traffic. The tray icon keeps its short tooltip.

Per-process traffic uses native Windows ETW. The main application keeps normal user privileges. If collection needs additional permission, click the allow-collection action in the details popup: Windows requests administrator authorization for a separate collector. Cancelling leaves ordinary speed monitoring available. Fresh portable settings disable automatic permission requests; replacing only the EXE preserves your existing preference.

Process ranking covers machine-wide TCP/UDP over IPv4/IPv6. Totals start when collection starts and differ from the selected-adapter accounting in the main window. No network addresses, packet contents or ETL files are saved, and no driver, service or scheduled task is installed. See [usage and troubleshooting](./Help_en-us.md).

## Update without losing data

The program checks this repository at startup. The context menu also lets you check manually or turn automatic checks off. It does not replace your files automatically; download the new version and follow these steps.

1. Download a new release and compare its ZIP hash with the release's `SHA256SUMS.txt`.
2. Choose Exit in the old program's menu. Keep the old EXE as a rollback copy.
3. **Replace only `TrafficMonitor.exe`**. Do not overwrite existing `config.ini`, `global_cfg.ini` or the entire `skins` folder with package defaults.
4. Start the new version. To roll back, exit it and restore the old EXE.

Keep `config.ini`, `global_cfg.ini`, `history_traffic.dat`, `history_traffic.dat.bak` (if present) and custom `skins`. Portable mode stores settings and history beside the EXE; AppData mode normally uses `%APPDATA%\TrafficMonitor`. Use the actual directory shown under Data and Configuration Files in Options, and back it up first. Exiting naturally saves settings and traffic counters.

The package starts with clean settings and contains no maintainer configuration, traffic history or logs. Example hash check:

```powershell
Get-FileHash .\TrafficMonitor-1.86.4-personal-Windows-x64-Lite-portable.zip -Algorithm SHA256
```

My changes are listed in [CHANGELOG_en-us.md](./CHANGELOG_en-us.md). `UpdateLog` keeps the original project's historical notes.

## Build from source

Use PowerShell 7, Visual Studio 2022/Build Tools with v143 C++ x64 tools, a Windows SDK and installed x64 MFC/ATL. From the repository directory:

```powershell
pwsh -File .\build.ps1
# For an existing separate MFC directory; no dependencies are downloaded:
pwsh -File .\build.ps1 -MfcRoot 'C:\Toolchains\atlmfc'
# EXE-only output for an existing installation:
pwsh -File .\build.ps1 -ExecutableOnly
```

The script builds a temporary copy of the current tree using `Release (lite)|x64`, static MFC/CRT and `asInvoker`. Source, build and output directories are separate; existing output directories are rejected and original source is preserved. Source archives are supported too. Final JSON identifies the EXE and evidence paths. `SOURCE-INPUTS.json`, `STAGED-INPUTS.json`, `BUILD-MANIFEST.json` and `build.log` in the build directory record actual inputs and outputs. Package `BUILD-INFO.json` and `SOURCE-INPUTS.json` omit local absolute paths.

## Licensing and feedback

The original [Anti 996 License](./LICENSE), [Chinese license](./LICENSE_CN) and source copyright notices are retained. TinyXML2 was written by Lee Thomason; its full notice and other attribution are in [THIRD_PARTY_NOTICES.md](./THIRD_PARTY_NOTICES.md). The downloads here are built from my modified version.

Report issues through [this repository](https://github.com/ganwennl/traffic-monitor/issues), including the application version, Windows version and reproduction steps. Remove private information before sharing configuration or screenshots.
