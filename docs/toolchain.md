# ROV-UI Windows toolchain

This is the Windows upper-computer environment only. Nano, Ubuntu, ARM64,
JetPack, MCU and device-side toolchains are intentionally out of scope.

第三方库清单（Qt、Fluent-Qt、Qwt 的版本、许可证和源码位置）见
[`dependencies.md`](dependencies.md)；本文只记录编译器、构建工具和 Qt SDK。

## Locked versions

| Component | Version | Installed location |
|---|---:|---|
| UI | Qt Widgets | `toolchain/qt/5.15.19` |
| Qt | 5.15.19 | `toolchain/qt/5.15.19` |
| C++ compiler | MinGW-w64 GCC/G++ 8.1.0 POSIX SEH x64 | `toolchain/mingw/8.1.0/Tools/mingw810_64` |
| CMake | 3.28.6 | `toolchain/cmake/3.28.6/cmake-3.28.6-windows-x86_64` |
| Ninja | 1.11.1 | `toolchain/ninja/1.11.1` |
| clangd/clang-format | 14.0.6 | `toolchain/llvm/14.0.6` |
| Qt Creator | Not installed by request | — |

Build-time utilities installed on Windows are 7-Zip and Strawberry Perl.
Qwt is statically linked into the plotting page; there is no separate Qwt DLL
to deploy. The project uses Qt Core, Gui, Widgets, Concurrent, PrintSupport and
Qt's Windows platform plugin.

## Activation

PowerShell:

```powershell
. 'F:\file\BaiduSyncdisk\Project\Underwater_GUI\toolchain\env\activate.ps1'
```

Command Prompt:

```bat
call F:\file\BaiduSyncdisk\Project\Underwater_GUI\toolchain\env\activate.cmd
```

The activation scripts put the fixed tools before the machine's existing
CMake 4.x, Ninja 1.13.x, GCC 12.x and the w64devkit shell. The Qt build was
verified with Git's `sh.exe` because the w64devkit shell mishandles `F:` paths.

## Verification

```powershell
qmake --version
qmake -query QT_VERSION
cmake --version
ninja --version
gcc --version
g++ --version
clang-format --version
clangd --version
```

Expected project versions are Qt 5.15.19, CMake 3.28.6, Ninja 1.11.1,
GCC/G++ 8.1.0, clangd 14.0.6 and clang-format 14.0.6.

The project also requires clangd 14.0.6 for the configured VSCode language
server. CMake exports `build/gui/compile_commands.json`; the query driver must
point at the project MinGW 8.1.0 bin directory so clangd does not select MSVC
headers.

## Qt build record

Qt was built from the official `qtbase-everywhere-opensource-src-5.15.19`
source archive with the Windows MinGW kit, release configuration, no examples
and no tests. The Qt build and install both completed successfully. The source
tree and build tree are retained under `build/` for reproducibility; the
installed SDK is under `toolchain/qt/5.15.19`.

## Runtime deployment

This source build does not include Qt Creator or `windeployqt`. Use
`toolchain/env/deploy-qt.ps1` for a release executable. It copies the Qt
Core/Gui/Widgets DLLs, the MinGW runtime DLLs, and the Windows/offscreen Qt
platform plugins beside the executable, so the program can be started from
Explorer without first activating a shell.

The deployment smoke test completed successfully: the test executable starts
with a clean PATH and `-platform offscreen`, and the default Windows platform
also initializes successfully.
