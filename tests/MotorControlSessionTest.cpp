#include "pages/motor_debug/MotorDebugPage.h"
#include <FluentQt/FluentQt.h>
#include <QApplication>
#include <QPushButton>
#include <QSlider>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>
int main(int argc,char **argv)
{
    fluent::prepareHighDpiApplication();QApplication app(argc,argv);fluent::initializeResources();
    rov::MotorDebugPage page;int requests=0;
    QObject::connect(&page,&rov::MotorDebugPage::speedControlRequested,[&](const rov::MotorSpeedControlRequest &){++requests;});
    QPushButton *run=nullptr;for(auto *b:page.findChildren<QPushButton *>())if(b->text()==QStringLiteral("启动"))run=b;
    QSlider *speed=nullptr;for(auto *s:page.findChildren<QSlider *>())if(s->minimum()==-10000&&s->maximum()==10000)speed=s;
    if(!run||!speed)return 1;
    run->click();speed->setValue(1200);const int before=requests;
    page.resetControlSession();
    if(run->text()!=QStringLiteral("启动")||speed->value()!=0)return 1;
    QElapsedTimer t;t.start();while(t.elapsed()<150){app.processEvents();QThread::msleep(2);}
    if(requests!=before)return 1;
    speed->setValue(1300);t.restart();while(t.elapsed()<150){app.processEvents();QThread::msleep(2);}
    if(requests!=before)return 1;
    run->click();if(requests<=before)return 1;
    page.resetControlSession();
    std::cout<<"PASS motor session reset: no delayed or latched command after disconnect\n";return 0;
}
