#include "pages/dashboard/DashboardPage.h"

#include "preview/PreviewData.h"
#include "ui/common/UiPrimitives.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDialog>
#include <QEvent>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QTransform>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <functional>

namespace
{

enum class ControlAxis
{
    Surge,
    Sway,
    Heave,
    Roll,
    Pitch,
    Yaw
};

bool hasSystemData(const rov::DashboardSnapshot &snapshot)
{
    return snapshot.systemStamp.validity == rov::DataValidity::Valid &&
           snapshot.systemStamp.freshness != rov::DataFreshness::Offline;
}

void setTone(QLabel *label, const QString &tone)
{
    if (label == nullptr)
    {
        return;
    }
    label->setProperty("dashboardTone", QVariant(tone));
    label->style()->unpolish(label);
    label->style()->polish(label);
}

QString formatNumber(const bool available, const double value, const int precision,
                     const QString &suffix = QString())
{
    if (!available)
    {
        return QStringLiteral("--");
    }
    return QStringLiteral("%1%2").arg(value, 0, 'f', precision).arg(suffix);
}

QString formatAxisValue(const double value)
{
    const QString sign = value > 0.0 ? QStringLiteral("+") : QString();
    return QStringLiteral("%1%2").arg(sign).arg(value, 0, 'f', 1);
}

QPushButton *axisButton(const QString &text, QWidget *parent)
{
    auto *button = rov::makeButton(text, QStringLiteral("softButton"), parent);
    button->setProperty("dashboardAxis", true);
    button->setFixedSize(32, 30);
    button->setFocusPolicy(Qt::NoFocus);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

class MiniBarChart final : public QWidget
{
  public:
    explicit MiniBarChart(const QColor &color, QWidget *parent = nullptr)
        : QWidget(parent), m_color(color)
    {
        setMinimumSize(72, 34);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    QSize sizeHint() const override
    {
        return QSize(104, 38);
    }

    void setValues(const QVector<double> &values)
    {
        m_values = values;
        update();
    }

  protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event)
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(QStringLiteral("#dce6ef")), 1));
        painter.drawLine(0, height() - 2, width(), height() - 2);

        if (m_values.isEmpty())
        {
            painter.setPen(QColor(QStringLiteral("#9cafc1")));
            painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("--"));
            return;
        }

        const double maximum = *std::max_element(m_values.constBegin(), m_values.constEnd());
        const double safeMaximum = maximum > 0.0 ? maximum : 1.0;
        const qreal gap = 3.0;
        const qreal barWidth = qMax(3.0, (width() - gap * (m_values.size() + 1)) /
                                             static_cast<qreal>(m_values.size()));
        const qreal baseline = height() - 3.0;
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_color);
        for (int i = 0; i < m_values.size(); ++i)
        {
            const qreal x = gap + i * (barWidth + gap);
            const qreal barHeight = qBound(3.0, (m_values.at(i) / safeMaximum) * (height() - 8),
                                           static_cast<qreal>(height() - 8));
            painter.drawRoundedRect(QRectF(x, baseline - barHeight, barWidth, barHeight), 2, 2);
        }
    }

  private:
    QColor m_color;
    QVector<double> m_values;
};

class RovTopView final : public QWidget
{
  public:
    explicit RovTopView(QWidget *parent = nullptr) : QWidget(parent)
    {
        m_image.load(QStringLiteral(":/dashboard/rov_top_view.png"));
        if (!m_image.isNull())
        {
            // The source image faces left. Place the bow at the bottom of the dashboard
            // top view so the displayed head/tail positions match the vehicle layout.
            m_image = m_image.transformed(QTransform().rotate(-90), Qt::SmoothTransformation);
        }
        setMinimumSize(250, 350);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    QPointF thrusterPosition(const int index) const
    {
        // Coordinates follow the annotated dashboard view: T1/T2 are the upper outer pair,
        // T5/T6 and T7/T8 are the upper/lower inner pairs, and T3/T4 are the lower outer pair.
        static const QPointF positions[] = {
            QPointF(0.19, 0.14), QPointF(0.81, 0.14), QPointF(0.19, 0.86),
            QPointF(0.81, 0.86), QPointF(0.32, 0.38), QPointF(0.68, 0.38),
            QPointF(0.32, 0.63), QPointF(0.68, 0.63)};
        const QPointF normalized = positions[qBound(0, index, rov::kDashboardThrusterCount - 1)];
        const QRectF imageRect = drawnImageRect();
        return QPointF(imageRect.left() + normalized.x() * imageRect.width(),
                       imageRect.top() + normalized.y() * imageRect.height());
    }

  protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event)
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), Qt::white);

        if (!m_image.isNull())
        {
            const QRectF imageRect = drawnImageRect();
            painter.drawPixmap(imageRect, m_image, m_image.rect());
            return;
        }

        const QColor dark(QStringLiteral("#203b60"));
        const QColor outline(QStringLiteral("#a9bacb"));
        const QColor blue(QStringLiteral("#1687ee"));
        const QPointF center(width() * 0.50, height() * 0.53);
        const qreal bodyWidth = qMin(width() * 0.25, 126.0);
        const qreal bodyHeight = qMin(height() * 0.66, 278.0);
        const QRectF body(center.x() - bodyWidth / 2.0, center.y() - bodyHeight / 2.0, bodyWidth,
                          bodyHeight);

        painter.setPen(QPen(QColor(QStringLiteral("#d8e3ec")), 3));
        painter.setBrush(QColor(QStringLiteral("#f1f5f8")));
        painter.drawRoundedRect(body.adjusted(4, 4, 4, 4), bodyWidth / 2.0, bodyWidth / 2.0);
        painter.setPen(QPen(dark, 2));
        painter.setBrush(QColor(QStringLiteral("#f8fafc")));
        painter.drawRoundedRect(body, bodyWidth / 2.0, bodyWidth / 2.0);

        painter.setPen(QPen(outline, 1.2));
        painter.drawLine(center.x(), body.top() + 48, center.x(), body.bottom() - 48);
        painter.drawLine(body.left() + 18, center.y(), body.right() - 18, center.y());
        painter.drawEllipse(QPointF(center.x(), body.top() + 28), 7, 7);
        painter.drawEllipse(QPointF(center.x(), body.bottom() - 29), 5, 5);
        painter.drawRoundedRect(QRectF(center.x() - 9, body.top() + 61, 18, 22), 4, 4);
        painter.drawRoundedRect(QRectF(center.x() - 8, body.bottom() - 82, 16, 22), 4, 4);

        // Dashboard mapping follows the labeled physical layout in the vehicle top view.
        const QPointF thrusters[] = {QPointF(body.left() - 48, body.top() + 38),
                                     QPointF(body.right() + 48, body.top() + 38),
                                     QPointF(body.left() - 48, body.bottom() - 38),
                                     QPointF(body.right() + 48, body.bottom() - 38),
                                     QPointF(body.left() - 37, body.top() + 92),
                                     QPointF(body.right() + 37, body.top() + 92),
                                     QPointF(body.left() - 37, body.bottom() - 92),
                                     QPointF(body.right() + 37, body.bottom() - 92)};
        const QStringList labels = {
            QStringLiteral("T1"), QStringLiteral("T2"), QStringLiteral("T3"), QStringLiteral("T4"),
            QStringLiteral("T5"), QStringLiteral("T6"), QStringLiteral("T7"), QStringLiteral("T8")};
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::DemiBold));
        for (int i = 0; i < rov::kDashboardThrusterCount; ++i)
        {
            const QPointF position = thrusters[i];
            const bool leftSide = position.x() < center.x();
            painter.setPen(QPen(outline, 1));
            painter.drawLine(QPointF(leftSide ? body.left() : body.right(), position.y()),
                             position);
            const bool horizontal = i < 4;
            painter.setPen(QPen(dark, 1.3));
            painter.setBrush(QColor(QStringLiteral("#34485e")));
            if (horizontal)
            {
                painter.drawEllipse(position, 21, 21);
            }
            else
            {
                painter.drawRoundedRect(QRectF(position.x() - 15, position.y() - 19, 30, 38), 7, 7);
            }
            painter.setBrush(blue);
            if (horizontal)
            {
                painter.drawEllipse(position, 10, 10);
            }
            else
            {
                painter.drawEllipse(position, 7, 11);
            }
            painter.setPen(QPen(QColor(QStringLiteral("#9dd5f8")), 1.1));
            painter.drawLine(position + QPointF(-7, -7), position + QPointF(7, 7));
            painter.drawLine(position + QPointF(-7, 7), position + QPointF(7, -7));
            painter.setPen(dark);
            painter.drawText(
                QRectF(leftSide ? position.x() - 42 : position.x() + 24, position.y() - 10, 28, 20),
                Qt::AlignCenter, labels.at(i));
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(QStringLiteral("#10a85e")));
            painter.drawEllipse(position + QPointF(leftSide ? -29 : 29, -17), 4, 4);
        }

        painter.setPen(QPen(QColor(QStringLiteral("#7189a3")), 1.2));
        painter.drawLine(center.x(), body.top() - 29, center.x(), body.top() - 7);
        painter.drawLine(center.x(), body.top() - 29, center.x() - 5, body.top() - 21);
        painter.drawLine(center.x(), body.top() - 29, center.x() + 5, body.top() - 21);
        painter.drawText(QRectF(center.x() - 26, body.top() - 52, 52, 18), Qt::AlignCenter,
                         QStringLiteral("前方"));
        painter.drawLine(center.x(), body.bottom() + 7, center.x(), body.bottom() + 29);
        painter.drawLine(center.x(), body.bottom() + 29, center.x() - 5, body.bottom() + 21);
        painter.drawLine(center.x(), body.bottom() + 29, center.x() + 5, body.bottom() + 21);
        painter.drawText(QRectF(center.x() - 26, body.bottom() + 34, 52, 18), Qt::AlignCenter,
                         QStringLiteral("后方"));
    }

  private:
    QRectF drawnImageRect() const
    {
        const QRectF target = QRectF(rect()).adjusted(8, 8, -8, -8);
        const QSize scaledSize = m_image.isNull()
                                     ? target.size().toSize()
                                     : m_image.size().scaled(target.size().toSize(), Qt::KeepAspectRatio);
        return QRectF(QPointF(target.center().x() - scaledSize.width() / 2.0,
                              target.center().y() - scaledSize.height() / 2.0),
                      scaledSize);
    }

    QPixmap m_image;
};

QWidget *metricTile(const QString &label, QLabel *&value, const QString &initial)
{
    auto *tile = new QFrame;
    tile->setObjectName(QStringLiteral("card"));
    tile->setMinimumHeight(52);
    auto *layout = new QVBoxLayout(tile);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(2);
    layout->addWidget(rov::makeMetricLabel(label));
    value = rov::makeMetricValue(initial);
    layout->addWidget(value);
    return tile;
}

class ThrusterCard final : public QFrame
{
  public:
    using ClickHandler = std::function<void()>;
    using HoverHandler = std::function<void(bool)>;

    explicit ThrusterCard(QWidget *parent = nullptr) : QFrame(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
    }

    ClickHandler onClicked;
    HoverHandler onHoverChanged;

  protected:
    void enterEvent(QEvent *event) override
    {
        if (onHoverChanged)
        {
            onHoverChanged(true);
        }
        QFrame::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        if (onHoverChanged)
        {
            onHoverChanged(false);
        }
        QFrame::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            m_pressed = true;
        }
        QFrame::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        const bool clicked = m_pressed && rect().contains(event->pos());
        m_pressed = false;
        QFrame::mouseReleaseEvent(event);
        if (clicked && onClicked)
        {
            onClicked();
        }
    }

  private:
    bool m_pressed = false;
};

class ThrusterConnectionOverlay final : public QWidget
{
  public:
    explicit ThrusterConnectionOverlay(RovTopView *topView, QWidget *parent = nullptr)
        : QWidget(parent), m_topView(topView)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
    }

    void showConnection(const int index, ThrusterCard *card)
    {
        m_index = index;
        m_card = card;
        update();
    }

    void clearConnection(const ThrusterCard *card)
    {
        if (m_card == card)
        {
            m_index = -1;
            m_card = nullptr;
            update();
        }
    }

  protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event)
        if (m_card == nullptr || m_topView == nullptr || m_index < 0)
        {
            return;
        }

        const QPointF cardCenter = mapFromGlobal(m_card->mapToGlobal(m_card->rect().center()));
        const bool cardOnLeft = cardCenter.x() < width() / 2.0;
        const QPoint cardEdge(cardOnLeft ? m_card->width() : 0, m_card->height() / 2);
        const QPointF start = mapFromGlobal(m_card->mapToGlobal(cardEdge));
        const QPointF target =
            mapFromGlobal(m_topView->mapToGlobal(m_topView->thrusterPosition(m_index).toPoint()));
        const QPointF elbow(start.x() + (cardOnLeft ? 20.0 : -20.0), start.y());

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(QStringLiteral("#1687ee")), 2.5, Qt::SolidLine,
                            Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(start, elbow);       // Short segment leaves the card cleanly.
        painter.drawLine(elbow, target);      // Long segment identifies the physical thruster.
        painter.setPen(QPen(Qt::white, 1.5));
        painter.setBrush(QColor(QStringLiteral("#1687ee")));
        painter.drawEllipse(target, 5.0, 5.0);
    }

  private:
    RovTopView *m_topView = nullptr;
    ThrusterCard *m_card = nullptr;
    int m_index = -1;
};

class ThrusterOverview final : public QWidget
{
  public:
    explicit ThrusterOverview(QWidget *parent = nullptr) : QWidget(parent)
    {
        m_grid = new QGridLayout(this);
        m_grid->setContentsMargins(0, 0, 0, 0);
        m_grid->setHorizontalSpacing(8);
        m_grid->setVerticalSpacing(8);
        m_topView = new RovTopView(this);
        m_grid->addWidget(m_topView, 0, 1, 4, 1);
        m_grid->setColumnStretch(1, 2);
        m_overlay = new ThrusterConnectionOverlay(m_topView, this);
        m_overlay->setGeometry(rect());
        m_overlay->raise();
    }

    void addThrusterCard(ThrusterCard *card, const int index)
    {
        // Arrange cards beside the same physical row as their annotated propeller:
        // T1/T2, T5/T6, T7/T8, then T3/T4 from top to bottom.
        static const int rows[] = {0, 0, 3, 3, 1, 1, 2, 2};
        const int row = rows[qBound(0, index, rov::kDashboardThrusterCount - 1)];
        const int column = index % 2 == 0 ? 0 : 2;
        m_grid->addWidget(card, row, column);
        card->onHoverChanged = [this, index, card](const bool entered)
        {
            if (entered)
            {
                m_overlay->showConnection(index, card);
            }
            else
            {
                m_overlay->clearConnection(card);
            }
        };
    }

  protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        m_overlay->setGeometry(rect());
        m_overlay->raise();
    }

  private:
    QGridLayout *m_grid = nullptr;
    RovTopView *m_topView = nullptr;
    ThrusterConnectionOverlay *m_overlay = nullptr;
};

ThrusterCard *thrusterTile(const rov::ThrusterTelemetry &item, QLabel *&rpmValue,
                           QLabel *&currentValue, QLabel *&temperatureValue, QLabel *&statusValue)
{
    auto *tile = new ThrusterCard;
    tile->setObjectName(QStringLiteral("card"));
    tile->setProperty("dashboardInteractive", true);
    tile->setMinimumSize(148, 96);
    auto *layout = new QVBoxLayout(tile);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(3);

    auto *titleRow = new QHBoxLayout;
    const QString shortLabel = item.label.section(QStringLiteral(" · "), 0, 0);
    tile->setToolTip(item.label);
    auto *title = rov::makeLabel(QStringLiteral("推进器 %1").arg(shortLabel),
                                 QStringLiteral("thrusterTitle"));
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    titleRow->addWidget(title);
    titleRow->addStretch();
    statusValue = rov::makeLabel(QStringLiteral("●"), QStringLiteral("statusGood"));
    statusValue->setAttribute(Qt::WA_TransparentForMouseEvents);
    statusValue->setToolTip(item.status);
    titleRow->addWidget(statusValue);
    layout->addLayout(titleRow);

    auto addValueRow = [layout](const QString &label, QLabel *&value, const QString &text)
    {
        auto *row = new QHBoxLayout;
        row->setSpacing(4);
        auto *nameLabel = rov::makeMetricLabel(label);
        nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        row->addWidget(nameLabel);
        row->addStretch();
        value = rov::makeLabel(text, QStringLiteral("bodyValue"));
        value->setAttribute(Qt::WA_TransparentForMouseEvents);
        row->addWidget(value);
        layout->addLayout(row);
    };
    addValueRow(QStringLiteral("转速"), rpmValue, QString::number(item.rpm, 'f', 0));
    addValueRow(QStringLiteral("电流"), currentValue,
                QStringLiteral("%1 A").arg(item.currentA, 0, 'f', 4));
    addValueRow(QStringLiteral("温度"), temperatureValue,
                QStringLiteral("%1 °C").arg(item.temperatureC, 0, 'f', 1));
    return tile;
}

class ThrusterDetailDialog final : public QDialog
{
  public:
    ThrusterDetailDialog(const rov::ThrusterTelemetry &item, const bool disabled,
                         QWidget *parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(QStringLiteral("推进器详情 · %1").arg(item.label));
        setModal(true);
        setMinimumWidth(360);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(18, 16, 18, 16);
        layout->setSpacing(12);

        auto *title = rov::makeLabel(QStringLiteral("推进器 %1").arg(item.label),
                                     QStringLiteral("cardTitle"));
        layout->addWidget(title);

        auto *grid = new QGridLayout;
        grid->setHorizontalSpacing(18);
        grid->setVerticalSpacing(8);
        grid->addWidget(rov::makeMetricLabel(QStringLiteral("当前状态")), 0, 0);
        grid->addWidget(
            rov::makeLabel(disabled ? QStringLiteral("已停用") : item.status,
                           disabled ? QStringLiteral("statusBad") : QStringLiteral("statusGood")),
            0, 1);
        grid->addWidget(rov::makeMetricLabel(QStringLiteral("转速")), 1, 0);
        grid->addWidget(rov::makeLabel(QStringLiteral("%1 rpm").arg(item.rpm, 0, 'f', 0),
                                       QStringLiteral("bodyValue")),
                        1, 1);
        grid->addWidget(rov::makeMetricLabel(QStringLiteral("电流")), 2, 0);
        grid->addWidget(rov::makeLabel(QStringLiteral("%1 A").arg(item.currentA, 0, 'f', 4),
                                       QStringLiteral("bodyValue")),
                        2, 1);
        grid->addWidget(rov::makeMetricLabel(QStringLiteral("温度")), 3, 0);
        grid->addWidget(rov::makeLabel(QStringLiteral("%1 °C").arg(item.temperatureC, 0, 'f', 3),
                                       QStringLiteral("bodyValue")),
                        3, 1);
        layout->addLayout(grid);

        m_disableCheck = new QCheckBox(QStringLiteral("停用该推进器"), this);
        m_disableCheck->setChecked(disabled);
        layout->addWidget(m_disableCheck);

        auto *actionRow = new QHBoxLayout;
        m_testButton =
            rov::makeButton(QStringLiteral("发送测试 1 秒"), QStringLiteral("softButton"), this);
        auto *closeButton =
            rov::makeButton(QStringLiteral("关闭"), QStringLiteral("softButton"), this);
        actionRow->addWidget(m_testButton);
        actionRow->addWidget(closeButton);
        layout->addLayout(actionRow);

        m_result = rov::makeLabel(QStringLiteral("演示模式：操作只记录为请求。"),
                                  QStringLiteral("mutedLabel"));
        m_result->setWordWrap(true);
        layout->addWidget(m_result);

        connect(m_disableCheck, &QCheckBox::toggled, this,
                [this](const bool checked)
                {
                    m_result->setText(
                        checked ? QStringLiteral("该推进器将被标记为停用，可取消勾选恢复。")
                                : QStringLiteral("该推进器已准备恢复，点击关闭后生效。"));
                });
        connect(m_testButton, &QPushButton::clicked, this,
                [this]
                {
                    if (m_disableCheck->isChecked())
                    {
                        m_result->setText(QStringLiteral("当前推进器已停用，未发送测试请求。"));
                        return;
                    }
                    m_testRequested = true;
                    m_result->setText(QStringLiteral("已记录 1 秒单推进器测试请求。"));
                });
        connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    }

    bool disabled() const
    {
        return m_disableCheck->isChecked();
    }
    bool testRequested() const
    {
        return m_testRequested;
    }

  private:
    QCheckBox *m_disableCheck = nullptr;
    QPushButton *m_testButton = nullptr;
    QLabel *m_result = nullptr;
    bool m_testRequested = false;
};

} // namespace

namespace rov
{

DashboardPage::DashboardPage(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("dashboardPage"));
    // Reuse the main window's existing small-screen scaling without changing other pages.
    setProperty("fitViewportScale", true);
    m_thrusterDisabled = QVector<bool>(kDashboardThrusterCount, false);
    setStyleSheet(QStringLiteral(
        "QLabel#thrusterTitle { color: #18365b; font-weight: 650; }"
        "QLabel#bodyValue { color: #203b60; font-weight: 600; }"
        "QLabel[dashboardTone=good] { color: #078d4a; }"
        "QLabel[dashboardTone=warn] { color: #d98512; }"
        "QLabel[dashboardTone=bad] { color: #c54854; }"
        "QLabel#summaryGood { color: #078d4a; }"
        "QLabel#summaryWarn { color: #d98512; }"
        "QLabel#summaryBad { color: #c54854; }"
        "QLabel#axisName { color: #203b60; font-weight: 650; }"
        "QLabel#axisHint { color: #7a8fa6; font-size: 11px; }"
        "QLabel#axisValue { color: #203b60; font-weight: 650; min-width: 22px; }"
        "QPushButton#softButton[dashboardAxis=\"true\"] { min-width: 32px; min-height: 28px; "
        "padding: 0; font-size: 18px; font-weight: 700; border-radius: 8px; }"
        "QFrame#card[dashboardInteractive=\"true\"] { border-color: #b9d8f3; }"
        "QFrame#card[dashboardInteractive=\"true\"]:hover { border-color: #3d9de8; "
        "background: #f7fbff; }"
        "QCheckBox#dashboardEnable::indicator { width: 36px; height: 20px; border-radius: 10px; }"
        "QCheckBox#dashboardEnable::indicator:unchecked { background: #c8d5e2; border: 1px solid "
        "#b5c6d6; }"
        "QCheckBox#dashboardEnable::indicator:checked { background: #218fe6; border: 1px solid "
        "#218fe6; }"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 10);
    root->setSpacing(10);
    root->addWidget(makePageHeader(QStringLiteral("总览"),
                                   QStringLiteral("水下机器人状态、推进器与控制概览。"),
                                   QStringLiteral("演示 · 未连接设备")));

    const DashboardSnapshot preview = dashboardPreview();
    auto *topRow = new QHBoxLayout;
    topRow->setSpacing(12);

    auto *overview = new CardWidget(QStringLiteral("机器人总览（俯视图）"), IconKind::Dashboard);
    auto *thrusterOverview = new ThrusterOverview;
    for (int i = 0; i < kDashboardThrusterCount; ++i)
    {
        QLabel *rpm = nullptr;
        QLabel *current = nullptr;
        QLabel *temperature = nullptr;
        QLabel *status = nullptr;
        const ThrusterTelemetry item = preview.thrusters.value(i);
        auto *tile = thrusterTile(item, rpm, current, temperature, status);
        tile->onClicked = [this, i]() { openThrusterDetails(i); };
        m_thrusterRpmValues.append(rpm);
        m_thrusterCurrentValues.append(current);
        m_thrusterTemperatureValues.append(temperature);
        m_thrusterStatusValues.append(status);
        thrusterOverview->addThrusterCard(tile, i);
    }
    overview->contentLayout()->addWidget(thrusterOverview);
    topRow->addWidget(overview, 5);

    auto *stateColumn = new QVBoxLayout;
    stateColumn->setSpacing(12);

    auto *stateCard = new CardWidget(QStringLiteral("机器人状态"), IconKind::Status);
    m_stateUpdate = makeLabel(QStringLiteral("更新时间：--"), QStringLiteral("mutedLabel"));
    stateCard->headerLayout()->addWidget(m_stateUpdate);
    auto *stateGrid = new QGridLayout;
    stateGrid->setSpacing(8);
    stateGrid->addWidget(metricTile(QStringLiteral("深度"), m_depthValue, QStringLiteral("--")), 0,
                         0);
    stateGrid->addWidget(metricTile(QStringLiteral("横滚"), m_rollValue, QStringLiteral("--")), 0,
                         1);
    stateGrid->addWidget(metricTile(QStringLiteral("俯仰"), m_pitchValue, QStringLiteral("--")), 0,
                         2);
    stateGrid->addWidget(metricTile(QStringLiteral("航向"), m_yawValue, QStringLiteral("--")), 0,
                         3);
    stateGrid->addWidget(
        metricTile(QStringLiteral("母线电压"), m_voltageValue, QStringLiteral("--")), 1, 0);
    stateGrid->addWidget(metricTile(QStringLiteral("工作模式"), m_modeValue, QStringLiteral("--")),
                         1, 1);
    stateGrid->addWidget(metricTile(QStringLiteral("解锁状态"), m_armValue, QStringLiteral("--")),
                         1, 2);
    stateGrid->addWidget(
        metricTile(QStringLiteral("内部温度"), m_temperatureValue, QStringLiteral("--")), 1, 3);
    for (int column = 0; column < 4; ++column)
    {
        stateGrid->setColumnStretch(column, 1);
    }
    stateCard->contentLayout()->addLayout(stateGrid);
    stateColumn->addWidget(stateCard, 1);

    auto *controlCard = new CardWidget(QStringLiteral("六自由度手动控制"), IconKind::Action);
    auto *enableRow = new QHBoxLayout;
    enableRow->addWidget(makeLabel(QStringLiteral("启用控制"), QStringLiteral("bodyValue")));
    enableRow->addStretch();
    m_enableControl = new QCheckBox(controlCard);
    m_enableControl->setObjectName(QStringLiteral("dashboardEnable"));
    m_enableControl->setChecked(false);
    m_enableControl->setToolTip(QStringLiteral("只输出用户意图，不直接控制设备"));
    enableRow->addWidget(m_enableControl);
    controlCard->contentLayout()->addLayout(enableRow);

    auto *permissionRow = new QHBoxLayout;
    permissionRow->addWidget(makeLabel(QStringLiteral("控制权限"), QStringLiteral("bodyValue")));
    permissionRow->addStretch();
    m_controlPermission = makeStatusPill(QStringLiteral("仅演示"), QStringLiteral("statusWarn"));
    permissionRow->addWidget(m_controlPermission);
    controlCard->contentLayout()->addLayout(permissionRow);

    auto *axisGrid = new QGridLayout;
    axisGrid->setContentsMargins(0, 0, 0, 0);
    axisGrid->setHorizontalSpacing(3);
    axisGrid->setVerticalSpacing(6);

    const QStringList axisNames = {QStringLiteral("前进"), QStringLiteral("横移"),
                                   QStringLiteral("升沉"), QStringLiteral("横滚"),
                                   QStringLiteral("俯仰"), QStringLiteral("航向")};
    const QStringList axisHints = {QStringLiteral("前进 / 后退"), QStringLiteral("左 / 右"),
                                   QStringLiteral("上 / 下"),     QStringLiteral("左 / 右"),
                                   QStringLiteral("上 / 下"),     QStringLiteral("左 / 右")};
    const QStringList positiveArrows = {QStringLiteral("↑"), QStringLiteral("→"),
                                        QStringLiteral("↑"), QStringLiteral("↶"),
                                        QStringLiteral("↶"), QStringLiteral("↶")};
    const QStringList negativeArrows = {QStringLiteral("↓"), QStringLiteral("←"),
                                        QStringLiteral("↓"), QStringLiteral("↷"),
                                        QStringLiteral("↷"), QStringLiteral("↷")};
    const ControlAxis controlAxes[] = {ControlAxis::Surge, ControlAxis::Sway,  ControlAxis::Heave,
                                       ControlAxis::Roll,  ControlAxis::Pitch, ControlAxis::Yaw};
    const auto sendAxisRequest = [this](const ControlAxis axis, const double value,
                                        const QString &name, const QString &direction)
    {
        SixDofControlRequest request;
        switch (axis)
        {
        case ControlAxis::Surge:
            request.surge = value;
            break;
        case ControlAxis::Sway:
            request.sway = value;
            break;
        case ControlAxis::Heave:
            request.heave = value;
            break;
        case ControlAxis::Roll:
            request.roll = value;
            break;
        case ControlAxis::Pitch:
            request.pitch = value;
            break;
        case ControlAxis::Yaw:
            request.yaw = value;
            break;
        }
        emit manualControlRequested(request);
        logRequest(QStringLiteral("手动控制：%1 %2（归一化 %3）")
                       .arg(name, direction)
                       .arg(value, 0, 'f', 1));
    };

    auto addAxis = [&](QGridLayout *grid, const int index)
    {
        auto *axisWidget = new QWidget(controlCard);
        axisWidget->setObjectName(QStringLiteral("dashboardAxis"));
        auto *axisLayout = new QVBoxLayout(axisWidget);
        axisLayout->setContentsMargins(0, 0, 0, 0);
        axisLayout->setSpacing(2);
        axisLayout->addWidget(makeLabel(axisNames.at(index), QStringLiteral("axisName")), 0,
                              Qt::AlignCenter);
        axisLayout->addWidget(makeLabel(axisHints.at(index), QStringLiteral("axisHint")), 0,
                              Qt::AlignCenter);
        auto *controls = new QHBoxLayout;
        controls->setContentsMargins(0, 3, 0, 0);
        controls->setSpacing(2);
        auto *negative = axisButton(negativeArrows.at(index), axisWidget);
        auto *zero = makeLabel(formatAxisValue(0.0), QStringLiteral("axisValue"));
        auto *positive = axisButton(positiveArrows.at(index), axisWidget);
        m_axisValueLabels.append(zero);
        m_axisValues.append(0.0);
        controls->addWidget(negative);
        controls->addWidget(zero, 1, Qt::AlignCenter);
        controls->addWidget(positive);
        axisLayout->addLayout(controls);
        grid->addWidget(axisWidget, 0, index);
        const auto updateAxis = [this, sendAxisRequest, axis = controlAxes[index],
                                 name = axisNames.at(index), index,
                                 zero](const double delta, const QString &direction)
        {
            m_axisValues[index] = qBound(-1.0, m_axisValues[index] + delta, 1.0);
            zero->setText(formatAxisValue(m_axisValues[index]));
            sendAxisRequest(axis, m_axisValues[index], name, direction);
        };
        connect(negative, &QPushButton::clicked, this,
                [updateAxis]() { updateAxis(-0.1, QStringLiteral("负向")); });
        connect(positive, &QPushButton::clicked, this,
                [updateAxis]() { updateAxis(0.1, QStringLiteral("正向")); });
    };
    for (int index = 0; index < 6; ++index)
    {
        addAxis(axisGrid, index);
    }
    for (int column = 0; column < 6; ++column)
    {
        axisGrid->setColumnStretch(column, 1);
    }
    controlCard->contentLayout()->addLayout(axisGrid);

    auto *limitRow = new QHBoxLayout;
    limitRow->addWidget(makeLabel(QStringLiteral("最大推力上限"), QStringLiteral("bodyValue")));
    m_thrustLimitSlider = new QSlider(Qt::Horizontal, controlCard);
    m_thrustLimitSlider->setRange(0, 100);
    m_thrustLimitSlider->setSingleStep(5);
    limitRow->addWidget(m_thrustLimitSlider, 1);
    m_thrustLimitValue = makeLabel(QStringLiteral("--"), QStringLiteral("bodyValue"));
    limitRow->addWidget(m_thrustLimitValue);
    controlCard->contentLayout()->addLayout(limitRow);
    stateColumn->addWidget(controlCard, 1);
    topRow->addLayout(stateColumn, 6);
    root->addLayout(topRow, 1);

    auto *bottomRow = new QHBoxLayout;
    bottomRow->setSpacing(12);
    auto *summary = new CardWidget(QStringLiteral("电机状态汇总"), IconKind::Motor);
    auto *summaryGrid = new QGridLayout;
    summaryGrid->setContentsMargins(0, 0, 0, 0);
    summaryGrid->setHorizontalSpacing(8);
    summaryGrid->setVerticalSpacing(6);
    m_totalThrusterValue = makeMetricValue(QStringLiteral("--"));
    m_onlineThrusterValue = makeLabel(QStringLiteral("--"), QStringLiteral("metricValue"));
    m_offlineThrusterValue = makeLabel(QStringLiteral("--"), QStringLiteral("metricValue"));
    m_warningValue = makeLabel(QStringLiteral("--"), QStringLiteral("metricValue"));
    const QStringList summaryLabels = {QStringLiteral("总推进器"), QStringLiteral("在线"),
                                       QStringLiteral("离线"), QStringLiteral("警告")};
    QLabel *summaryValues[] = {m_totalThrusterValue, m_onlineThrusterValue, m_offlineThrusterValue,
                               m_warningValue};
    for (int column = 0; column < 4; ++column)
    {
        summaryGrid->addWidget(makeMetricLabel(summaryLabels.at(column)), 0, column,
                               Qt::AlignCenter);
        summaryGrid->addWidget(summaryValues[column], 1, column, Qt::AlignCenter);
        summaryGrid->setColumnStretch(column, 1);
    }
    m_averageRpmValue = makeMetricLabel(QStringLiteral("平均转速  --"));
    m_averageCurrentValue = makeMetricLabel(QStringLiteral("平均电流  --"));
    m_averageTemperatureValue = makeMetricLabel(QStringLiteral("平均温度  --"));
    m_rpmChart = new MiniBarChart(QColor(QStringLiteral("#66b3f3")), summary);
    m_currentChart = new MiniBarChart(QColor(QStringLiteral("#79b8ee")), summary);
    m_temperatureChart = new MiniBarChart(QColor(QStringLiteral("#8bc6f3")), summary);
    summaryGrid->addWidget(m_averageRpmValue, 2, 0);
    summaryGrid->addWidget(m_rpmChart, 2, 1);
    summaryGrid->addWidget(m_averageCurrentValue, 3, 0);
    summaryGrid->addWidget(m_currentChart, 3, 1);
    summaryGrid->addWidget(m_averageTemperatureValue, 4, 0);
    summaryGrid->addWidget(m_temperatureChart, 4, 1);
    summary->contentLayout()->addLayout(summaryGrid);
    bottomRow->addWidget(summary, 4);

    auto *alarms = new CardWidget(QStringLiteral("活动报警"), IconKind::Alarm);
    m_alarmValue = makeMetricValue(QStringLiteral("--"));
    m_alarmSummary = makeLabel(QStringLiteral("等待有效数据"), QStringLiteral("mutedLabel"));
    m_alarmSummary->setWordWrap(true);
    alarms->contentLayout()->addStretch();
    alarms->contentLayout()->addWidget(m_alarmValue, 0, Qt::AlignCenter);
    alarms->contentLayout()->addWidget(m_alarmSummary, 0, Qt::AlignCenter);
    alarms->contentLayout()->addStretch();
    bottomRow->addWidget(alarms, 4);

    auto *recording = new CardWidget(QStringLiteral("科研数据记录"), IconKind::Dashboard);
    m_recordingStatus = makeLabel(QStringLiteral("未记录"), QStringLiteral("statusWarn"));
    m_recordingStatus->setAlignment(Qt::AlignCenter);
    m_recordingCounters = makeLabel(QStringLiteral("记录电机、输入、原始 CAN；不进入绘图"),
                                    QStringLiteral("mutedLabel"));
    m_recordingCounters->setWordWrap(true);
    m_recordingCounters->setAlignment(Qt::AlignCenter);
    recording->contentLayout()->addWidget(m_recordingStatus);
    recording->contentLayout()->addWidget(m_recordingCounters);
    auto *recordButtons = new QHBoxLayout;
    m_recordingStart = makeButton(QStringLiteral("开始记录"), QStringLiteral("primaryButton"));
    m_recordingStop = makeButton(QStringLiteral("停止"), QStringLiteral("softButton"));
    m_recordingOpen = makeButton(QStringLiteral("打开目录"), QStringLiteral("softButton"));
    m_recordingStop->setEnabled(false);
    recordButtons->addWidget(m_recordingStart);
    recordButtons->addWidget(m_recordingStop);
    recordButtons->addWidget(m_recordingOpen);
    recording->contentLayout()->addLayout(recordButtons);
    connect(m_recordingStart, &QPushButton::clicked, this,
            [this]() { emit recordingStartRequested(); });
    connect(m_recordingStop, &QPushButton::clicked, this,
            [this]() { emit recordingStopRequested(); });
    connect(m_recordingOpen, &QPushButton::clicked, this,
            [this]() { emit recordingOpenDirectoryRequested(); });
    bottomRow->addWidget(recording, 4);

    auto *quick = new CardWidget(QStringLiteral("快捷操作"), IconKind::Action);
    auto *quickGrid = new QGridLayout;
    auto *arm = makeButton(QStringLiteral("解锁电机"), QStringLiteral("primaryButton"));
    auto *disarm = makeButton(QStringLiteral("停用电机"), QStringLiteral("dangerButton"));
    auto *hold = makeButton(QStringLiteral("保持位置"), QStringLiteral("softButton"));
    auto *surface = makeButton(QStringLiteral("上浮（紧急）"), QStringLiteral("softButton"));
    arm->setIcon(makeIcon(IconKind::Action));
    disarm->setIcon(makeIcon(IconKind::Status));
    hold->setIcon(makeIcon(IconKind::Manipulator));
    surface->setIcon(makeIcon(IconKind::Action));
    quickGrid->addWidget(arm, 0, 0);
    quickGrid->addWidget(disarm, 0, 1);
    quickGrid->addWidget(hold, 1, 0);
    quickGrid->addWidget(surface, 1, 1);
    quick->contentLayout()->addLayout(quickGrid);
    m_requestLog =
        makeLabel(QStringLiteral("请求已记录；未连接设备。"), QStringLiteral("mutedLabel"));
    m_requestLog->setWordWrap(true);
    quick->contentLayout()->addWidget(m_requestLog);
    connect(arm, &QPushButton::clicked, this,
            [this]()
            {
                emit armRequested();
                logRequest(QStringLiteral("请求解锁电机"));
            });
    connect(disarm, &QPushButton::clicked, this,
            [this]()
            {
                emit disarmRequested();
                logRequest(QStringLiteral("请求停用电机"));
            });
    connect(hold, &QPushButton::clicked, this,
            [this]()
            {
                emit holdPositionRequested();
                logRequest(QStringLiteral("请求保持位置"));
            });
    connect(surface, &QPushButton::clicked, this,
            [this]()
            {
                emit surfaceRequested();
                logRequest(QStringLiteral("请求紧急上浮"));
            });
    bottomRow->addWidget(quick, 4);
    root->addLayout(bottomRow);

    connect(m_enableControl, &QCheckBox::toggled, this,
            [this](const bool enabled)
            {
                emit manualControlEnableRequested(ManualControlEnableRequest{enabled});
                logRequest(enabled ? QStringLiteral("手动控制已启用（仅记录请求）")
                                   : QStringLiteral("手动控制已停用"));
            });
    connect(m_thrustLimitSlider, &QSlider::valueChanged, this,
            [this](const int value)
            {
                m_thrustLimitValue->setText(QStringLiteral("%1%").arg(value));
                emit thrustLimitRequested(ThrustLimitRequest{value});
                logRequest(QStringLiteral("推力上限请求：%1%").arg(value));
            });

    setSnapshot(preview);
}

void DashboardPage::setSnapshot(const DashboardSnapshot &snapshot)
{
    m_snapshot = snapshot;
    refreshView();
    emit snapshotAvailable(snapshot);
}

void DashboardPage::setRecordingStatus(const bool active, const quint64 accepted,
                                       const quint64 dropped, const QString &path)
{
    if (m_recordingStatus == nullptr || m_recordingCounters == nullptr)
        return;
    m_recordingStatus->setText(active ? QStringLiteral("正在记录") : QStringLiteral("未记录"));
    setTone(m_recordingStatus, active ? "good" : "warn");
    const QString fileName = path.isEmpty() ? QStringLiteral("--") : QFileInfo(path).fileName();
    m_recordingCounters->setText(
        QStringLiteral("接收 %1 · 丢弃 %2\n%3").arg(accepted).arg(dropped).arg(fileName));
    if (m_recordingStart != nullptr)
        m_recordingStart->setEnabled(!active);
    if (m_recordingStop != nullptr)
        m_recordingStop->setEnabled(active);
}

void DashboardPage::openThrusterDetails(const int index)
{
    if (index < 0 || index >= kDashboardThrusterCount)
    {
        return;
    }

    ThrusterTelemetry item;
    if (index < m_snapshot.thrusters.size())
    {
        item = m_snapshot.thrusters.at(index);
    }
    else
    {
        item.id = QStringLiteral("thruster%1").arg(index + 1);
        item.label = QStringLiteral("T%1").arg(index + 1);
        item.status = QStringLiteral("无数据");
    }

    const bool wasDisabled = m_thrusterDisabled.value(index, false);
    ThrusterDetailDialog dialog(item, wasDisabled, this);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    const bool disabled = dialog.disabled();
    if (disabled != wasDisabled)
    {
        m_thrusterDisabled[index] = disabled;
        emit thrusterCommandRequested(
            ThrusterCommandRequest{item.id, ThrusterCommand::SetEnabled, !disabled});
        logRequest(QStringLiteral("推进器 %1：%2")
                       .arg(item.label,
                            disabled ? QStringLiteral("请求停用") : QStringLiteral("请求恢复")));
    }
    if (dialog.testRequested())
    {
        emit thrusterCommandRequested(ThrusterCommandRequest{item.id, ThrusterCommand::Test, true});
        logRequest(QStringLiteral("推进器 %1：请求测试 1 秒").arg(item.label));
    }
    refreshView();
}

void DashboardPage::refreshView()
{
    const bool available = hasSystemData(m_snapshot);
    m_depthValue->setText(formatNumber(available && m_snapshot.depthValid, m_snapshot.depthM, 1,
                                       QStringLiteral(" m")));
    m_rollValue->setText(formatNumber(available && m_snapshot.attitudeValid, m_snapshot.rollDeg, 1,
                                      QStringLiteral("°")));
    m_pitchValue->setText(formatNumber(available && m_snapshot.attitudeValid, m_snapshot.pitchDeg,
                                       1, QStringLiteral("°")));
    m_yawValue->setText(formatNumber(available && m_snapshot.attitudeValid, m_snapshot.yawDeg, 1,
                                     QStringLiteral("°")));
    m_voltageValue->setText(formatNumber(available && m_snapshot.busVoltageValid,
                                         m_snapshot.busVoltageV, 3, QStringLiteral(" V")));
    m_modeValue->setText(available && !m_snapshot.robotMode.isEmpty() ? m_snapshot.robotMode
                                                                      : QStringLiteral("--"));
    m_armValue->setText(
        available ? (m_snapshot.armed ? QStringLiteral("已解锁") : QStringLiteral("已停用"))
                  : QStringLiteral("--"));
    m_temperatureValue->setText(formatNumber(available && m_snapshot.internalTemperatureValid,
                                             m_snapshot.internalTemperatureC, 1,
                                             QStringLiteral(" °C")));
    setTone(m_modeValue, "good");
    setTone(m_armValue, available && m_snapshot.armed ? "good" : "warn");

    if (m_stateUpdate != nullptr)
    {
        const QString timestamp =
            available && m_snapshot.demo.lastUpdate.isValid()
                ? m_snapshot.demo.lastUpdate.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                : QStringLiteral("--");
        m_stateUpdate->setText(QStringLiteral("更新时间：%1").arg(timestamp));
    }
    if (m_controlPermission != nullptr)
    {
        m_controlPermission->setText(m_snapshot.canControl ? QStringLiteral("允许")
                                                           : QStringLiteral("仅演示"));
        m_controlPermission->setToolTip(m_snapshot.canControl
                                            ? QStringLiteral("当前快照允许控制")
                                            : m_snapshot.controlUnavailableReason);
        m_controlPermission->setObjectName(m_snapshot.canControl ? QStringLiteral("statusGood")
                                                                 : QStringLiteral("statusWarn"));
        setTone(m_controlPermission, m_snapshot.canControl ? "good" : "warn");
    }
    if (m_thrustLimitSlider != nullptr)
    {
        const QSignalBlocker blocker(m_thrustLimitSlider);
        m_thrustLimitSlider->setValue(qBound(0, m_snapshot.thrustLimitPercent, 100));
    }
    if (m_thrustLimitValue != nullptr)
    {
        m_thrustLimitValue->setText(available
                                        ? QStringLiteral("%1%").arg(m_snapshot.thrustLimitPercent)
                                        : QStringLiteral("--"));
    }

    QVector<double> rpmValues;
    QVector<double> currentValues;
    QVector<double> temperatureValues;
    int online = 0;
    int offline = 0;
    double rpmTotal = 0.0;
    double currentTotal = 0.0;
    double temperatureTotal = 0.0;
    int numericCount = 0;
    for (int i = 0; i < kDashboardThrusterCount; ++i)
    {
        const bool present = i < m_snapshot.thrusters.size();
        if (present)
        {
            const ThrusterTelemetry &item = m_snapshot.thrusters.at(i);
            const bool disabled = m_thrusterDisabled.value(i, false);
            const bool itemValid = item.stamp.validity == DataValidity::Valid &&
                                   item.stamp.freshness != DataFreshness::Offline;
            const bool itemOnline = item.status == QStringLiteral("在线");
            itemOnline ? ++online : ++offline;
            if (itemValid)
            {
                rpmTotal += item.rpm;
                currentTotal += item.currentA;
                temperatureTotal += item.temperatureC;
                ++numericCount;
                rpmValues.append(item.rpm);
                currentValues.append(item.currentA);
                temperatureValues.append(item.temperatureC);
            }
            m_thrusterRpmValues.at(i)->setText(itemValid ? QString::number(item.rpm, 'f', 0)
                                                         : QStringLiteral("--"));
            m_thrusterCurrentValues.at(i)->setText(
                itemValid ? QStringLiteral("%1 A").arg(item.currentA, 0, 'f', 4)
                          : QStringLiteral("--"));
            m_thrusterTemperatureValues.at(i)->setText(
                itemValid ? QStringLiteral("%1 °C").arg(item.temperatureC, 0, 'f', 1)
                          : QStringLiteral("--"));
            m_thrusterStatusValues.at(i)->setText(disabled ? QStringLiteral("停用")
                                                           : QStringLiteral("●"));
            m_thrusterStatusValues.at(i)->setToolTip(disabled ? QStringLiteral("已在页面中停用")
                                                              : item.status);
            setTone(m_thrusterStatusValues.at(i),
                    disabled ? "bad" : (itemOnline ? "good" : "warn"));
        }
        else
        {
            ++offline;
            m_thrusterRpmValues.at(i)->setText(QStringLiteral("--"));
            m_thrusterCurrentValues.at(i)->setText(QStringLiteral("--"));
            m_thrusterTemperatureValues.at(i)->setText(QStringLiteral("--"));
            const bool disabled = m_thrusterDisabled.value(i, false);
            m_thrusterStatusValues.at(i)->setText(disabled ? QStringLiteral("停用")
                                                           : QStringLiteral("●"));
            m_thrusterStatusValues.at(i)->setToolTip(disabled ? QStringLiteral("已在页面中停用")
                                                              : QStringLiteral("无数据"));
            setTone(m_thrusterStatusValues.at(i), disabled ? "bad" : "warn");
        }
    }

    m_totalThrusterValue->setText(available ? QString::number(m_snapshot.thrusters.size())
                                            : QStringLiteral("--"));
    m_onlineThrusterValue->setText(available ? QString::number(online) : QStringLiteral("--"));
    m_offlineThrusterValue->setText(available ? QString::number(offline) : QStringLiteral("--"));
    m_warningValue->setText(available ? QString::number(m_snapshot.alarmCount)
                                      : QStringLiteral("--"));
    setTone(m_onlineThrusterValue, "good");
    setTone(m_offlineThrusterValue, offline > 0 ? "warn" : "good");
    setTone(m_warningValue, m_snapshot.alarmCount > 0 ? "bad" : "good");
    if (numericCount > 0)
    {
        m_averageRpmValue->setText(
            QStringLiteral("平均转速  %1").arg(rpmTotal / numericCount, 0, 'f', 0));
        m_averageCurrentValue->setText(
            QStringLiteral("平均电流  %1 A").arg(currentTotal / numericCount, 0, 'f', 4));
        m_averageTemperatureValue->setText(
            QStringLiteral("平均温度  %1 °C").arg(temperatureTotal / numericCount, 0, 'f', 3));
    }
    else
    {
        m_averageRpmValue->setText(QStringLiteral("平均转速  --"));
        m_averageCurrentValue->setText(QStringLiteral("平均电流  --"));
        m_averageTemperatureValue->setText(QStringLiteral("平均温度  --"));
    }
    static_cast<MiniBarChart *>(m_rpmChart)->setValues(rpmValues);
    static_cast<MiniBarChart *>(m_currentChart)->setValues(currentValues);
    static_cast<MiniBarChart *>(m_temperatureChart)->setValues(temperatureValues);

    if (!available)
    {
        m_alarmValue->setText(QStringLiteral("--"));
        m_alarmSummary->setText(QStringLiteral("等待有效数据"));
    }
    else if (m_snapshot.alarmCount == 0)
    {
        m_alarmValue->setText(QStringLiteral("无活动报警"));
        m_alarmSummary->setText(QStringLiteral("所有系统正常。"));
    }
    else
    {
        m_alarmValue->setText(QStringLiteral("%1 条活动报警").arg(m_snapshot.alarmCount));
        if (m_snapshot.alarms.isEmpty())
        {
            m_alarmSummary->setText(QStringLiteral("请查看系统报警详情。"));
        }
        else
        {
            QStringList alarmLines;
            for (int i = 0; i < qMin(3, m_snapshot.alarms.size()); ++i)
            {
                alarmLines.append(m_snapshot.alarms.at(i));
            }
            m_alarmSummary->setText(alarmLines.join(QStringLiteral("\n")));
        }
    }
}

void DashboardPage::logRequest(const QString &message)
{
    if (m_requestLog != nullptr)
    {
        m_requestLog->setText(QStringLiteral("最近请求：%1 · 仅演示").arg(message));
    }
}

} // namespace rov
