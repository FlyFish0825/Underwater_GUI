#pragma once

#include <QMainWindow>

class QLabel;
class QButtonGroup;
class QResizeEvent;
class QScrollArea;
class QStackedWidget;

namespace rov
{

class DashboardPage;
class MotorDebugPage;
class FirmwarePage;
class ManipulatorPage;
class VisionPage;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

  public:
    explicit MainWindow(QWidget *parent = nullptr);

    void setPageIndex(int index);

  private:
    void resizeEvent(QResizeEvent *event) override;
    void updatePageViewport();
    void handleRequest(const QString &message);
    void selectPage(int index);

    QStackedWidget *m_pages = nullptr;
    QScrollArea *m_pageScroll = nullptr;
    QButtonGroup *m_navGroup = nullptr;
    QLabel *m_footerStatus = nullptr;
    QLabel *m_footerLog = nullptr;
};

} // namespace rov
