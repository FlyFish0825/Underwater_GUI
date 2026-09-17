#pragma once

#include "communication/bootloader/BootloaderTypes.h"

#include <QDialog>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTextBrowser;

namespace rov
{

/**
 * @brief 高级 Bootloader 命令中心。
 *
 * 默认列出全部 Host 命令，使用目标、Byte2 和语义参数生成请求；Peer
 * 报文只在监视区显示。只有显式打开“协议开发模式”后才允许编辑原始
 * 四字节参数，避免正常操作误改 Session 状态。
 */
class BootloaderCommandDialog final : public QDialog
{
    Q_OBJECT

  public:
    explicit BootloaderCommandDialog(QWidget *parent = nullptr);

    void setTarget(quint8 nodeId);
    void appendPeerMessage(const PeerControlMessage &message);
    void appendDebugMessage(const QString &message);
    void setDebugHistory(const QStringList &messages);

  signals:
    void commandRequested(quint8 target, BootCommand command, quint8 byte2,
                          const QByteArray &params);
    void peerCommandRequested(quint8 target, BootCommand command, quint8 source,
                              quint16 session, quint16 value);

  private:
    void sendSelected();
    void updateParameterHints();

    QListWidget *m_commandList = nullptr;
    QComboBox *m_target = nullptr;
    QComboBox *m_source = nullptr;
    QComboBox *m_jumpMode = nullptr;
    QLineEdit *m_session = nullptr;
    QLineEdit *m_commandCode = nullptr;
    QLineEdit *m_byte2 = nullptr;
    QLineEdit *m_params = nullptr;
    QLineEdit *m_value = nullptr;
    QCheckBox *m_developerMode = nullptr;
    QLabel *m_commandDescription = nullptr;
    QTextBrowser *m_peerMonitor = nullptr;
};

} // namespace rov
