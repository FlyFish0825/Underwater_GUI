#include "pages/dashboard/DashboardPage.h"
#include "pages/dashboard/SensorPanel.h"
#include <FluentQt/FluentQt.h>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <iostream>
using namespace rov;
static int failures=0, checks=0;
#define CHECK(x) do { ++checks; if(!(x)){ ++failures; std::cerr<<__LINE__<<": " #x "\n"; }}while(0)
int main(int argc,char **argv)
{
    fluent::prepareHighDpiApplication();
    QApplication app(argc,argv);
    fluent::initializeResources();
    QFile theme(QCoreApplication::applicationDirPath()+QStringLiteral("/../../resources/theme.qss"));
    if(theme.open(QIODevice::ReadOnly)) app.setStyleSheet(QString::fromUtf8(theme.readAll()));
    // Offscreen rendering does not always provide Windows' CJK fallback.
    app.setStyleSheet(app.styleSheet()+QStringLiteral("QWidget { font-family: 'Microsoft YaHei UI'; }"));
    // This fixture never constructs a transport or opens a COM port.
    QScrollArea scroll; scroll.setWidgetResizable(true); scroll.setFrameShape(QFrame::NoFrame);
    auto *page=new DashboardPage; scroll.setWidget(page); scroll.resize(1680,1050);
    scroll.show(); app.processEvents();
    auto *panel=page->findChild<SensorPanel *>(QStringLiteral("sensorPanel"));
    auto *toggle=page->findChild<QPushButton *>(QStringLiteral("dashboardSensorToggle"));
    auto *depthValue=page->findChild<QLabel *>(QStringLiteral("dashboardDepthValue"));
    auto *rollValue=page->findChild<QLabel *>(QStringLiteral("dashboardRollValue"));
    auto *yawValue=page->findChild<QLabel *>(QStringLiteral("dashboardYawValue"));
    auto *sources=page->findChild<QLabel *>(QStringLiteral("dashboardSensorSources"));
    CHECK(panel&&toggle&&depthValue&&rollValue&&yawValue&&sources);
    if(!(panel&&toggle&&depthValue&&rollValue&&yawValue&&sources)) return 1;
    CHECK(panel->parentWidget()==page);
    CHECK(panel->isHidden()&&!toggle->isChecked());
    CHECK(page->findChildren<QLabel *>(QStringLiteral("pageTitle")).size()==1);
    CHECK(depthValue->text()==QStringLiteral("--")&&rollValue->text()==QStringLiteral("--"));
    auto *raw=panel->findChild<QTableWidget *>(QStringLiteral("imuRawReadout"));
    auto *att=panel->findChild<QTableWidget *>(QStringLiteral("imuAttitudeReadout"));
    auto *dep=panel->findChild<QTableWidget *>(QStringLiteral("depthReadout"));
    auto *write=panel->findChild<QPushButton *>(QStringLiteral("sensor1Param0001Write"));
    auto *read=panel->findChild<QPushButton *>(QStringLiteral("sensor1Param0001Read"));
    auto *editor=panel->findChild<QSpinBox *>(QStringLiteral("sensor1Param0001Editor"));
    auto *info=panel->findChild<QPushButton *>(QStringLiteral("sensor1Command01"));
    auto *zero=panel->findChild<QPushButton *>(QStringLiteral("sensor2Command0c"));
    auto *save=panel->findChild<QPushButton *>(QStringLiteral("sensor1Command05"));
    auto *feedback=panel->findChild<QLabel *>(QStringLiteral("sensor1Param0001Feedback"));
    auto *tabs=panel->findChild<QTabWidget *>(QStringLiteral("sensorTabs"));
    CHECK(raw&&att&&dep&&write&&read&&editor&&info&&zero&&save&&feedback&&tabs);
    if(!(raw&&att&&dep&&write&&read&&editor&&info&&zero&&save&&feedback&&tabs)) return 1;
    CHECK(raw->item(0,1)->text()==QStringLiteral("--"));
    CHECK(!write->isEnabled()&&!info->isEnabled()&&!zero->isEnabled());
    int requests=0; SensorRequest observed;
    QObject::connect(page,&DashboardPage::sensorRequestIssued,[&](const SensorRequest &r){++requests;observed=r;});
    toggle->click(); app.processEvents(); CHECK(!panel->isHidden()&&toggle->isChecked()&&requests==0);
    SensorSnapshot s; s.connected=true; page->setSensorSnapshot(s);
    CHECK(info->isEnabled()&&!write->isEnabled());
    s.devices[0].infoKnown=true; s.devices[0].name=QStringLiteral("7E23 IMU (test fixture)");
    s.devices[0].capabilities=(1U<<5)|(1U<<7); s.devices[0].status=SensorStatus::PinBlocked;
    page->setSensorSnapshot(s); CHECK(!write->isEnabled()&&read->isEnabled()&&!save->isEnabled());
    CHECK(panel->findChild<QLabel *>(QStringLiteral("imuDeviceStatus"))->text().contains(QStringLiteral("PA9")));
    // Passive RX is independent of the configuration TX lock; motors may be offline.
    s.devices[0].status|=SensorStatus::Online|SensorStatus::RawValid|SensorStatus::EulerValid;
    s.devices[0].online=true; s.rawValid=true; s.rawAgeMs=20; s.attitudeAgeMs=20;
    s.accelG={-0.25,0,1}; s.gyroRadS={0.5,-0.5,2}; s.eulerValid=true; s.eulerDeg={1,2,180};
    page->setSensorSnapshot(s); CHECK(!write->isEnabled());
    CHECK(raw->item(0,1)->text()==QStringLiteral("-0.25000"));
    CHECK(att->item(2,1)->text()==QStringLiteral("180.000"));
    CHECK(rollValue->text()==QStringLiteral("1.0°")&&yawValue->text()==QStringLiteral("180.0°"));
    CHECK(sources->text().contains(QStringLiteral("IMU 测量")));
    page->setSnapshot(DashboardSnapshot{}); CHECK(yawValue->text()==QStringLiteral("180.0°")&&requests==0);
    s.devices[0].status&=~SensorStatus::PinBlocked; page->setSensorSnapshot(s); CHECK(write->isEnabled());
    editor->setValue(50); write->click();
    CHECK(requests==1&&observed.parameterId==1&&observed.value.toInt()==50&&observed.operation==SensorOperation::SetParameter);
    SensorParameterFeedback f; f.target=1; f.parameterId=1; f.value=25; f.confirmed=false;
    page->setSensorParameterFeedback(f);
    CHECK(editor->value()==50&&feedback->text().contains(QStringLiteral("未确认")));
    f.confirmed=true; page->setSensorParameterFeedback(f); CHECK(feedback->text().contains(QStringLiteral("设备确认")));
    s.devices[0].pending=true; page->setSensorSnapshot(s); CHECK(!write->isEnabled()&&!editor->isEnabled());
    s.devices[1].infoKnown=true; s.devices[1].name=QStringLiteral("MS5837 (test fixture)");
    s.devices[1].capabilities=(1U<<10)|(1U<<11)|(1U<<5); s.devices[1].online=true;
    s.devices[1].status=SensorStatus::Online|SensorStatus::PromValid;
    s.depthAgeMs=10; s.rawAdcD1=12345; s.rawAdcD2=56789; page->setSensorSnapshot(s);
    CHECK(!zero->isEnabled()&&depthValue->text()==QStringLiteral("--"));
    CHECK(dep->item(0,1)->text()==QStringLiteral("--")&&dep->item(5,1)->text()==QStringLiteral("12345"));
    CHECK(sources->text().contains(QStringLiteral("型号未确认")));
    s.devices[1].status|=SensorStatus::ModelConfirmed; page->setSensorSnapshot(s);
    CHECK(sources->text().contains(QStringLiteral("水面零点")));
    s.pressureValid=s.temperatureValid=s.depthValid=s.zeroValid=true;
    s.devices[1].status|=SensorStatus::PressureValid|SensorStatus::TemperatureValid|SensorStatus::DepthValid|SensorStatus::ZeroValid;
    s.pressurePa=121325; s.temperatureC=22.51; s.depthRawM=-0.001; s.depthFilteredM=2.0312; s.surfacePressurePa=101325;
    page->setSensorSnapshot(s); CHECK(zero->isEnabled());
    CHECK(dep->item(2,1)->text()==QStringLiteral("-0.0010")&&dep->item(3,1)->text()==QStringLiteral("2.0312"));
    CHECK(depthValue->text()==QStringLiteral("2.03 m")&&depthValue->toolTip().contains(QStringLiteral("深度计")));
    // Hiding or switching a tab must not send STOP_STREAM or destroy edited values.
    tabs->setCurrentIndex(1); toggle->click(); app.processEvents();
    CHECK(panel->isHidden()&&depthValue->text()==QStringLiteral("2.03 m")&&requests==1);
    const int collapsedHeight=page->minimumSizeHint().height();
    const QString folder=QCoreApplication::applicationDirPath()+QStringLiteral("/sensor-test-artifacts");
    QDir().mkpath(folder); CHECK(page->grab().save(folder+QStringLiteral("/dashboard-collapsed-fixture.png")));
    toggle->click(); app.processEvents();
    CHECK(!panel->isHidden()&&tabs->currentIndex()==1&&editor->value()==50&&requests==1);
    CHECK(page->minimumSizeHint().height()>collapsedHeight&&scroll.verticalScrollBar()->maximum()>0);
    CHECK(panel->grab().save(folder+QStringLiteral("/depth-fixture.png")));
    tabs->setCurrentIndex(0); app.processEvents(); CHECK(panel->grab().save(folder+QStringLiteral("/imu-fixture.png")));
    CHECK(page->grab().save(folder+QStringLiteral("/dashboard-expanded-fixture.png")));
    s.eulerValid=s.depthValid=false; page->setSensorSnapshot(s);
    CHECK(depthValue->text()==QStringLiteral("--")&&rollValue->text()==QStringLiteral("--"));
    s=SensorSnapshot{}; page->setSensorSnapshot(s);
    CHECK(!write->isEnabled()&&feedback->text()==QStringLiteral("未读取"));
    CHECK(raw->item(0,1)->text()==QStringLiteral("--")&&dep->item(0,1)->text()==QStringLiteral("--"));
    CHECK(requests==1);
    auto *collapse=panel->findChild<QPushButton *>(QStringLiteral("sensorPanelCollapse"));
    CHECK(collapse!=nullptr);
    if(collapse) collapse->click();
    CHECK(panel->isHidden()&&!toggle->isChecked()&&requests==1);
    std::cout<<"dashboard sensor integration: "<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
