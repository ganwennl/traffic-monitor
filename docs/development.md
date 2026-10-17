# 开发与目录约定

使用 PowerShell 7、Visual Studio 2022 / Build Tools 的 v143 C++ x64 工具、Windows SDK 和 MFC/ATL。从项目根目录运行：

```powershell
pwsh -File .\build.ps1
pwsh -File .\build.ps1 -ExecutableOnly
pwsh -File .\build.ps1 -MfcRoot 'C:\Toolchains\atlmfc'
```

`build.ps1` 是稳定入口，实现在 `scripts/build.ps1`。默认构建 `Release (lite)|x64`，静态链接 MFC/CRT；只使用本机工具链。`-BuildRoot` 和 `-OutputDir` 必须是源码目录之外、尚不存在的独立目录。源码 ZIP 也可构建。

`TrafficMonitor.sln` 引用 `src/TrafficMonitor`、`src/OpenHardwareMonitorApi` 和 `examples/PluginDemo`。共享接口在 `include`。Lite 发行物不包含上游硬件组件或示例插件，不代表本次验证了其他配置。

`src/TrafficMonitor/res`、`language` 和 `skins` 是程序资源；`data` 存放离线 IP 数据；来源与许可在 `third_party`。文档图片在 `docs/images` 和 `docs/screenshots`，原版历史在 `docs/upstream-history`。

`publish-local.ps1` 继续负责固定本机成品目录交付，实现在 `scripts/publish-local.ps1`。它保留用户配置、皮肤、历史与一份回退，并在成功后清理当次临时产物。具体流程见[维护说明](LOCAL_MAINTENANCE.md)。

构建证据记录真实输入、编译参数、日志和 SHA-256；整理目录不会改变产品版本或自动发布 Releases。
