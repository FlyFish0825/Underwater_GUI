#pragma once

#include <QMainWindow>

class QLabel;
class QButtonGroup;
class QGraphicsProxyWidget;
class QGraphicsScene;
class QGraphicsView;
class QCloseEvent;
class QResizeEvent;
class QShowEvent;
class QStackedWidget;

namespace rov
{

class DashboardPage;
class MotorDebugPage;
class FirmwarePage;
class ManipulatorPage;
class VisionPage;
class ObserverMotorDataService;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

  public:
    explicit MainWindow(QWidget *parent = nullptr);

    void setPageIndex(int index);

  private:
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, long *result) override;
    void updatePageViewport();
    void fitNormalGeometryToScreen();
    void handleRequest(const QString &message);
    void selectPage(int index);

    QStackedWidget *m_pages = nullptr;
    QGraphicsView *m_pageView = nullptr;
    QGraphicsScene *m_pageScene = nullptr;
    QGraphicsProxyWidget *m_pageProxy = nullptr;
    QButtonGroup *m_navGroup = nullptr;
    QLabel *m_footerStatus = nullptr;
    QLabel *m_footerLog = nullptr;
    QLabel *m_gatewayStatus = nullptr;
    ObserverMotorDataService *m_motorData = nullptr;
    quint16 m_motorControlSequence = 0;
    bool m_screenSignalConnected = false;
};

} // namespace rov
