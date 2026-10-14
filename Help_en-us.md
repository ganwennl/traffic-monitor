**[简体中文](./Help.md) | English**

# Local Maintenance Edition Help

This help applies to the locally compiled Windows x64 Lite edition. Updates are manual; there is no online update check, update download or update-source selector. See [README](./README_en-us.md) for origin and licenses.

## Updating

Check the version and SHA256 of a new local build, exit the running program and replace only `TrafficMonitor.exe`. Retain configuration, skins and traffic data. Upstream automated releases and plug-ins are not updates for this local edition.

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

Lite stores startup configuration in the current user's `HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run`. After moving the program, disable and re-enable startup to update its path. Administrator execution is unnecessary.

## Configuration cannot be saved or seems to disappear

Use a directory writable by the current user, or choose AppData storage in Data and Configuration Files. In `global_cfg.ini`, `portable_mode=false` uses the user configuration folder; `true` uses the program folder. Confirm that the executable being started is the one being configured.

## Temperature, GPU or disk monitoring is unavailable

The maintained release is Lite and includes no hardware-monitor DLL or kernel driver. Network speed, CPU and RAM usage need neither component.

## An MSVC or MFC DLL is reported missing

The local build statically links MFC/CRT. Verify that the correct local executable is running and report the build version and error to the local maintainer. Do not replace it with a separately downloaded DLL.

## Crashes or plug-in problems

Exit the program and temporarily move any manually added `plugins` DLLs aside, then retry. The local build bundles no plug-ins and provides no plug-in online updater. Record the version, reproduction steps, error and necessary configuration for the local maintainer, removing private information before sharing. The upstream author is not the support contact for local changes.
