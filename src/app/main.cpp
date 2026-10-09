#include "app/MainWindow.h"

#include <FluentQt/FluentQt.h>

#include <QApplication>
#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QDir>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTimer>

#ifdef Q_OS_WIN
#include <QWinEventNotifier>
#include <qt_windows.h>

namespace
{

constexpr wchar_t kInstanceMutex[] = L"Local\\UnderwaterGUI.SingleInstance.v1";
constexpr wchar_t kActivationEvent[] = L"Local\\UnderwaterGUI.Activate.v1";
constexpr wchar_t kWindowProperty[] = L"UnderwaterGUI.MainWindow.v1";

// 在创建QApplication/MainWindow前取得所有权，避免第二份程序初始化串口服务。
// 使用Windows内核互斥量，异常退出也会自动释放，不留下需要手动删除的锁文件。
class SingleInstance final
{
  public:
    SingleInstance()
    {
        m_mutex = CreateMutexW(nullptr, FALSE, kInstanceMutex);
        m_activation = CreateEventW(nullptr, TRUE, FALSE, kActivationEvent);
        if (m_mutex == nullptr || m_activation == nullptr)
            return;
        const DWORD result = WaitForSingleObject(m_mutex, 0);
        m_primary = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
        m_valid = m_primary || result == WAIT_TIMEOUT;
        if (m_primary)
            ResetEvent(m_activation);
    }

    ~SingleInstance()
    {
        if (m_activation != nullptr) CloseHandle(m_activation);
        if (m_primary) ReleaseMutex(m_mutex);
        if (m_mutex != nullptr) CloseHandle(m_mutex);
    }
    SingleInstance(const SingleInstance &) = delete;
    SingleInstance &operator=(const SingleInstance &) = delete;

    bool valid() const { return m_valid; }
    bool primary() const { return m_primary; }
    HANDLE activationEvent() const { return m_activation; }

    void activateExisting() const
    {
        // 把用户本次启动带来的前台权限交给已有窗口所属进程。
        EnumWindows([](HWND window, LPARAM) -> BOOL {
            if (GetPropW(window, kWindowProperty) == nullptr) return TRUE;
            DWORD processId = 0;
            GetWindowThreadProcessId(window, &processId);
            AllowSetForegroundWindow(processId);
            return FALSE;
        }, 0);
        // 手动复位事件会保留请求，首个进程尚在加载窗口时也不会丢失。
        SetEvent(m_activation);
    }

  private:
    HANDLE m_mutex = nullptr;
    HANDLE m_activation = nullptr;
    bool m_primary = false;
    bool m_valid = false;
};

} // namespace
#endif

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    SingleInstance instance;
    if (!instance.valid())
    {
        MessageBoxW(nullptr, L"无法建立程序单实例保护，请稍后重试。",
                    L"水下机器人上位机", MB_OK | MB_ICONERROR);
        return 1;
    }
    if (!instance.primary())
    {
        instance.activateExisting();
        return 0;
    }
#endif

    // Fluent-Qt 要求在 QApplication 创建前配置 High-DPI；这与现有的
    // PassThrough 策略一致，因此只复用配置，不改变现有窗口比例行为。
    fluent::prepareHighDpiApplication();
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setOrganizationName(QStringLiteral("UnderwaterGUI"));
    QCoreApplication::setApplicationName(QStringLiteral("UnderwaterGUI"));
    QApplication application(argc, argv);
    application.setWindowIcon(QIcon(QStringLiteral(":/icons/project_logo.png")));
    fluent::initializeResources();

    QFile theme(QStringLiteral(":/theme/theme.qss"));
    if (theme.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        application.setStyleSheet(QString::fromUtf8(theme.readAll()));
    }

    const QStringList arguments = application.arguments();
    const int smokeIndex = arguments.indexOf(QStringLiteral("--smoke-test"));
    const bool smokeTest = smokeIndex >= 0;
    if (smokeTest && smokeIndex + 1 >= arguments.size()) return 2;
    rov::MainWindow window(nullptr, !smokeTest);
    if (smokeTest)
    {
        window.setAttribute(Qt::WA_DontShowOnScreen);
        window.setPageIndex(4);
    }
    for (int i = 1; i < argc - 1; ++i)
    {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--window-size"))
        {
            const QStringList parts = QString::fromLocal8Bit(argv[i + 1]).split('x');
            if (parts.size() == 2)
            {
                window.resize(parts.at(0).toInt(), parts.at(1).toInt());
            }
            break;
        }
    }
    window.show();
    if (smokeTest)
    {
        // Opt-in package check: no automatic MCU connection and no fake camera preview.
        const QString outputDirectory = arguments.at(smokeIndex + 1);
        const int jpegIndex = arguments.indexOf(QStringLiteral("--smoke-jpeg"));
        const QString jpegPath = jpegIndex >= 0 && jpegIndex + 1 < arguments.size()
                                     ? arguments.at(jpegIndex + 1) : QString();
        QTimer::singleShot(1000, &window, [&, outputDirectory, jpegPath] {
            QImage jpeg;
            if (jpegPath.isEmpty())
            {
                QImage probe(2, 1, QImage::Format_RGB32);
                probe.fill(Qt::black);
                QByteArray encoded;
                QBuffer buffer(&encoded);
                buffer.open(QIODevice::WriteOnly);
                if (probe.save(&buffer, "JPEG")) jpeg = QImage::fromData(encoded, "JPEG");
            }
            else jpeg.load(jpegPath);
            const bool directoryOk = QDir().mkpath(outputDirectory);
            const bool screenshotOk = directoryOk && window.grab().save(
                QDir(outputDirectory).filePath(QStringLiteral("startup.png")));
            const bool ok = screenshotOk && !jpeg.isNull();
            const QJsonObject report{{QStringLiteral("started"), true},
                                     {QStringLiteral("platform"), QGuiApplication::platformName()},
                                     {QStringLiteral("jpegDecoded"), !jpeg.isNull()},
                                     {QStringLiteral("jpegWidth"), jpeg.width()},
                                     {QStringLiteral("jpegHeight"), jpeg.height()},
                                     {QStringLiteral("screenshotSaved"), screenshotOk},
                                     {QStringLiteral("automaticDeviceConnection"), false},
                                     {QStringLiteral("passed"), ok}};
            QFile output(QDir(outputDirectory).filePath(QStringLiteral("startup-result.json")));
            const QByteArray json = QJsonDocument(report).toJson();
            const bool reportOk = output.open(QIODevice::WriteOnly) && output.write(json) == json.size();
            output.close();
            window.close();
            application.exit(ok && reportOk ? 0 : 2);
        });
    }

#ifdef Q_OS_WIN
    const HWND mainHandle = reinterpret_cast<HWND>(window.winId());
    SetPropW(mainHandle, kWindowProperty, reinterpret_cast<HANDLE>(1));
    QWinEventNotifier activation(instance.activationEvent());
    QObject::connect(&activation, &QWinEventNotifier::activated, &window, [&]() {
        ResetEvent(instance.activationEvent());
        if (window.isMinimized())
            window.setWindowState(window.windowState() & ~Qt::WindowMinimized);
        window.show();
        window.raise();
        QWidget *target = QApplication::activeModalWidget();
        if (target == nullptr) target = &window;
        target->activateWindow();
        SetForegroundWindow(reinterpret_cast<HWND>(target->winId()));
    });
#endif

    const int result = application.exec();
#ifdef Q_OS_WIN
    activation.setEnabled(false);
    RemovePropW(mainHandle, kWindowProperty);
#endif
    return result;
}
