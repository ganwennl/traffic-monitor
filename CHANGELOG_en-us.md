# Local release history

[简体中文](./CHANGELOG.md)

## 1.86.3-local

- Hovering over the taskbar speed display or floating window for about 400 ms shows the top eight processes, their PIDs, upload/download rates and totals since collection started. Values refresh every second; the tray keeps its short tooltip.
- Existing main-window and taskbar tooltip switches remain effective. Process totals cover machine-wide TCP/UDP over IPv4/IPv6 and are separate from the selected-adapter totals.
- The main application runs as the current user. A separate collector requests Windows authorization when needed; cancellation keeps ordinary speed monitoring available. Closing the application stops the collector and ETW session.
- The first public portable package includes all bundled skins, Chinese/English setup and usage instructions, licenses, source input hashes and SHA256 checksums. Fresh settings request extra permission only after user action; existing installations retain their settings.
- Added repository-local `build.ps1` for x64 Lite builds from the current tree or a source archive. Original source is preserved; encoding normalization and build-time generation occur only in a temporary copy.

## 1.86.2-local (internal maintenance version)

- Added enabled-by-default per-process traffic ranking, with upload/download rates and totals since collection started.
- Uses native Windows ETW, with a separate collector when permission is required. Installs no driver, service or scheduled task.

## 1.86.1-local

- Established independent maintenance from upstream V1.86.
- Removed application/plug-in online update checks, update-source settings and upstream promotional links. Help, FAQ and release notes are displayed locally.
- Standardized releases on x64 Lite with static MFC/CRT and `asInvoker` main-process privileges.
- Retained original authorship, licenses, third-party attribution and support for existing configuration, skins and traffic history.

`UpdateLog/update_log_en-us.md` contains historical upstream notes. This file describes the independently maintained versions.
