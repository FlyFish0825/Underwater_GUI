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
    auto *feedback=panel->findChild<QLabel *>(QStringLiteral("sensor1Param0001Feedback"));
    auto *tabs=panel->findChild<QTabWidget *>(QStringLiteral("sensorTabs"));
    CHECK(raw&&att&&dep&&write&&read&&editor&&info&&zero&&feedback&&tabs);
    if(!(raw&&att&&dep&&write&&read&&editor&&info&&zero&&feedback&&tabs)) return 1;
    CHECK(raw->item(0,1)->text()==QStringLiteral("--"));
    CHECK(!write->isEnabled()&&!info->isEnabled()&&!zero->isEnabled());
    int requests=0; SensorRequest observed;
    QObject::connect(page,&DashboardPage::sensorRequestIssued,[&](const SensorRequest &r){++requests;observed=r;});
    toggle->click(); app.processEvents(); CHECK(!panel->isHidden()&&toggle->isChecked()&&requests==0);
    SensorSnapshot s; s.connected=true; page->setSensorSnapshot(s);
    CHECK(info->isEnabled()&&!write->isEnabled());
    s.devices[0].infoKnown=true; s.devices[0].name=QStringLiteral("7E23 IMU (test fixture)");
    s.devices[0].capabilities=(1U<<5)|(1U<<7); s.devices[0].status=SensorStatus::PinBlocked;
    page->setSensorSnapshot(s); CHECK(!write->isEnabled()&&read->isEnabled());
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
    f.confirmed=true; page->setSensorParameterFeedback(f); CHECK(!feedback->text().contains(QStringLiteral("设备确认"))); // IMU cache stays unconfirmed
    s.devices[0].pending=true; page->setSensorSnapshot(s); CHECK(!write->isEnabled()&&!editor->isEnabled());
    s.devices[1].infoKnown=true; s.devices[1].name=QStringLiteral("MS5837 (test fixture)");
    s.devices[1].capabilities=(1U<<10)|(1U<<5); s.devices[1].online=true;
    s.devices[1].status=SensorStatus::Online|SensorStatus::PromValid|SensorStatus::RawValid
        | (0x1EU << SensorStatus::LastI2cCommandShift) | (5U << SensorStatus::LastErrorShift);
    s.depthRawValid=true;
    s.depthAgeMs=10; s.rawAdcD1=12345; s.rawAdcD2=56789;
    s.devices[1].statusKnown=true; s.devices[1].sampleSequence=45475; s.devices[1].goodFrames=45475;
    s.devices[1].sampleAgeMs=30; s.devices[1].sequence=861; s.depthTimestampUs=1818405000U;
    page->setSensorSnapshot(s);
    auto *depthRate=panel->findChild<QSpinBox *>(QStringLiteral("sensor2Param0001Editor"));
    auto *density=panel->findChild<QDoubleSpinBox *>(QStringLiteral("sensor2Param0102Editor"));
    auto *filter=panel->findChild<QDoubleSpinBox *>(QStringLiteral("sensor2Param0104Editor"));
    CHECK(!panel->findChild<QDoubleSpinBox *>(QStringLiteral("sensor2Param0103Editor")));
    CHECK(depthRate && depthRate->minimum()==1 && depthRate->maximum()==45);
    CHECK(density && density->value()==1029 && filter && filter->value()==0);
    CHECK(panel->findChild<QLabel *>(QStringLiteral("depthDeviceInfo"))->text().contains(QStringLiteral("MS5837-02BA")));
    const auto statusText=panel->findChild<QLabel *>(QStringLiteral("depthDeviceStatus"))->text();
    CHECK(statusText.contains(QStringLiteral("45475")) && statusText.contains(QStringLiteral("1818405000")));
    CHECK(statusText.contains(QStringLiteral("861")) && statusText.contains(QStringLiteral("30 ms")));
    CHECK(statusText.contains(QStringLiteral("最近错误 5")) && statusText.contains(QStringLiteral("关联 I²C 命令 0x1E")));
    for(const QString &command : {QStringLiteral("05"),QStringLiteral("06"),QStringLiteral("09"),QStringLiteral("0a"),QStringLiteral("0b")})
        CHECK(!panel->findChild<QPushButton *>(QStringLiteral("sensor2Command")+command));
    CHECK(dep->rowCount()==7 && tabs->count()==2); // keep the existing table and tabs
    s.depthRawValid=false; page->setSensorSnapshot(s);
    CHECK(dep->item(5,1)->text()==QStringLiteral("--") && dep->item(6,1)->text()==QStringLiteral("--"));
    s.depthRawValid=true; page->setSensorSnapshot(s);
    CHECK(!zero->isEnabled()&&depthValue->text()==QStringLiteral("--"));
    CHECK(dep->item(0,1)->text()==QStringLiteral("--")&&dep->item(5,1)->text()==QStringLiteral("12345"));
    CHECK(sources->text().contains(QStringLiteral("压力数据无效")));
    s.pressureValid=true; page->setSensorSnapshot(s);
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
    s.devices[1].sampleAgeMs=-1; page->setSensorSnapshot(s);
    CHECK(panel->findChild<QLabel *>(QStringLiteral("depthDeviceStatus"))->text().contains(QStringLiteral("设备尚无样本")));
    s.devices[1].sampleAgeMs=30; page->setSensorSnapshot(s);
    // Restore a coherent fixture for screenshots after checking the NOT_READY presentation.
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

    // The fixed probe has no model query, confirmation, or write control.
    {
        DashboardPage fixedPage;
        SensorSnapshot live; live.connected = true;
        live.devices[1].infoKnown = true; live.devices[1].capabilities = 0;
        fixedPage.setSensorSnapshot(live);
        CHECK(fixedPage.findChild<QComboBox *>(QStringLiteral("sensor2Param0105Editor")) == nullptr);
        CHECK(fixedPage.findChild<QLabel *>(QStringLiteral("depthDeviceInfo"))->text().contains(QStringLiteral("MS5837-02BA")));
        int modelRequests = 0;
        QObject::connect(&fixedPage, &DashboardPage::sensorRequestIssued,
                         [&](const SensorRequest &) { ++modelRequests; });
        CHECK(modelRequests == 0);
        live.devices[1].pending = true; fixedPage.setSensorSnapshot(live);
        CHECK(modelRequests == 0);
    }


    // test-linked: the existing editors form one constrained draft, never a device write on edit.
    {
        DashboardPage linkedPage;
        SensorSnapshot live; live.connected = true;
        live.devices[1].infoKnown = true; live.devices[1].capabilities = 1U << 11;
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


    {
        DashboardPage imuPage;
        SensorSnapshot s; s.connected=true; auto &imu=s.devices[0];
        imu.infoKnown=true;imu.online=true;imu.capabilities=0xFFFFFFFFU;imu.model=0;
        imu.name=QStringLiteral("7E23 IMU (synthetic)");imu.firmware=QStringLiteral("1.0.0");
        imu.status=0x43F;s.rawValid=s.quaternionValid=s.eulerValid=true;
        s.rawAgeMs=10;s.quaternionAgeMs=15;s.attitudeAgeMs=20;
        s.imuRawRateHz=50.0;s.imuAttitudeRateHz=100.0;s.imuRateCheck=ImuRateCheck::Matches;
        s.imuRateMessage=QStringLiteral("观测一致：仅原始流验证，不是设备 ACK");
        imuPage.setSensorSnapshot(s);
        auto *config=imuPage.findChild<SensorPanel *>();
        auto *rate=config->findChild<QSpinBox *>(QStringLiteral("sensor1Param0001Editor"));
        auto *mode=config->findChild<QComboBox *>(QStringLiteral("sensor1Param0003Editor"));
        auto *writeRate=config->findChild<QPushButton *>(QStringLiteral("sensor1Param0001Write"));
        auto *writeMode=config->findChild<QPushButton *>(QStringLiteral("sensor1Param0003Write"));
        auto *rateStatus=config->findChild<QLabel *>(QStringLiteral("imuRateObservation"));
        CHECK(rate && rate->minimum()==10 && rate->maximum()==100);
        CHECK(mode && !mode->currentData().isValid() && mode->findData(6)>=0 && mode->findData(9)>=0);
        for(const QString &cmd:{QStringLiteral("05"),QStringLiteral("06"),QStringLiteral("09"),QStringLiteral("0a"),QStringLiteral("0b")})
            CHECK(!config->findChild<QPushButton *>(QStringLiteral("sensor1Command")+cmd));
        CHECK(rateStatus && rateStatus->text().contains(QStringLiteral("50.0 Hz")) && rateStatus->text().contains(QStringLiteral("100.0 Hz")));
        CHECK(config->findChild<QLabel *>(QStringLiteral("imuDeviceStatus"))->text().contains(QStringLiteral("四元数")));
        int commands=0;SensorRequest last;
        QObject::connect(&imuPage,&DashboardPage::sensorRequestIssued,[&](const SensorRequest &r){++commands;last=r;});
        writeMode->click();CHECK(commands==0); // placeholder is not an implicit 6-axis write
        rate->setValue(50);writeRate->click();CHECK(commands==1 && last.target==1 && last.parameterId==1 && last.value.toInt()==50);
        mode->setCurrentIndex(mode->findData(9));writeMode->click();CHECK(commands==2 && last.parameterId==3 && last.value.toInt()==9);
        SensorParameterFeedback f;f.target=1;f.parameterId=1;f.value=25;f.confirmed=false;
        f.message=QStringLiteral("最后下发缓存 / 未确认（非设备读回）");imuPage.setSensorParameterFeedback(f);
        CHECK(rate->value()==50 && config->findChild<QLabel *>(QStringLiteral("sensor1Param0001Feedback"))->text().contains(QStringLiteral("缓存")));
        imu.status|=SensorStatus::PinBlocked;imuPage.setSensorSnapshot(s);
        CHECK(!writeRate->isEnabled() && !writeMode->isEnabled());
        CHECK(config->findChild<QPushButton *>(QStringLiteral("sensor1Param0001Read"))->isEnabled());
        CHECK(config->findChild<QTableWidget *>(QStringLiteral("imuAttitudeReadout"))->item(0,1)->text()!=QStringLiteral("--"));
        CHECK(!imuPage.findChild<QLabel *>(QStringLiteral("dashboardSensorSources"))->isVisible());
        auto *tabs=config->findChild<QTabWidget *>(QStringLiteral("sensorTabs"));CHECK(tabs->count()==2);
        CHECK(config->findChild<QTableWidget *>(QStringLiteral("depthReadout"))->rowCount()==7);
        s.eulerValid=false;s.imuAttitudeRateHz=-1;imuPage.setSensorSnapshot(s);
        CHECK(config->findChild<QTableWidget *>(QStringLiteral("imuAttitudeReadout"))->item(0,1)->text()==QStringLiteral("--"));
        CHECK(config->findChild<QTableWidget *>(QStringLiteral("imuAttitudeReadout"))->item(3,1)->text()!=QStringLiteral("--"));
    }


    // Attachment policy wins over capability bits: no hardware-calibration/reset buttons.
    {
        DashboardPage imuPage;
        SensorSnapshot live; live.connected=true;
        auto &d=live.devices[0]; d.infoKnown=true; d.online=true; d.capabilities=0x1E7;
        d.status=0x43F; d.firmware=QStringLiteral("1.0.0"); d.name=QStringLiteral("7E23 IMU (synthetic)");
        live.rawValid=live.quaternionValid=live.eulerValid=true;
        live.rawAgeMs=live.quaternionAgeMs=live.attitudeAgeMs=10;
        live.quaternionWxyz={1,0,0,0}; live.eulerDeg={1,2,3};
        live.imuRawRateHz=25; live.imuAttitudeRateHz=50; live.imuRateMessage=QStringLiteral("观测一致，参数仍未确认");
        imuPage.setSensorSnapshot(live);
        for (const QString &cmd : {QStringLiteral("05"),QStringLiteral("06"),QStringLiteral("09"),QStringLiteral("0a"),QStringLiteral("0b")})
            CHECK(!imuPage.findChild<QPushButton *>(QStringLiteral("sensor1Command")+cmd));
        auto *choice=imuPage.findChild<QComboBox *>(QStringLiteral("sensor1Param0003Editor"));
        auto *writeMode=imuPage.findChild<QPushButton *>(QStringLiteral("sensor1Param0003Write"));
        auto *imuRates=imuPage.findChild<QLabel *>(QStringLiteral("imuRateObservation"));
        auto *imuValues=imuPage.findChild<QTableWidget *>(QStringLiteral("imuAttitudeReadout"));
        auto *rawValues=imuPage.findChild<QTableWidget *>(QStringLiteral("imuRawReadout"));
        CHECK(choice && writeMode && imuRates && imuValues && rawValues);
        if(choice && writeMode && imuRates && imuValues && rawValues) {
            CHECK(choice->count()==3 && !choice->currentData().isValid() && !writeMode->isEnabled());
            CHECK(imuRates->text().contains(QStringLiteral("25.0 Hz")) && imuRates->text().contains(QStringLiteral("50.0 Hz")));
            for(int i=6;i<9;++i) CHECK(rawValues->item(i,2)->text()==QStringLiteral("协议单位"));
            int requests=0; SensorRequest last;
            QObject::connect(&imuPage,&DashboardPage::sensorRequestIssued,[&](const SensorRequest &r){++requests;last=r;});
            choice->setCurrentIndex(choice->findData(9));
            CHECK(writeMode->isEnabled() && requests==0);
            writeMode->click(); CHECK(requests==1 && last.target==1 && last.parameterId==3 && last.value.toInt()==9);
            SensorParameterFeedback cache; cache.target=1; cache.parameterId=3; cache.value=6; cache.confirmed=true;
            imuPage.setSensorParameterFeedback(cache);
            CHECK(choice->currentData().toInt()==9);
            CHECK(!imuPage.findChild<QLabel *>(QStringLiteral("sensor1Param0003Feedback"))->text().contains(QStringLiteral("设备确认")));
            d.status|=SensorStatus::PinBlocked; imuPage.setSensorSnapshot(live);
            CHECK(!writeMode->isEnabled() && imuValues->item(0,1)->text()==QStringLiteral("1.000"));
            live.eulerValid=false; imuPage.setSensorSnapshot(live);
            CHECK(imuValues->item(0,1)->text()==QStringLiteral("--") && imuValues->item(3,1)->text()==QStringLiteral("1.000000"));
            live.quaternionValid=false; live.eulerValid=true; imuPage.setSensorSnapshot(live);
            CHECK(imuValues->item(0,1)->text()==QStringLiteral("1.000") && imuValues->item(3,1)->text()==QStringLiteral("--"));
            imuPage.setSensorSnapshot(SensorSnapshot{});
            CHECK(!writeMode->isEnabled() && requests==1);
        }
    }

    {
        DashboardPage streamPage;
        SensorSnapshot live; live.connected = true;
        live.devices[0].streamState = SensorStreamState::Stopped;
        live.devices[1].streamState = SensorStreamState::Running;
        streamPage.setSensorSnapshot(live);
        const auto *imu = streamPage.findChild<QLabel *>(QStringLiteral("imuDeviceStatus"));
        const auto *depth = streamPage.findChild<QLabel *>(QStringLiteral("depthDeviceStatus"));
        CHECK(imu && depth);
        if (imu && depth)
        {
            CHECK(imu->text().contains(QStringLiteral("测量上传已停止")));
            CHECK(depth->text().contains(QStringLiteral("上传已开启（设备确认）")));
            live.devices[0].streamState = SensorStreamState::Running;
            live.devices[1].streamState = SensorStreamState::Stopped;
            streamPage.setSensorSnapshot(live);
            CHECK(imu->text().contains(QStringLiteral("上传已开启（设备确认）")));
            CHECK(depth->text().contains(QStringLiteral("测量上传已停止")));
            streamPage.setSensorSnapshot(SensorSnapshot{});
            CHECK(imu->text().contains(QStringLiteral("上传开关尚未确认")));
            CHECK(depth->text().contains(QStringLiteral("上传开关尚未确认")));
        }
    }
    std::cout<<"dashboard sensor integration: "<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
