#pragma once

#include <QDialog>
#include <QStringList>

class QTextBrowser;

namespace rov
{

class FirmwareLogRecordingDialog final : public QDialog
{
    Q_OBJECT

  public:
    explicit FirmwareLogRecordingDialog(const QStringList &entries, QWidget *parent = nullptr);
    static bool exportEntries(const QStringList &entries, QWidget *parent = nullptr);

  private:
    void exportLogs();

    QStringList m_entries;
    QTextBrowser *m_view = nullptr;
};

} // namespace rov
