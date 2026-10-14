**简体中文 | [English](./README_en-us.md)**

# TrafficMonitor 独立维护版

轻量 Windows 网速与资源监视器：显示实时上传/下载、CPU、内存和历史流量，可放在悬浮窗或任务栏中，并支持皮肤及网卡选择。

当前版本 **1.86.3-local · Windows x64 Lite**。本项目由 [TrafficMonitor V1.86](https://github.com/zhongyang219/TrafficMonitor) 派生，原作者为 **zhongyang219**；我们独立维护源码、编译和发布。程序及插件的在线更新已移除，更新通过本仓库 Releases 手动完成。

## 下载与安装

1. 打开 [Releases](https://github.com/ganwennl/traffic-monitor/releases)，下载最新的 `TrafficMonitor-<版本>-Windows-x64-Lite-portable.zip`。`Source code` 是源码，不能直接运行。
2. 完整解压到当前用户可写的目录，例如 `C:\Tools\TrafficMonitor`；不要直接从 ZIP 中运行，也不要只取 ZIP 中的 EXE。
3. 双击 `TrafficMonitor.exe`。主程序无需安装或管理员权限；MFC/CRT 静态链接，无需另外安装其运行库。

完整包包含 EXE、全部内置皮肤、初始配置、中英文说明、更新日志、许可证和文件校验信息。图标和语言资源已编入 EXE；不附带硬件监控 DLL、内核驱动或插件 DLL。

本次构建验证平台为 Windows 11 x64。程序面向 Windows 10/11 x64，Windows 10 和不同任务栏布局尚未在本次发布中实机验证；不提供 x86、ARM64 原生版本或温度/GPU/硬盘传感器监控。

## 使用

- 右键悬浮窗或通知区图标打开菜单；勾选“显示任务栏窗口”显示任务栏网速。
- 在“选择网络连接”中选择实际使用的网卡，或使用自动选择。切换 Wi-Fi、VPN 后可刷新连接列表。
- “显示更多信息”可显示 CPU 和内存；任务栏窗口通过“显示设置”选择项目。
- “选项”用于修改字体、颜色、单位、开机启动和配置保存位置；“更换皮肤”选择 `skins` 下的皮肤。
- 鼠标停留在任务栏网速或悬浮窗约 400 毫秒可查看进程流量前 8 名。托盘图标仍使用简短提示。

进程流量使用 Windows 原生 ETW。主程序保持普通用户权限；权限不足时，点击明细中的允许统计入口，Windows 会请求启动独立采集进程的管理员授权。取消授权不影响普通网速显示。完整便携包的初始配置关闭自动请求授权；手动替换 EXE 时保留你已有的授权设置。

排行榜统计本机所有 TCP/UDP、IPv4/IPv6 连接，累计从本次采集开始计算，与主窗口选定网卡的统计口径不同。程序不保存网络地址、包内容或 ETL 文件，也不安装驱动、服务或计划任务。详见[使用与排障](./Help.md)。

## 更新和数据保留

1. 下载新版本，并用 Releases 中的 `SHA256SUMS.txt` 检查 ZIP 校验值。
2. 在旧程序菜单中选择“退出”，保留旧 EXE 作为回退副本。
3. **只替换 `TrafficMonitor.exe`**；不要用新包中的 `config.ini`、`global_cfg.ini` 或整个 `skins` 覆盖旧文件。
4. 运行新版本。若需回退，退出程序并换回旧 EXE。

保留 `config.ini`、`global_cfg.ini`、`history_traffic.dat`、`history_traffic.dat.bak`（如存在）和自定义 `skins`。便携模式将设置与历史记录保存在程序目录；AppData 模式通常使用 `%APPDATA%\TrafficMonitor`。以“选项”中的“数据和配置文件”显示位置为准，更新前备份实际目录。程序退出时会自然保存设置和流量。

完整包从空配置开始，不含维护者的私人配置、历史流量或日志。校验命令：

```powershell
Get-FileHash .\TrafficMonitor-1.86.3-local-Windows-x64-Lite-portable.zip -Algorithm SHA256
```

本地版本更新记录见 [CHANGELOG.md](./CHANGELOG.md)。历史上游记录在 `UpdateLog` 中，不能作为本地版本的更新来源。

## 从源码构建

需要 PowerShell 7、Visual Studio 2022/Build Tools 的 v143 C++ x64 工具、Windows SDK 和已安装的 x64 MFC/ATL。从仓库目录运行：

```powershell
pwsh -File .\build.ps1
# MFC 在独立目录时，指定已有目录；脚本不会下载依赖：
pwsh -File .\build.ps1 -MfcRoot 'C:\Toolchains\atlmfc'
# 只输出 EXE，适合更新现有安装：
pwsh -File .\build.ps1 -ExecutableOnly
```

脚本构建当前源码的临时副本，使用 `Release (lite)|x64`、静态 MFC/CRT 和 `asInvoker`。源码、构建和输出目录分离，已存在的输出目录会被拒绝；源码不被改写。源码 ZIP 也可构建。输出 JSON 给出 EXE 和证据路径；构建目录中的 `SOURCE-INPUTS.json`、`STAGED-INPUTS.json`、`BUILD-MANIFEST.json`、`build.log` 记录实际输入和产物。发布包中的 `BUILD-INFO.json` 与 `SOURCE-INPUTS.json` 提供不含本机绝对路径的构建信息。

## 许可与反馈

保留原始 [Anti 996 License](./LICENSE)、[中文版许可](./LICENSE_CN) 与源码版权声明。TinyXML2 原作者为 Lee Thomason；其许可全文及其他归属见 [THIRD_PARTY_NOTICES.md](./THIRD_PARTY_NOTICES.md)。本仓库包含独立修改，发布不使用上游二进制或自动更新链路。

在[本仓库 Issues](https://github.com/ganwennl/traffic-monitor/issues)反馈问题，附版本、Windows 版本和复现步骤；分享配置或截图前删除私人信息。
