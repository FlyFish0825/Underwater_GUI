#include "pages/firmware/FirmwarePage.h"

#include "preview/PreviewData.h"
#include "ui/common/UiPrimitives.h"

#include <QFileDialog>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace
{

QString firmwareStateText(const rov::FirmwareState state)
{
    switch (state)
    {
    case rov::FirmwareState::Completed:
        return QStringLiteral("成功");
    case rov::FirmwareState::Programming:
        return QStringLiteral("编程中");
    case rov::FirmwareState::Verifying:
        return QStringLiteral("校验中");
    case rov::FirmwareState::Failed:
        return QStringLiteral("失败");
    default:
        return QStringLiteral("空闲");
    }
}

QLabel *fileField(QLabel *&target, const QString &label)
{
    auto *holder = new QWidget;
    auto *layout = new QHBoxLayout(holder);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(rov::makeLabel(label, QStringLiteral("mutedLabel")));
    target = rov::makeLabel(QStringLiteral("--"), QStringLiteral("bodyValue"));
    layout->addWidget(target);
    layout->addStretch();
    return target;
}

} // namespace

namespace rov
{

FirmwarePage::FirmwarePage(QWidget *parent) : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 10);
    root->setSpacing(10);
    root->addWidget(makePageHeader(QStringLiteral("固件升级"),
                                   QStringLiteral("管理固件信息并发出未来升级请求。"),
                                   QStringLiteral("演示 · 仅界面与请求")));

    auto *mainRow = new QHBoxLayout;
    mainRow->setSpacing(12);
    auto *fileCard = new CardWidget(QStringLiteral("固件文件"), IconKind::File);
    auto *dropZone = new QFrame;
    dropZone->setObjectName(QStringLiteral("card"));
    dropZone->setMinimumHeight(145);
    auto *dropLayout = new QVBoxLayout(dropZone);
    dropLayout->setAlignment(Qt::AlignCenter);
    auto *fileIcon = new IconWidget(IconKind::File);
    fileIcon->setFixedSize(42, 42);
    dropLayout->addWidget(fileIcon, 0, Qt::AlignCenter);
    dropLayout->addWidget(
        makeLabel(QStringLiteral("将固件文件拖放到此处"), QStringLiteral("bodyValue")), 0,
        Qt::AlignCenter);
    dropLayout->addWidget(makeLabel(QStringLiteral("或点击浏览"), QStringLiteral("statusGood")), 0,
                          Qt::AlignCenter);
    dropLayout->addWidget(
        makeLabel(QStringLiteral("支持格式：.bin、.hex、.uf2"), QStringLiteral("mutedLabel")), 0,
        Qt::AlignCenter);
    fileCard->contentLayout()->addWidget(dropZone);
    auto *info = new QGridLayout;
    info->setVerticalSpacing(8);
    info->addWidget(makeLabel(QStringLiteral("文件名"), QStringLiteral("mutedLabel")), 0, 0);
    m_fileName = makeLabel(QStringLiteral("--"), QStringLiteral("bodyValue"));
    info->addWidget(m_fileName, 0, 1);
    info->addWidget(makeLabel(QStringLiteral("版本"), QStringLiteral("mutedLabel")), 1, 0);
    m_fileVersion = makeLabel(QStringLiteral("--"), QStringLiteral("bodyValue"));
    info->addWidget(m_fileVersion, 1, 1);
    info->addWidget(makeLabel(QStringLiteral("文件大小"), QStringLiteral("mutedLabel")), 2, 0);
    m_fileSize = makeLabel(QStringLiteral("--"), QStringLiteral("bodyValue"));
    info->addWidget(m_fileSize, 2, 1);
    info->addWidget(makeLabel(QStringLiteral("校验摘要"), QStringLiteral("mutedLabel")), 3, 0);
    m_checksum = makeLabel(QStringLiteral("--"), QStringLiteral("bodyValue"));
    info->addWidget(m_checksum, 3, 1);
    info->addWidget(makeLabel(QStringLiteral("说明"), QStringLiteral("mutedLabel")), 4, 0);
    m_description = makeLabel(QStringLiteral("--"), QStringLiteral("bodyValue"));
    m_description->setWordWrap(true);
    info->addWidget(m_description, 4, 1);
    fileCard->contentLayout()->addLayout(info);
    fileCard->contentLayout()->addStretch();
    mainRow->addWidget(fileCard, 3);

    auto *right = new QVBoxLayout;
    right->setSpacing(12);
    auto *connectionCard = new CardWidget(QStringLiteral("USB CDC 通信"), IconKind::Action);
    auto *connectionRow = new QHBoxLayout;
    m_serialDeviceCombo = new QComboBox;
    m_serialDeviceCombo->setMinimumWidth(250);
    connectionRow->addWidget(m_serialDeviceCombo, 1);
    auto *refreshSerial = makeButton(QStringLiteral("刷新设备"), QStringLiteral("softButton"));
    connectionRow->addWidget(refreshSerial);
    m_serialConnectButton = makeButton(QStringLiteral("接管串口"), QStringLiteral("primaryButton"));
    connectionRow->addWidget(m_serialConnectButton);
    connectionCard->contentLayout()->addLayout(connectionRow);
    m_serialStatus = makeLabel(QStringLiteral("未连接 · VID_0483 PID_5740"), QStringLiteral("mutedLabel"));
    m_serialStatus->setWordWrap(true);
    connectionCard->contentLayout()->addWidget(m_serialStatus);
    connectionCard->contentLayout()->addWidget(
        makeLabel(QStringLiteral("USB CDC 不使用波特率；接收采用 AA55 帧的增量拆包。"),
                  QStringLiteral("mutedLabel")));
    right->addWidget(connectionCard, 0);

    auto *targetCard = new CardWidget(QStringLiteral("目标节点（6）"), IconKind::Firmware);
    m_nodeTable = new QTableWidget(0, 6);
    m_nodeTable->setHorizontalHeaderLabels(
        {QStringLiteral("节点 ID"), QStringLiteral("设备名称"), QStringLiteral("当前版本"),
         QStringLiteral("目标版本"), QStringLiteral("在线状态"), QStringLiteral("升级状态")});
    m_nodeTable->horizontalHeader()->setStretchLastSection(true);
    m_nodeTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_nodeTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_nodeTable->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_nodeTable->verticalHeader()->setDefaultSectionSize(28);
    m_nodeTable->setMinimumHeight(6 * 28 + 30);
    m_nodeTable->setColumnWidth(0, 66);
    m_nodeTable->setColumnWidth(2, 100);
    m_nodeTable->setColumnWidth(3, 100);
    m_nodeTable->setColumnWidth(4, 100);
    m_nodeTable->setColumnWidth(5, 100);
    m_nodeTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_nodeTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    targetCard->contentLayout()->addWidget(m_nodeTable);
    right->addWidget(targetCard, 3);

    auto *progressCard = new CardWidget(QStringLiteral("升级进度"), IconKind::Action);
    auto *progressLayout = new QVBoxLayout;
    progressLayout->setSpacing(7);
    for (int i = 0; i < 5; ++i)
    {
        auto *row = new QHBoxLayout;
        row->addWidget(makeLabel(QStringLiteral("0x0%1").arg(i + 1), QStringLiteral("bodyValue")));
        row->addWidget(
            makeLabel(QStringLiteral("推进器 %1").arg(i + 1), QStringLiteral("bodyValue")), 1);
        auto *bar = new QProgressBar;
        bar->setRange(0, 100);
        bar->setValue(i == 0 ? 100 : (i == 1 ? 65 : 0));
        bar->setTextVisible(false);
        row->addWidget(bar, 2);
        row->addWidget(
            makeLabel(i == 0 ? QStringLiteral("成功") : QStringLiteral("空闲"),
                      i == 0 ? QStringLiteral("statusGood") : QStringLiteral("statusIdle")));
        progressLayout->addLayout(row);
    }
    auto *actions = new QHBoxLayout;
    actions->addWidget(makeButton(QStringLiteral("进入 Boot"), QStringLiteral("softButton")));
    actions->addWidget(makeButton(QStringLiteral("擦除"), QStringLiteral("softButton")));
    actions->addWidget(makeButton(QStringLiteral("编程"), QStringLiteral("softButton")));
    auto *verify = makeButton(QStringLiteral("校验"), QStringLiteral("softButton"));
    actions->addWidget(verify);
    actions->addWidget(makeButton(QStringLiteral("重启"), QStringLiteral("softButton")));
    auto *update = makeButton(QStringLiteral("升级选中节点"), QStringLiteral("primaryButton"));
    actions->addWidget(update, 1);
    progressLayout->addLayout(actions);
    progressCard->contentLayout()->addLayout(progressLayout);
    right->addWidget(progressCard, 3);

    auto *logCard = new CardWidget(QStringLiteral("升级日志"), IconKind::List);
    m_requestLog =
        makeLabel(QStringLiteral("演示日志将显示在此处。"), QStringLiteral("mutedLabel"));
    m_requestLog->setWordWrap(true);
    logCard->contentLayout()->addWidget(m_requestLog);
    right->addWidget(logCard, 1);
    mainRow->addLayout(right, 7);
    root->addLayout(mainRow, 1);

    connect(verify, &QPushButton::clicked, this,
            [this]()
            {
                emit verifyRequested();
                logRequest(QStringLiteral("请求校验；不执行设备操作"));
            });
    connect(update, &QPushButton::clicked, this,
            [this]()
            {
                FirmwareUpgradeRequest request;
                for (const auto &node : m_snapshot.nodes)
                {
                    if (node.online)
                    {
                        request.nodeIds.append(node.nodeId);
                    }
                }
                request.firmwarePath = m_snapshot.fileName;
                emit upgradeRequested(request);
                logRequest(
                    QStringLiteral("请求升级 %1 个在线演示节点").arg(request.nodeIds.size()));
            });

    m_communication = new BootloaderCommunicationService(this);
    connect(refreshSerial, &QPushButton::clicked, this, &FirmwarePage::refreshSerialDevices);
    connect(m_serialConnectButton, &QPushButton::clicked, this,
            &FirmwarePage::toggleSerialConnection);
    connect(m_communication, &BootloaderCommunicationService::opened, this,
            [this](const QString &port)
            {
                m_serialConnectButton->setText(QStringLiteral("断开串口"));
                m_serialStatus->setText(QStringLiteral("已接管 %1 · 等待下位机数据").arg(port));
                logRequest(QStringLiteral("已打开 %1").arg(port));
            });
    connect(m_communication, &BootloaderCommunicationService::closed, this,
            [this]()
            {
                m_serialConnectButton->setText(QStringLiteral("接管串口"));
                m_serialStatus->setText(QStringLiteral("未连接 · VID_0483 PID_5740"));
                logRequest(QStringLiteral("串口已断开"));
            });
    connect(m_communication, &BootloaderCommunicationService::errorOccurred, this,
            [this](const QString &message)
            {
                m_serialStatus->setText(QStringLiteral("通信错误：%1").arg(message));
                logRequest(QStringLiteral("错误：%1").arg(message));
            });
    connect(m_communication, &BootloaderCommunicationService::rawBytesReceived, this,
            [this](const QByteArray &bytes)
            {
                logRequest(QStringLiteral("RX %1 字节：%2")
                               .arg(bytes.size())
                               .arg(QString(bytes.toHex(' ').toUpper())));
            });
    connect(m_communication, &BootloaderCommunicationService::frameReceived, this,
            [this](const CanGatewayFrame &frame)
            { logRequest(QStringLiteral("解析到 %1").arg(describeCanGatewayFrame(frame))); });

    refreshSerialDevices();
    setSnapshot(firmwarePreview());
}

void FirmwarePage::setSnapshot(const FirmwareSnapshot &snapshot)
{
    m_snapshot = snapshot;
    refreshView();
}

void FirmwarePage::refreshView()
{
    if (m_nodeTable == nullptr)
    {
        return;
    }
    m_fileName->setText(m_snapshot.fileName);
    m_fileVersion->setText(m_snapshot.fileVersion);
    m_fileSize->setText(m_snapshot.fileSize);
    m_checksum->setText(m_snapshot.checksum);
    m_description->setText(m_snapshot.fileDescription);
    m_nodeTable->setRowCount(m_snapshot.nodes.size());
    for (int row = 0; row < m_snapshot.nodes.size(); ++row)
    {
        const auto &node = m_snapshot.nodes.at(row);
        m_nodeTable->setItem(row, 0, new QTableWidgetItem(node.nodeId));
        m_nodeTable->setItem(row, 1, new QTableWidgetItem(node.deviceName));
        m_nodeTable->setItem(row, 2, new QTableWidgetItem(node.currentVersion));
        m_nodeTable->setItem(row, 3, new QTableWidgetItem(node.targetVersion));
        m_nodeTable->setItem(
            row, 4,
            new QTableWidgetItem(node.online ? QStringLiteral("在线") : QStringLiteral("离线")));
        m_nodeTable->setItem(row, 5, new QTableWidgetItem(firmwareStateText(node.state)));
    }
    if (!m_snapshot.log.isEmpty() && m_requestLog != nullptr)
    {
        m_requestLog->setText(m_snapshot.log.join(QStringLiteral("\n")));
    }
}

void FirmwarePage::refreshSerialDevices()
{
    if (m_serialDeviceCombo == nullptr)
        return;
    m_serialDevices = m_communication->enumerateDevices();
    m_serialDeviceCombo->clear();
    for (const auto &device : m_serialDevices)
    {
        const QString name = device.displayName.isEmpty() ? device.portName : device.displayName;
        m_serialDeviceCombo->addItem(QStringLiteral("%1 · %2").arg(device.portName, name));
    }
    if (m_serialDevices.isEmpty())
    {
        m_serialDeviceCombo->addItem(QStringLiteral("未发现 VID_0483 PID_5740 设备"));
        m_serialStatus->setText(QStringLiteral("未发现 Lamost USB CDC 虚拟串口"));
        logRequest(QStringLiteral("刷新设备：未发现 VID_0483 PID_5740"));
    }
    else
    {
        m_serialStatus->setText(QStringLiteral("发现 %1 个匹配设备").arg(m_serialDevices.size()));
        logRequest(QStringLiteral("刷新设备：发现 %1 个匹配设备").arg(m_serialDevices.size()));
    }
}

void FirmwarePage::toggleSerialConnection()
{
    if (m_communication == nullptr)
        return;
    if (m_communication->isOpen())
    {
        m_communication->close();
        return;
    }
    const int index = m_serialDeviceCombo == nullptr ? -1 : m_serialDeviceCombo->currentIndex();
    if (index < 0 || index >= m_serialDevices.size())
    {
        logRequest(QStringLiteral("请先刷新并选择 VID_0483 PID_5740 设备"));
        return;
    }
    m_communication->open(m_serialDevices.at(index));
}

void FirmwarePage::logRequest(const QString &message)
{
    if (m_requestLog != nullptr)
    {
        m_runtimeLog.append(message);
        while (m_runtimeLog.size() > 12)
            m_runtimeLog.removeFirst();
        m_requestLog->setText(m_runtimeLog.join(QStringLiteral("\n")));
    }
}

} // namespace rov
