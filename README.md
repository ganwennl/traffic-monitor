# TrafficMonitor 个人版

基于 TrafficMonitor V1.86 维护的 Windows 网速监控工具，按自己的需求增加了进程流量明细、连接查看与离线 IP 查询。

[下载成品](https://github.com/ganwennl/traffic-monitor/releases/latest) · [使用说明](docs/usage.md) · [English](docs/usage.en.md) · [更新日志](CHANGELOG.md)

## 使用

下载 Windows x64 Lite 便携包，完整解压后运行 `TrafficMonitor.exe`。程序启动时请求管理员权限，按 Windows 提示确认一次；正常采集无需再次授权，取消启动授权则不启动程序。升级时保留配置、皮肤与流量历史，只替换需要更新的运行文件。详细步骤见[使用与排障](docs/Help.md)。

## 项目目录

| 目录 | 内容 |
| --- | --- |
| `src/TrafficMonitor/` | 主程序、窗口、资源、语言和皮肤 |
| `src/OpenHardwareMonitorApi/` | 上游硬件监控桥接源码，Lite 成品不包含此组件 |
| `include/` | 共享头文件及插件接口 |
| `examples/PluginDemo/` | 插件示例工程 |
| `scripts/` | 构建和本机交付实现 |
| `data/` | 编译和分发所需的离线 IP 数据，不是用户运行数据 |
| `third_party/` | 依赖来源、版本和许可 |
| `docs/` | 使用、维护、皮肤教程、图片和上游历史记录 |

根目录保留 Visual Studio 解决方案及 `build.ps1`、`publish-local.ps1` 两个稳定入口。

## 开发

```powershell
pwsh -File .\build.ps1 -ExecutableOnly
```

需要 Visual Studio 2022 的 C++ x64 工具、Windows SDK、MFC/ATL 和 PowerShell 7。默认构建 x64 Lite，构建与输出目录独立于源码；具体参数见[开发说明](docs/development.md)。

## English

A personally maintained TrafficMonitor fork for Windows, with per-process traffic, TCP connection details and offline IP lookup. Startup requests administrator permission once through Windows; normal collection needs no additional approval. Cancelling startup approval leaves the application closed. See [Releases](https://github.com/ganwennl/traffic-monitor/releases/latest) and the [English guide](docs/usage.en.md). Application code, optional components, plugin examples and documentation are grouped separately.

## 许可

保留原作者与第三方版权：[Anti 996 License](LICENSE)、[中文版](LICENSE_CN)、[第三方声明](THIRD_PARTY_NOTICES.md)。
