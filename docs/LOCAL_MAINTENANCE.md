# 个人版维护说明

由于有点个人需求，我自己维护了一个版本，修改了点小功能。原版是 zhongyang219 的 TrafficMonitor V1.86，历史上游提交为 `02a817a069bac6bf4d263b5209d9c1b07fe2f950`。保留原始许可、作者声明和第三方源码版权。

## 版本与发布

- 产品显示版本由 `src/TrafficMonitor/stdafx.h` 的 `VERSION` 定义；Windows 数字文件版本同时在资源中维护。
- 当前发布目标为 x64 Lite，静态 MFC/CRT，`requireAdministrator`，不附带硬件监控 DLL、内核驱动或插件 DLL。
- `1.86.4-personal` 起，启动时自动检查 `ganwennl/traffic-monitor` 的正式 Releases，可在右键菜单关闭或手动检查。发现新版本后由用户确认打开下载页，不自动替换文件；不使用原作者的 Release、Gitee 或插件更新服务。
- 发布时记录版本、实际源码摘要和可执行文件 SHA256。Git 提交只能标识基础历史；存在未提交修改时，以记录的输入 SHA256 清单为准。

## 本机构建

在 PowerShell 7 中执行：

```powershell
& 'D:\newcode\TrafficMonitor\build.ps1' -ExecutableOnly
```

脚本仅使用已安装的 Visual Studio C++ 工具链和本机 MFC 目录，不执行联网依赖恢复。需要时通过 `-SourceRoot`、`-MfcRoot`、`-BuildRoot`、`-OutputDir` 指定真实路径。默认构建和输出目录包含随机标识，现有目录一律拒绝覆盖，不在源码目录构建。

构建过程复制当前维护树并逐文件验证字节，再仅对临时构建副本规范化源文件编码、生成编译时间。原始源码不会因构建被改写。DLL、现成 EXE、用户工程配置、Git 数据和常见构建缓存不进入构建副本。

`BuildRoot` 中保存：

- `SOURCE-INPUTS.json`：本次源码输入的路径、长度和 SHA256。
- `STAGED-INPUTS.json`：编码规范化和编译时间生成后的副本清单。
- `BUILD-MANIFEST.json`：两份清单摘要、真实版本、相关 Git 提交与未提交状态、构建脚本摘要、工具链路径、MSBuild 参数和产物 SHA256。
- `build.log`：构建日志。

历史 `trafficmonitor_upstream_v1.86.json` 只保留原始来源依据，不能用来断言当前源码没有修改，也不再作为构建准入条件。

## 固定本机成品目录

以后本机成品固定放在 `%USERPROFILE%\Documents\ChatGPT\TrafficMonitor\成品`，目录不加版本号，只放运行文件、离线数据库、皮肤、许可和说明。工具链、构建证据和上次回退文件放在旁边的 `%USERPROFILE%\Documents\ChatGPT\TrafficMonitor-维护`，不放进成品目录。

退出正在使用的 TrafficMonitor 后，在 PowerShell 7 中执行：

```powershell
& 'D:\newcode\TrafficMonitor\publish-local.ps1'
```

脚本先使用维护目录的 `toolchain\VC\Tools\MSVC\<工具链版本>\atlmfc`，没有可用目录时沿用 `build.ps1` 的已安装 MFC 查找逻辑；也可显式指定 `-MfcRoot`。构建和完整包在维护目录 `work` 下随机隔离，始终从当前源码生成默认配置，不从个人成品复制配置到包中。

发布更新 EXE、IPv4/IPv6 离线库、许可、说明和公开构建清单，并逐文件核对 SHA256。已有 `config.ini`、`global_cfg.ini`、历史数据和皮肤文件保持原样；仅补入缺少的默认配置与新皮肤文件。运行文件被占用时明确报错，保留旧文件，不关闭程序或模拟键盘输入。

更新前，仅将即将替换的运行文件备份到维护目录 `rollback`，只保留上次一份，不备份个人配置；`RECOVERY.json` 记录旧文件摘要和新添加文件。每个文件以同目录临时文件原子替换；替换过程失败时恢复已修改文件，失败工作目录保留供排查。

成功后，`BUILD-MANIFEST.json`、`SOURCE-INPUTS.json`、`STAGED-INPUTS.json`、`build.log` 和本机发布清单留在维护目录 `evidence\current`，清理本次隔离构建目录和临时包。供 GitHub 发布的干净完整包固定为维护目录的 `releases\TrafficMonitor-Windows-x64-Lite.zip`，仅含新构建默认配置，不含个人配置；压缩后逐文件核对字节摘要。`build.ps1 -ExecutableOnly` 继续用于只生成 EXE 的临时维护，不作为固定目录的完整交付入口。

需要原版的新功能时，先审查和合并源码，再重新编译、验证，通过我这个仓库发布。

## 程序流量明细

`1.86.5-personal` 起，排行榜使用柔和灰蓝配色与圆角，跟随 Windows 应用深色主题；高对比度模式使用系统配色。PID 4 显示为“系统（System）”，其流量可能包含系统或内核代应用收发的数据，不代表可直接归属到某个普通程序。

点击排行榜中的程序可查看当前 TCP IPv4/IPv6 连接、端口和近似归属地；右上角可单独查询 IP。连接与查询仅保存在窗口内存中，不记录到磁盘、不发起在线定位请求。UDP 端点表不提供远端 IP。ip2region 社区数据库随源码保存在 `data`：IPv4 为完整 XDB，IPv6 为两个原始二进制分卷，构建时自动串联并核对官方 SHA256。运行时将还原后的 `ip2region_v4.xdb`、`ip2region_v6.xdb` 放在 EXE 旁的 `data` 目录；数据来源、版本和许可见 `third_party/ip2region`。缺库或坏库时仍显示连接 IP，并提示离线库不可用。

完整构建会复制还原后的数据库和许可；`-ExecutableOnly` 仍只输出 EXE，首次升级此功能时需从 `BuildRoot/source/data` 复制两份 `.xdb`，另复制许可目录，保留用户原有配置、皮肤与历史数据。不要把源码分卷直接当作运行时数据库。

`1.86.3-local` 默认在任务栏网速窗口和悬浮小窗口悬停时显示排行榜（停留约 400 毫秒）。托盘图标保留原来的简短提示，不显示程序排行榜；主窗口和任务栏的 `show_tool_tip` 开关继续有效。排行榜，每秒显示占用最多的前 8 个进程、PID、下载/上传速度及本次累计流量。统计覆盖本机所有连接的 TCP/UDP、IPv4/IPv6；它与主窗口所选网卡的字节计数口径不同，累计从采集启动时开始。

采集使用 Windows 原生 ETW，不安装驱动、服务或计划任务，不保存网络地址、包内容或 ETL 文件。`1.86.6-personal` 起，主程序使用 `requireAdministrator`，启动时仍需一次 Windows UAC 确认；取消则不启动程序。正常情况下直接在主进程内采集，不再另行授权采集器。

`config.ini` 的 `[general]` 中，`show_process_traffic` 默认 `true`；`auto_elevate_process_traffic` 保留原有读取默认值和便携包初始值以兼容已有配置，不控制启动时的 UAC。关闭主程序后采集和专用会话退出，不改变系统用户组或自动启动方式。Lite 的 HKCU Run 启动项不保证管理员 EXE 自动启动，当前版本的开机自启尚未验证；未新增计划任务或改变 UAC 设置。
