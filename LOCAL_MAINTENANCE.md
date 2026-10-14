# 本地维护约定

本项目基于 zhongyang219 的 TrafficMonitor V1.86，历史上游提交为 `02a817a069bac6bf4d263b5209d9c1b07fe2f950`。本地源码和版本有独立修改，不再作为未修改的上游快照发布。原始许可、作者声明与第三方源码版权必须保留。

## 版本与发布

- 产品显示版本由 `TrafficMonitor/stdafx.h` 的 `VERSION` 定义；Windows 数字文件版本同时在资源中维护。
- 当前发布目标为 x64 Lite，静态 MFC/CRT，`asInvoker`，不附带硬件监控 DLL、内核驱动或插件 DLL。
- 程序和插件的在线更新已经移除。后续版本仅从本地源码自行编译，人工验证后发布；不自动访问上游 Release、Gitee 或插件更新服务。
- 发布时记录版本、实际源码摘要和可执行文件 SHA256。Git 提交只能标识基础历史；存在未提交修改时，以记录的输入 SHA256 清单为准。

## 本机构建

在 PowerShell 7 中执行：

```powershell
& 'D:\newcode\scripts\build_trafficmonitor.ps1' -ExecutableOnly
```

脚本仅使用已安装的 Visual Studio C++ 工具链和本机 MFC 目录，不执行联网依赖恢复。需要时通过 `-SourceRoot`、`-MfcRoot`、`-BuildRoot`、`-OutputDir` 指定真实路径。默认构建和输出目录包含随机标识，现有目录一律拒绝覆盖，不在源码目录构建。

构建过程复制当前维护树并逐文件验证字节，再仅对临时构建副本规范化源文件编码、生成编译时间。原始源码不会因构建被改写。DLL、现成 EXE、用户工程配置、Git 数据和常见构建缓存不进入构建副本。

`BuildRoot` 中保存：

- `SOURCE-INPUTS.json`：本次源码输入的路径、长度和 SHA256。
- `STAGED-INPUTS.json`：编码规范化和编译时间生成后的副本清单。
- `BUILD-MANIFEST.json`：两份清单摘要、真实版本、相关 Git 提交与未提交状态、构建脚本摘要、工具链路径、MSBuild 参数和产物 SHA256。
- `build.log`：构建日志。

历史 `trafficmonitor_upstream_v1.86.json` 只保留原始来源依据，不能用来断言当前源码没有修改，也不再作为构建准入条件。

## 手动替换

验证产物后退出旧程序，只把新 `TrafficMonitor.exe` 复制到实际使用目录，核对复制前后 SHA256。保留已有配置、皮肤和历史流量数据。`-ExecutableOnly` 的输出目录仅含 EXE，证据留在 `BuildRoot`；脚本本身不会替换现有用户目录或生成压缩包。

后续接收上游改动时，人工审查和合并所需源码，重新构建及验证，不恢复上游自动更新或自动发布链路。

## 程序流量明细

`1.86.2-local` 默认启用托盘悬停排行榜，每秒显示占用最多的前 8 个进程、PID、下载/上传速度及本次累计流量。统计覆盖本机所有连接的 TCP/UDP、IPv4/IPv6；它与主窗口所选网卡的字节计数口径不同，累计从采集启动时开始。

采集使用 Windows 原生 ETW，不安装驱动、服务或计划任务，不保存网络地址、包内容或 ETL 文件。主程序继续使用 `asInvoker`；权限不足时，单独的同程序采集进程通过 Windows 授权运行。取消授权仍可使用原有网速显示，悬停弹窗提供重新允许统计的入口。

`config.ini` 的 `[general]` 中，`show_process_traffic` 默认 `true`；`auto_elevate_process_traffic` 默认 `true`，改为 `false` 可仅在点击允许统计时申请权限。关闭主程序后采集进程和专用会话退出，不改变系统用户组或自动启动方式。
