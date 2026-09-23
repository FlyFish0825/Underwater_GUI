#include "pages/firmware/FirmwareLogRecordingDialog.h"

#include "ui/firmware_history/FirmwareLogFormatter.h"
#include "ui/common/UiPrimitives.h"

#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextStream>
#include <QVBoxLayout>

namespace rov
{

FirmwareLogRecordingDialog::FirmwareLogRecordingDialog(const QStringList &entries, QWidget *parent)
    : QDialog(parent), m_entries(entries)
{
    setWindowTitle(QStringLiteral("本次日志记录"));
    setObjectName(QStringLiteral("logRecordingDialog"));
    setWindowFlag(Qt::Window, true);
    resize(980, 640);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 14);
    root->setSpacing(10);

    auto *toolbar = new QHBoxLayout;
    toolbar->addWidget(makeLabel(QStringLiteral("本次记录：%1 条").arg(m_entries.size()),
                                 QStringLiteral("mutedLabel")));
    toolbar->addStretch();
    auto *exportButton = makeButton(QStringLiteral("导出记录"), QStringLiteral("softButton"));
    toolbar->addWidget(exportButton);
    root->addLayout(toolbar);

    m_view = new QTextBrowser;
    m_view->setReadOnly(true);
    m_view->setLineWrapMode(QTextEdit::NoWrap);
    m_view->setOpenLinks(false);
    m_view->setOpenExternalLinks(false);
    for (const QString &entry : m_entries)
        m_view->append(formatFirmwareLogHtml(entry));
    m_view->moveCursor(QTextCursor::End);
    root->addWidget(m_view, 1);

    connect(exportButton, &QPushButton::clicked, this, &FirmwareLogRecordingDialog::exportLogs);
}

void FirmwareLogRecordingDialog::exportLogs()
{
    exportEntries(m_entries, this);
}

bool FirmwareLogRecordingDialog::exportEntries(const QStringList &entries, QWidget *parent)
{
    const QString path = QFileDialog::getSaveFileName(
        parent, QStringLiteral("导出本次日志"), QStringLiteral("bootloader_log.txt"),
        QStringLiteral("日志文本 (*.txt *.log);;所有文件 (*.*)"));
    if (path.isEmpty())
        return false;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(parent, QStringLiteral("导出失败"),
                             QStringLiteral("无法写入日志文件：%1").arg(file.errorString()));
        return false;
    }
    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    for (const QString &entry : entries)
        stream << entry << '\n';
    file.close();
    QMessageBox::information(parent, QStringLiteral("导出完成"),
                             QStringLiteral("已导出 %1 条原始日志。\n%2")
                                 .arg(entries.size()).arg(path));
    return true;
}

} // namespace rov
