# ROV Qt 上位机

这是水下机器人 ROV 的 Qt Widgets 上位机工程。当前阶段以 **UI、页面契约、可重复 Preview 和请求日志** 为主，暂不实现 MCU/Nano 驱动、串口、CAN、TCP/UDP、真实 Bootloader、真实电机控制、摄像头和视觉算法。

## 开始开发前必须阅读

后续所有开发者、Agent 和协作者，开始任务前必须先阅读 `agent` 目录中的统一规范：

1. `agent/agent.md`：当前任务执行规则和检查要求；
2. `agent/ROV Qt 上位机集成版统一规范与Agent提示词 v2.1-integrated.md`：集成后的总规范、集成人提示词和各页面 Agent 提示词；
3. `agent/ROV上位机通信协议与分层架构规范_v1.0.md`：Transport、Protocol、Service/Data、UI 的分层边界；
4. `agent/示意图/`：Dashboard、电机调试、Bootloader、机械臂和双目视觉参考图。

不得跳过上述文档自行建立另一套工程结构、主题、页面接口或通信方式。任务卡与规范冲突时，必须先报告冲突和影响范围。

## 工程结构

```text
src/app/             主窗口和应用入口
src/pages/           Dashboard、Motor Debug、Firmware、Manipulator、Vision、Settings
src/contracts/       页面快照和请求契约
src/communication/   通信分层占位及协议边界
src/data/            Service/Data 层占位
src/ui/              公共控件和主题
resources/           Qt 资源和统一 QSS
docs/                框架、工具链、契约文档
agent/               Agent 提示词、通信规范和参考图
```

依赖方向必须保持：

```text
UI → Service/Data → Protocol → Transport
```

页面不得直接访问串口、协议解析器或原始字节流；页面只接收 Snapshot，并输出类型明确的 Request。

## 编译与运行

使用项目已经锁定的工具链：

```powershell
. 'F:\file\BaiduSyncdisk\Project\Underwater_GUI\toolchain\env\activate.ps1'
& 'F:\file\BaiduSyncdisk\Project\Underwater_GUI\toolchain\env\build-gui.ps1'
```

编译结果位于 `build/gui/rov_ui.exe`。Qt 和 MinGW 运行库部署使用匹配当前工具链的 `deploy-qt.ps1`。

为了生成可重复的界面截图：

```powershell
& 'F:\file\BaiduSyncdisk\Project\Underwater_GUI\build\gui\rov_ui.exe' `
    --page 0 --capture 'F:\file\BaiduSyncdisk\Project\Underwater_GUI\build\gui\dashboard.png'
```

页面索引：0 Dashboard，1 Motor Debug，2 Firmware，3 Manipulator，4 Vision，5 Settings。

## Git 约定

`build/`、`toolchain/`、日志、缓存和编译产物只保留在本机，不提交到 Git。源码、文档、协议规范、资源和参考图可以提交。完整规则见根目录 `.gitignore`。

多人协作请先阅读 [CONTRIBUTING.md](CONTRIBUTING.md)。所有开发者和 Agent 都必须先阅读 `agent/` 目录中的统一提示词及通信分层规范，从 `main` 创建功能分支，通过 Pull Request 合并；不要直接向 `main` 推送。

## 相关文档

- `docs/framework.md`：工程框架和分层边界；
- `docs/ui-contract.md`：页面输入快照与输出请求契约；
- `docs/toolchain.md`：精确工具链和构建记录；
- `agent/`：后续开发必须遵守的统一 Agent 提示词、通信规范和参考图。
