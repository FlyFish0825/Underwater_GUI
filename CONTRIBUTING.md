# ROV Qt 上位机协作开发规范

本仓库使用 GitHub 作为唯一协作源。所有开发者和 Agent 开始任务前，必须先阅读：

1. `README.md`；
2. `agent/agent.md`；
3. `agent/ROV Qt 上位机集成版统一规范与Agent提示词 v2.1-integrated.md`；
4. `agent/ROV上位机通信协议与分层架构规范_v1.0.md`；
5. 对应页面的参考图。

## 分支规则

`main` 是稳定集成分支，不直接提交开发代码。每项工作从最新 `main` 创建独立分支：

~~~text
feature/dashboard-telemetry
feature/motor-debug-waveform
feature/firmware-page
fix/xxx
docs/xxx
refactor/xxx
~~~

开始工作：

~~~powershell
git switch main
git pull --ff-only origin main
git switch -c feature/your-task
~~~

分支只完成一个明确任务，不要把无关格式化、依赖升级或其他页面改动混进来。

## 提交与 Pull Request

提交前检查：

~~~powershell
git status
git diff --check
~~~

提交信息建议使用以下格式：

~~~text
feat: 增加 Dashboard 状态卡片
fix: 修复页面切换后的请求重复
docs: 更新协作说明
refactor: 提取公共状态标签
~~~

推送分支并创建 Pull Request：

~~~powershell
git push -u origin feature/your-task
~~~

Pull Request 必须说明：

- 完成了什么、修改了哪些文件；
- 对应页面、类名、CMake 目标和契约；
- 参考了哪张示意图；
- 如何构建和验证；
- 哪些平台、设备或真实通信尚未验证；
- 是否涉及公共接口、依赖、主题或目录归属；
- 是否需要集成人执行页面注册或冲突处理。

至少一名集成人审核通过后才能合并。合并后删除远程功能分支，并同步本地 `main`。

## 两人/多人分工

- 集成人：维护 `MainWindow`、导航、QStackedWidget、公共主题、公共控件、根 CMake、工具链和公共契约；负责最终集成。
- 页面负责人：只修改任务卡授权的 `src/pages/<page>`、`src/contracts/<page>` 和对应 Preview；不要直接修改通信层或主窗口。
- 通信/数据层负责人：遵守 `agent/ROV上位机通信协议与分层架构规范_v1.0.md`，保持 `Transport → Protocol → Service/Data → Contracts → UI` 分层，不让 UI 解析原始报文。
- Agent：严格使用 `agent/` 中的提示词和任务卡，交付时报告真实验证结果，不虚构实机联调。

同一页面尽量只安排一位负责人。公共接口、依赖、目录归属和全局样式变更必须先在 Pull Request 中说明，由集成人协调。

## 架构与代码底线

~~~text
UI → Service/Data → Protocol → Transport
~~~

页面只能通过有类型的 Snapshot 接收状态，通过有类型的 Request 信号输出用户意图。禁止出现：

- `Page → serial.write()`；
- `Page → CRC / AA55 / AA56`；
- `Protocol → QWidget`；
- 通信线程直接更新 QWidget；
- 用 0 代替未知或过期数据；
- 未经任务卡授权新增第三方库、QML、Qt Charts 或真实硬件功能。

## 构建目录和本地文件

以下目录和文件只保留在本机，禁止提交：

- `build/`；
- `toolchain/`；
- `logs/`、抓图、录制和测试输出；
- Qt Creator、VS Code、Visual Studio 的个人配置；
- 本机路径、密钥和环境变量。

如果文件已经被 Git 跟踪，`.gitignore` 不会自动取消跟踪。应先确认目标，再使用：

~~~powershell
git rm -r --cached build toolchain logs
~~~

这不会删除本地文件，只会从 Git 索引中移除；之后提交该变更即可。

## 合并冲突

遇到公共主题、契约、CMake 或协议规范冲突时：

1. 不要覆盖对方文件或强推远程分支；
2. 对照 `agent/` 中的统一规范确认生效规则；
3. 在 Pull Request 中列出冲突、影响文件和采用的方案；
4. 由集成人统一处理并在文档中记录。
