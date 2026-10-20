**[简体中文](Help.md) | English**

# Personal Edition Help

This help applies to this repository's Windows x64 Lite release. For a fresh installation, extract the entire portable ZIP from Releases, then run `TrafficMonitor.exe`. See [README](usage.en.md) for installation, origin and licenses.

## Updating

Update checks use only [this repository's Releases](https://github.com/ganwennl/traffic-monitor/releases). They run at startup; you can also check manually or turn automatic checks off in the context menu. A new version prompts you before opening the download page. Failed checks do not interrupt the normal display.

Check the version and SHA256, choose Exit in the old program's menu and replace only `TrafficMonitor.exe`. Keep `config.ini`, `global_cfg.ini`, `skins`, `history_traffic.dat` and its `.bak`. Do not replace existing settings with package defaults. Portable data lives beside the EXE; in AppData mode, check the actual location in Options. Get updates for this personal edition from this repository.

## See which processes are using the network

Hover over the taskbar speed display or floating window for about 400 ms to see the top eight processes, their PIDs, upload/download rates and totals since collection started. The tray keeps its short tooltip. Disabling the corresponding window's tooltip also disables this popup.

The application requests administrator permission at startup. Confirm Windows UAC once; cancelling leaves the application closed. Normal collection runs in the main process without separate collector approval. Under `[general]` in `config.ini`, `show_process_traffic=false` disables details. `auto_elevate_process_traffic` is retained for compatibility and does not control startup UAC. Replacing only the EXE retains existing preferences.

Details cover machine-wide TCP/UDP over IPv4/IPv6 and use different accounting from the selected-adapter totals. Traffic before collection started is not included. New connections can take one refresh cycle to appear. No addresses, packet contents or ETL files are saved. Exiting the main application stops collection.

## Network speed stays at zero

Refresh the network connection list from the context menu and select the active adapter. Options → General Settings → Advanced lets you choose which connections to monitor. A Wi-Fi, VPN or virtual adapter change may leave the previously selected adapter idle.

## Showing CPU and RAM usage

Enable Show More Information in the main window menu. Choose taskbar items through Display Settings. To more closely match Windows Task Manager's CPU reading, choose Use Performance Counter in General Settings → Advanced → CPU Usage Acquisition Method.

## Changing labels, colors and fonts

Open Options → Taskbar Window Settings. Edit labels through Display Text Settings. Enable separate item colors to assign each color. Custom labels remain when changing language; use Restore Default when needed.

## Clipped values or an obstructed taskbar window

Increase the number of display digits or reduce the font size. In Windows 11, enable the option to avoid overlap with right-side widgets. Left-side placement may be unavailable when the taskbar is left-aligned. If icons fill the taskbar, reduce their number or use the floating window. For HDR display problems, try Direct2D rendering or disable background transparency.

## Disabling click-through

Right-click the notification icon and clear Mouse Penetration. A click-through floating window does not accept mouse clicks.

## Startup no longer works

Lite still stores startup configuration in the current user's `HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run`. This does not guarantee automatic startup of an administrator EXE, and startup at logon has not been verified for this version. Start it manually and confirm UAC if needed. No scheduled task was added and UAC settings are unchanged. Moving the program also requires updating the old startup path.

## Configuration cannot be saved or seems to disappear

Use a directory writable by the current user, or choose AppData storage in Data and Configuration Files. In `global_cfg.ini`, `portable_mode=false` uses the user configuration folder; `true` uses the program folder. Confirm that the executable being started is the one being configured.

## Temperature, GPU or disk monitoring is unavailable

The release is Lite and includes no hardware-monitor DLL or kernel driver. Network speed, CPU and RAM usage need neither component.

## An MSVC or MFC DLL is reported missing

The release statically links MFC/CRT. Verify that the correct executable from this repository is running and report the version and error through this repository's Issues. Do not replace it with a separately downloaded DLL.

## Crashes or plug-in problems

Exit the program and temporarily move manually added `plugins` DLLs aside, then retry. The release bundles no plug-ins and provides no plug-in online updater. Report the version, reproduction steps, error and necessary configuration through this repository's Issues, removing private information before sharing. Please report issues with my changes here.
