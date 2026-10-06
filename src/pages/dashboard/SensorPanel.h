#pragma once

#include "contracts/sensors/SensorContract.h"
#include <QWidget>
#include <QVector>

class QLabel;
class QPushButton;
class QGridLayout;
class QTableWidget;

namespace rov
{
// Embedded dashboard controls; not a top-level page or navigation destination.
class SensorPanel final : public QWidget
{
    Q_OBJECT
  public:
    explicit SensorPanel(QWidget *parent = nullptr);
    void setSnapshot(const SensorSnapshot &snapshot);
    void setParameterFeedback(const SensorParameterFeedback &feedback);
  signals:
    void requestIssued(const rov::SensorRequest &request);
    void collapseRequested();
  private:
    struct Control
    {
        QPushButton *button = nullptr;
        quint8 target = 0;
        int capability = -1;
        bool deviceAction = false;
    };
    struct ParameterRow
    {
        quint8 target = 0;
        quint16 id = 0;
        QLabel *feedback = nullptr;
        QWidget *editor = nullptr;
    };
    void addParameter(QGridLayout *grid, int row, quint8 target, quint16 id,
                      const QString &name, QWidget *editor, int capability);
    QPushButton *actionButton(const QString &name, quint8 target, SensorOperation operation,
                              int capability = -1, bool confirmation = false,
                              bool deviceAction = false, quint8 calType = 1, quint8 calAction = 1);
    static bool confirmAction(const QString &message);
    SensorSnapshot m_snapshot;
    QLabel *m_connection = nullptr;
    QLabel *m_imuInfo = nullptr;
    QLabel *m_depthInfo = nullptr;
    QLabel *m_imuStatus = nullptr;
    QLabel *m_depthStatus = nullptr;
    QLabel *m_imuCommand = nullptr;
    QLabel *m_depthCommand = nullptr;
    QTableWidget *m_rawTable = nullptr;
    QTableWidget *m_attitudeTable = nullptr;
    QTableWidget *m_depthTable = nullptr;
    QVector<Control> m_controls;
    QVector<ParameterRow> m_parameters;
};
} // namespace rov
