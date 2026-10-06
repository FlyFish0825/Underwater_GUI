#include "pages/dashboard/DashboardPage.h"
#include "pages/dashboard/SensorPanel.h"
#include <FluentQt/FluentQt.h>
#include <QApplication>
#include <QDir>
#include <QComboBox>
#include <QMessageBox>
#include <QTimer>
#include <QDoubleSpinBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
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
    CHECK(sources->isHidden()); // Requested diagnostic strip is absent from overview layout.
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
    s.devices[1].status=SensorStatus::Online|SensorStatus::PromValid|SensorStatus::RawValid;
    s.depthRawValid=true;
    s.depthAgeMs=10; s.rawAdcD1=12345; s.rawAdcD2=56789;
    s.devices[1].statusKnown=true; s.devices[1].sampleSequence=45475; s.devices[1].goodFrames=45475;
    s.devices[1].sampleAgeMs=30; s.devices[1].sequence=861; s.depthTimestampUs=1818405000U;
    s.devices[1].model=2; page->setSensorSnapshot(s);
    auto *depthRate=panel->findChild<QSpinBox *>(QStringLiteral("sensor2Param0001Editor"));
    auto *density=panel->findChild<QDoubleSpinBox *>(QStringLiteral("sensor2Param0102Editor"));
    auto *filter=panel->findChild<QDoubleSpinBox *>(QStringLiteral("sensor2Param0104Editor"));
    CHECK(depthRate && depthRate->minimum()==1 && depthRate->maximum()==45);
    CHECK(density && density->value()==1029 && filter && filter->value()==0);
    CHECK(panel->findChild<QLabel *>(QStringLiteral("depthDeviceInfo"))->text().contains(QStringLiteral("MS5837-02BA")));
    const auto statusText=panel->findChild<QLabel *>(QStringLiteral("depthDeviceStatus"))->text();
    CHECK(statusText.contains(QStringLiteral("45475")) && statusText.contains(QStringLiteral("1818405000")));
    CHECK(statusText.contains(QStringLiteral("861")) && statusText.contains(QStringLiteral("30 ms")));
    for(const QString &command : {QStringLiteral("05"),QStringLiteral("06"),QStringLiteral("09"),QStringLiteral("0a"),QStringLiteral("0b")})
        CHECK(!panel->findChild<QPushButton *>(QStringLiteral("sensor2Command")+command));
    CHECK(dep->rowCount()==7 && tabs->count()==2); // keep the existing table and tabs
    s.depthRawValid=false; page->setSensorSnapshot(s);
    CHECK(dep->item(5,1)->text()==QStringLiteral("--") && dep->item(6,1)->text()==QStringLiteral("--"));
    s.depthRawValid=true; page->setSensorSnapshot(s);
    CHECK(!zero->isEnabled()&&depthValue->text()==QStringLiteral("--"));
    CHECK(dep->item(0,1)->text()==QStringLiteral("--")&&dep->item(5,1)->text()==QStringLiteral("12345"));
    CHECK(sources->text().contains(QStringLiteral("型号未确认")));
    s.devices[1].status|=SensorStatus::ModelConfirmed; page->setSensorSnapshot(s);
    CHECK(sources->text().contains(QStringLiteral("水面零点")));
    s.pressureValid=s.temperatureValid=s.depthValid=s.zeroValid=s.surfacePressureValid=true;
    s.devices[1].status|=SensorStatus::PressureValid|SensorStatus::TemperatureValid|SensorStatus::DepthValid|SensorStatus::ZeroValid;
    s.pressurePa=121325; s.temperatureC=22.51; s.depthRawM=-0.001; s.depthFilteredM=2.0312; s.surfacePressurePa=101325;
    page->setSensorSnapshot(s); CHECK(zero->isEnabled());
    CHECK(dep->item(2,1)->text()==QStringLiteral("-0.0010")&&dep->item(3,1)->text()==QStringLiteral("2.0312"));
    CHECK(depthValue->text()==QStringLiteral("2.03 m")&&depthValue->toolTip().contains(QStringLiteral("深度计")));
    CHECK(sources->isHidden()); // Neither real-data updates nor motor refresh may reveal it again.
    CHECK(sources->text().contains(QStringLiteral("121325.0 Pa")) && sources->text().contains(QStringLiteral("22.51 °C")));
    CHECK(sources->text().contains(QStringLiteral("1818405000")));
    CHECK(dep->item(4,1)->text()==QStringLiteral("101325.0"));
    s.surfacePressureValid=false; page->setSensorSnapshot(s);
    CHECK(dep->item(4,1)->text()==QStringLiteral("--") && zero->isEnabled());
    s.surfacePressureValid=true; page->setSensorSnapshot(s);
    CHECK(dep->item(4,1)->text()==QStringLiteral("101325.0"));

    // All existing readouts must become unavailable when the sample is stale, even if status stays online.
    s.depthAgeMs=2600; s.pressureValid=s.temperatureValid=s.depthValid=false; page->setSensorSnapshot(s);
    for(int row=0;row<7;++row) CHECK(dep->item(row,1)->text()==QStringLiteral("--"));
    CHECK(depthValue->text()==QStringLiteral("--") && !zero->isEnabled());
    s.depthAgeMs=10; s.pressureValid=s.temperatureValid=s.depthValid=true;
    s.depthFilteredM=0; page->setSensorSnapshot(s); CHECK(depthValue->text()==QStringLiteral("0.00 m"));
    s.depthFilteredM=2.0312; page->setSensorSnapshot(s);
    SensorParameterFeedback notReady; notReady.target=kDepthSensor; notReady.parameterId=0x0103;
    notReady.message=QStringLiteral("未采集水面零点（NOT_READY，通信正常）");
    page->setSensorParameterFeedback(notReady);
    const auto *zeroFeedback=panel->findChild<QLabel *>(QStringLiteral("sensor2Param0103Feedback"));
    CHECK(zeroFeedback && zeroFeedback->text().contains(QStringLiteral("NOT_READY")));
    CHECK(zeroFeedback && !zeroFeedback->text().contains(QStringLiteral("设备确认")));
    s.devices[1].sampleAgeMs=-1; page->setSensorSnapshot(s);
    CHECK(panel->findChild<QLabel *>(QStringLiteral("depthDeviceStatus"))->text().contains(QStringLiteral("设备尚无样本")));
    s.devices[1].sampleAgeMs=30; page->setSensorSnapshot(s);
    // Restore a coherent fixture for screenshots after checking the NOT_READY presentation.
    notReady.value=101325.0; notReady.confirmed=true; notReady.message.clear();
    page->setSensorParameterFeedback(notReady);
    CHECK(zeroFeedback->text().contains(QStringLiteral("设备确认")));
    auto *depthInfoRead=panel->findChild<QPushButton *>(QStringLiteral("sensor2Command01"));
    CHECK(depthInfoRead && depthInfoRead->toolTip().contains(QStringLiteral("只读")));
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

    // Fixed probe model uses the same row and typed request, without an editable selector.
    {
        DashboardPage fixedPage;
        SensorSnapshot live; live.connected = true;
        live.devices[1].infoKnown = true; live.devices[1].capabilities = 1U << 11;
        live.devices[1].model = 30; // A real mismatch must not be disguised as confirmed 02BA.
        live.devices[1].lastRequestHex = QStringLiteral("AA 5B 01 04 01 02");
        live.devices[1].lastReplyHex = QStringLiteral("AA 5B 01 44 06 02");
        fixedPage.setSensorSnapshot(live);
        auto *fixedModel = fixedPage.findChild<QLabel *>(QStringLiteral("sensor2Param0105Editor"));
        auto *applyModel = fixedPage.findChild<QPushButton *>(QStringLiteral("sensor2Param0105Write"));
        auto *commandLabel = fixedPage.findChild<QLabel *>(QStringLiteral("depthCommandResult"));
        CHECK(fixedModel && fixedModel->text() == QStringLiteral("MS5837-02BA（固定）"));
        CHECK(!fixedPage.findChild<QComboBox *>(QStringLiteral("sensor2Param0105Editor")));
        CHECK(fixedPage.findChild<QLabel *>(QStringLiteral("depthDeviceInfo"))->text().contains(QStringLiteral("MS5837-30BA")));
        CHECK(commandLabel && commandLabel->toolTip().contains(live.devices[1].lastRequestHex));
        CHECK(commandLabel && commandLabel->toolTip().contains(live.devices[1].lastReplyHex));
        int modelRequests = 0; SensorRequest fixedRequest;
        QObject::connect(&fixedPage, &DashboardPage::sensorRequestIssued,
                         [&](const SensorRequest &r) { ++modelRequests; fixedRequest = r; });
        CHECK(modelRequests == 0 && applyModel && applyModel->isEnabled());
        if (applyModel) {
            QTimer::singleShot(0, []() {
                if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                    box->done(QMessageBox::Yes);
            });
            applyModel->click();
        }
        CHECK(modelRequests == 1 && fixedRequest.target == kDepthSensor);
        CHECK(fixedRequest.operation == SensorOperation::SetParameter && fixedRequest.parameterId == 0x0105);
        CHECK(fixedRequest.value.toInt() == 2);
        live.devices[1].pending = true; fixedPage.setSensorSnapshot(live);
        CHECK(!applyModel->isEnabled() && fixedModel->text() == QStringLiteral("MS5837-02BA（固定）"));
    }


    // test-linked: the existing editors form one constrained draft, never a device write on edit.
    {
        DashboardPage linkedPage;
        SensorSnapshot live; live.connected = true;
        live.devices[1].infoKnown = true; live.devices[1].model = 2; live.devices[1].capabilities = 1U << 11;
        linkedPage.setSensorSnapshot(live);
        auto *rate = linkedPage.findChild<QSpinBox *>(QStringLiteral("sensor2Param0001Editor"));
        auto *osr = linkedPage.findChild<QComboBox *>(QStringLiteral("sensor2Param0101Editor"));
        auto *rateApply = linkedPage.findChild<QPushButton *>(QStringLiteral("sensor2Param0001Write"));
        auto *osrApply = linkedPage.findChild<QPushButton *>(QStringLiteral("sensor2Param0101Write"));
        auto *note = linkedPage.findChild<QLabel *>(QStringLiteral("depthSamplingConstraint"));
        CHECK(rate && osr && rateApply && osrApply && note);
        if (!(rate && osr && rateApply && osrApply && note)) return 1;
        CHECK(osr->count() == 6 && osr->currentData().toInt() == 4096 && rate->maximum() == 45);
        int calls = 0; SensorRequest observedPair;
        QObject::connect(&linkedPage, &DashboardPage::sensorRequestIssued,
            [&](const SensorRequest &r) { ++calls; observedPair = r; });
        const int choices[] = {256,512,1024,2048,4096,8192};
        const int maxima[] = {100,100,100,71,45,25};
        for (int n = 0; n < 6; ++n)
        {
            osr->setCurrentIndex(osr->findData(choices[n]));
            CHECK(rate->minimum() == 1 && rate->maximum() == maxima[n]);
            rate->setValue(maxima[n]+1); CHECK(rate->value() == maxima[n]);
            rate->setValue(0); CHECK(rate->value() == 1);
            CHECK(note->text().contains(QString::number(maxima[n])) && calls == 0);
        }
        osr->setCurrentIndex(osr->findData(512)); rate->setValue(100);
        osr->setCurrentIndex(osr->findData(8192));
        CHECK(rate->value() == 25 && note->text().contains(QStringLiteral("自动降到")));
        osr->setCurrentIndex(osr->findData(4096));
        auto *entry = rate->findChild<QLineEdit *>(); CHECK(entry != nullptr);
        if (entry) { entry->setText(QStringLiteral("100")); rate->interpretText(); }
        CHECK(rate->value() >= 1 && rate->value() <= 45 && calls == 0);
        osr->setCurrentIndex(osr->findData(512)); rate->setValue(100);
        // Readback values never relabel the selected draft OSR as already applied.
        SensorParameterFeedback f; f.target = kDepthSensor; f.parameterId = 0x0101; f.value = 4096; f.confirmed = true;
        linkedPage.setSensorParameterFeedback(f); linkedPage.setSensorSnapshot(live);
        CHECK(osr->currentData().toInt() == 512 && rate->value() == 100 && rate->maximum() == 100);
        CHECK(rateApply->text() == QStringLiteral("应用组合") && osrApply->text() == QStringLiteral("应用组合"));
        rateApply->click();
        CHECK(calls == 1 && observedPair.parameterId == 0x0001 && observedPair.value.toInt() == 100);
        CHECK(observedPair.samplingOsr == 512 && observedPair.samplingRateHz == 100 && observedPair.target == kDepthSensor);
        osrApply->click();
        CHECK(calls == 2 && observedPair.parameterId == 0x0101 && observedPair.value.toInt() == 512);
        CHECK(observedPair.samplingOsr == 512 && observedPair.samplingRateHz == 100);
        live.devices[1].pending = true; linkedPage.setSensorSnapshot(live);
        CHECK(!rateApply->isEnabled() && !osrApply->isEnabled() && !rate->isEnabled() && !osr->isEnabled());
        rateApply->click(); osrApply->click(); CHECK(calls == 2);
        live.connected = false; live.devices[1].pending = false; linkedPage.setSensorSnapshot(live);
        CHECK(!rateApply->isEnabled() && !osrApply->isEnabled());
        CHECK(rate->value() == 100 && osr->currentData().toInt() == 512 && calls == 2);
    }

    std::cout<<"dashboard sensor integration: "<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
