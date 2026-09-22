# Third-party dependencies

项目实际使用的完整依赖清单见 [`../docs/开发说明.md`](../docs/开发说明.md)。
本目录只放第三方源码或其可追溯构建入口；不要把 `build/` 下的编译产物复制回源码
目录，也不要把调研中未采用的库加入 CMake。

## Fluent-Qt

- Source: https://github.com/calvinhxx/Fluent-QT
- Pinned tag: `v1.8.4`
- Resolved commit: `fb9e6194ae8eaea1a29ed077e4d3b9bf48410223`
- License: MIT（以随附的上游 `LICENSE` 为准）
- Integration: CMake `add_subdirectory(third_party/Fluent-Qt EXCLUDE_FROM_ALL)`
- Enabled scope: Qt Widgets library only; examples, gallery, tests, bindings and install targets are disabled.

应用层只通过 `src/ui/common/App*.h` 薄封装使用控件库。目前已替换可保持原 Qt 信号槽契约的按钮、复选框、滑块、下拉框、单行输入框和线性进度条；进度环、通知条和切换开关也已准备好供页面使用。

现有导航动画、窗口标题栏、数值框、表格数据模型、页面业务、通信协议和全局 QSS 不在本次控件替换中改写。

## Qwt 6.3.x

- Source: https://sourceforge.net/projects/qwt/files/qwt/6.3.0/
- Resolved source branch: `qwt-6.3` (QWT_VERSION_STR `6.3.1`)
- Resolved source commit: `92dc4d1065751cbebcb92231e18359ec28437af9`
- License: Qwt License 1.0（随附 `qwt-6.3/COPYING`）
- Integration: `third_party/qwt-6.3/CMakeLists.txt`，构建 `rov_third_party_qwt`
- Enabled scope: QWidget plot/curve/picker/zoomer/direct painter；SVG、OpenGL canvas、Polar、Designer、示例和测试不参与本工程目标。
- Qwt 以静态库链接到 `rov_page_motor_debug`，因此运行目录不需要额外的 Qwt DLL；
  重新配置工程即可从已提交的上游源码复现构建。
