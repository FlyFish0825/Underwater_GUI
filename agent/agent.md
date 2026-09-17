你正在参与 ROV Qt 上位机项目。开始前必须阅读 AGENTS.md、docs/team-ui-guide.md、docs/toolchain.md、docs/ui-contract.md、当前工程、公共主题、对应参考图和本任务卡，并遵守《ROV Qt 上位机集成版统一规范与 Agent 提示词 v2.1-integrated》。

本项目只有一个 Qt Widgets MainWindow 和一个 QStackedWidget。参考图用于确定布局和信息层级，不逐像素复制，不自行增加未定义功能。当前默认只做 UI、页面契约、Preview、可重复演示数据和请求日志；不得实现真实设备通信、数据层、Nano 转发、STM32/网关驱动、重连、真实控制、烧录、视频或视觉算法，除非任务卡明确授权。

技术约束：Qt Widgets、C++17、Qt API >= 5.9；不使用 QML、WebEngine、Qt Charts、Qt 6 专属 API、std::filesystem、std::to_chars/from_chars。优先 QFile/QDir/QFileInfo、QPainter、Qt 资源系统和布局管理器。精确 Kit 由集成人锁定，不私自升级、降级或新增库。页面是独立 QWidget，命名空间 rov，构造函数为 explicit XxxPage(QWidget* parent = nullptr)，不重复创建主窗口、导航、顶栏或底栏。

输入使用 setSnapshot(const XxxSnapshot&)；契约值类型不得包含 QWidget、通信对象、包头、命令码或校验位。每字段写类型、单位、范围、有效性、新鲜度、业务对象和稳定 ID。目标值、请求、反馈分开；快照刷新不得误发信号，必要时使用 QSignalBlocker。控制只输出有类型请求，不做混控、PWM、QP、动力学、逆运动学、仲裁或看门狗；按住式控制覆盖释放、失焦、切页、禁用和销毁。

遵守目录归属、父子对象所有权、新式 connect、lambda 上下文、Allman 大括号、UTF-8/LF、.clang-format、资源路径和平台字体回退。不编辑 moc/uic/rcc，不用个人绝对路径，不把截图当 UI，不用 emoji 作为正式图标。所有 QWidget 更新在 GUI 线程。

Preview 数据必须固定可重复并标明演示/无真实设备连接，日志应可观察请求参数，波形和日志限制容量。先做最小可运行版本再完善。缺 Qt、缺设备或未能构建时如实报告，禁止虚构结果。交付必须列完成区域、文件、类/CMake/资源、契约、命令、真实验证、未验证平台、演示/占位内容、集成步骤、未决问题和冲突裁决。
请仔细阅读ROV上位机通信协议与分层架构规范_v1.0.md
ui设计要对比参考图的5个文件对应的你设计的部分
