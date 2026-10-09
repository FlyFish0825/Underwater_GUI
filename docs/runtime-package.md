# 干净运行包与目录约定

[返回 README](../README.md)

## 目录规则

用户于 2026-10-09 明确要求 `build/gui` 只保留重要运行文件，测试文件放在它同级的
`build/test`。这个规则优先于历史文档中与 `build` 同级的 `build-test` 规则。

- `build/gui`：主程序、必要 Qt/MinGW DLL、Windows/JPEG 插件和 `qt.conf`。
- `build/test/gui`：主程序构建缓存、编译数据库、对象文件、静态库、autogen 和测试程序。
- `build/test` 的其他子目录：日志、截图、测试录制、备份、清理清单与解压验证。
- `tests`：版本管理中的测试源码，不放生成物。

VSCode/CMake/clangd 已统一使用 `build/test/gui`，运行和调试目标仍是 `build/gui/rov_ui.exe`。
CMake 拒绝将运行目录作为构建目录，新脚本不再往其中输出缓存。

## 运行包清单

共 12 个文件；运行目录只保留 `platforms` 与 `imageformats` 两个插件子目录：

```text
gui/
  rov_ui.exe
  Qt5Core.dll
  Qt5Gui.dll
  Qt5Widgets.dll
  Qt5Network.dll
  Qt5Concurrent.dll
  libgcc_s_seh-1.dll
  libstdc++-6.dll
  libwinpthread-1.dll
  qt.conf
  platforms/qwindows.dll
  imageformats/qjpeg.dll
```

`qt.conf` 将插件路径固定为包内，防止本机安装的 Qt SDK 掩盖缺失插件。
不将 offscreen/minimal 测试插件、测试程序、源码、工具链、CMake/Ninja 缓存或历史数据装入包中。
解压 `build/gui.zip` 后直接运行其中的 `gui/rov_ui.exe`，无需安装 Qt 或 ROS。

## 可复现构建与打包

```powershell
# 默认 Release；主程序输出到 build/gui，缓存输出到 build/test/gui。
& .\tools\build-gui.ps1 -BuildType Release -RunTests
# 默认先构建和测试，再清理、打包和解压启动验证。
& .\tools\package-gui.ps1
# 刚完成上述构建与测试时，可跳过重复构建：
& .\tools\package-gui.ps1 -SkipBuild
```

打包脚本验证目标绝对路径、运行配置、重解析点和程序占用，先对运行文件和额外文件建
SHA-256 清单，再将额外内容移到 `build/test/package-时间/old-runtime-extras`。
每个归档文件和保留运行文件随后复核哈希；不覆盖源码，也不永久删除历史记录或现场证据。

ZIP 每个条目与解压文件分别核对哈希。启动验证使用已解压程序，清除工具链 PATH 和
外部 Qt 插件环境变量，使用 Windows 平台，验证真实主窗口及 JPEG 编解码往返。
启动验证显式关闭 MCU 自动连接，不连接 Nano、不发送控制、不生成相机预览替代实机图像。
启动结果与截图保存在 `build/test/package-时间/startup`，不污染包内目录。

## 2026-10-09 本轮结果

- 锁定 Qt 5.15.19 / MinGW 8.1.0 的 Release 构建成功。
- Release 配置中的 19 项离线回归测试全部通过。
- 主程序约 11.15 MiB；12 个运行文件组成的 ZIP 约 16.06 MiB。
- 从运行目录移出 793 个无关生成文件，约 679.45 MiB，保留在 `build/test` 的恢复归档。
- 旧 `build-test` 整体迁入 `build/test/previous-nano-tcp`，保留原测试证据；其中旧缓存仅作历史归档，
  不再作为构建入口。
- 最终 ZIP 条目、解压文件和独立 Windows 启动/JPEG 校验全部通过。
- 本轮 Nano 不在现场；真实图像、真实网络帧率、相机拔插及整机重启仍未验证。

最终包 SHA-256：

```text
F23F6D64801C35BA0D1F1B74E1EE0D0E6620F7B6ED6BF0ADBF1CAD6A2A089CE9
```

源码与旧 Debug 运行文件的预操作备份在 `build/test/backup-20261009-214430`；
缓存移出清单在 `build/test/package-20261009-215314`；
最终 12 文件包与解压验证在 `build/test/package-20261009-215501`。
所有生成物和备份均由 `.gitignore` 忽略，不随源码提交。
