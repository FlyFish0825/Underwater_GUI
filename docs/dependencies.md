# 第三方依赖清单

本文件告诉后续开发者：项目实际使用了哪些外部组件、它们放在哪里、由谁构建、
使用什么许可证，以及哪些项目只是调研候选而没有接入。新增第三方依赖时必须同步
更新本文件和 `third_party/README.md`。

## 当前实际使用

| 组件 | 固定版本/提交 | 用途 | 来源与目录 | 许可证/注意事项 | 构建方式 |
| --- | --- | --- | --- | --- | --- |
| Qt | 5.15.19 | Qt Widgets、Core、Gui、Concurrent、PrintSupport | 锁定工具链 `toolchain/qt/5.15.19`，不随仓库提交 | 按 Qt 发行版和项目发布方式遵守对应许可 | `find_package(Qt5 5.15 ...)` |
| Fluent-Qt | v1.8.4 / `fb9e6194ae8eaea1a29ed077e4d3b9bf48410223` | 公共按钮、输入框、进度控件和基础 UI 封装 | `third_party/Fluent-Qt`；当前由本机依赖提供，根 `.gitignore` 排除 | MIT，许可证在上游 `LICENSE` | CMake 目标 `FluentQt::FluentQt`；示例、画廊、测试和绑定关闭 |
| Qwt | 6.3.1 / `92dc4d1065751cbebcb92231e18359ec28437af9` | Motor Debug 多曲线绘图、图例、缩放、平移和坐标跟踪 | 完整源码 `third_party/qwt-6.3` | Qwt License 1.0，保留 `third_party/qwt-6.3/COPYING` | CMake 目标 `rov_third_party_qwt`，静态链接到 `rov_page_motor_debug` |

Qt、MinGW、CMake、Ninja 和 LLVM/clangd 属于固定开发工具链，不要把本机安装目录
复制进仓库。工具链版本和激活方式见 [`toolchain.md`](toolchain.md)。

## Qwt 构建范围

Qwt 包装目标只启用 Qt Widgets 绘图所需的普通 Plot、Curve、Picker、Zoomer 和
DirectPainter 代码；SVG、OpenGL Canvas、Polar、Designer、示例和测试没有加入本工程
目标。Qwt 以静态库链接，因此运行目录不需要额外的 Qwt DLL。

第三方编译缓存可以保留在本机的 `build/gui/third_party` 和
`build/third_party-cache`，不要把这些生成物提交到 Git。清理中间文件时，不能删除
上述目录中的第三方库，否则下次构建会重新编译 Qwt 或 Fluent-Qt。

## 仅调研、没有接入

以下项目只用于绘图库选型比较，不是当前工程依赖：

- QCustomPlot 2.x：GPL-3.0，未接入；
- Qt Charts：GPL-3.0 或商业许可，未接入；
- ImPlot：MIT，但依赖 Dear ImGui，未接入。

详细比较和选择理由见 [`plotting.md`](plotting.md)。不要因为这些项目出现在调研表中，
就把它们写入 CMake、依赖安装脚本或发布清单。

## 新增依赖的检查项

新增库前至少记录：

1. 上游 URL、固定版本或提交号；
2. 用途、实际链接目标和是否静态/动态链接；
3. 许可证文件位置和发布义务；
4. 是否需要加入 `.gitignore` 例外；
5. 最小构建验证和运行时部署变化。
