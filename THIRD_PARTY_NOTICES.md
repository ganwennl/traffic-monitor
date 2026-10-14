# Third-party notices / 第三方声明

## TrafficMonitor

Original author / 原作者: **zhongyang219 (Zhong Yang)**.

This personal edition is based on TrafficMonitor V1.86, historical source commit `02a817a069bac6bf4d263b5209d9c1b07fe2f950`. Original source: <https://github.com/zhongyang219/TrafficMonitor>.

The unchanged `LICENSE` (Anti 996 License v1.0 Draft) and `LICENSE_CN` are included. Source copyright notices are preserved. New personal edition releases use the `-personal` suffix.

这个个人版基于原版做了一些修改，保留原始许可证和作者声明。

## TinyXML2

The following notice is reproduced verbatim from `TrafficMonitor/tinyxml2/tinyxml2.h` and applies to TinyXML2:

```text
Original code by Lee Thomason (www.grinninglizard.com)

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any
damages arising from the use of this software.

Permission is granted to anyone to use this software for any
purpose, including commercial applications, and to alter it and
redistribute it freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must
not claim that you wrote the original software. If you use this
software in a product, an acknowledgment in the product documentation
would be appreciated but is not required.

2. Altered source versions must be plainly marked as such, and
must not be misrepresented as being the original software.

3. This notice may not be removed or altered from any source
distribution.
```

## Plug-in interface / 插件接口

`include/PluginInterface.h` retains `Copyright (C) by Zhong Yang 2021` and the original author notice. No plug-in DLL is bundled.

## Lite release / Lite 版本

Windows/Microsoft C++ libraries are used through the installed Windows SDK and Visual Studio toolchain. MFC/CRT are statically linked; no Microsoft runtime DLL is redistributed separately.

Hardware-monitor third-party attribution remains in the source tree. Hardware-monitor runtime DLLs and kernel drivers are not bundled or loaded by this Lite release.
