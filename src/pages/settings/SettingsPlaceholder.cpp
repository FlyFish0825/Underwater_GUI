#include "pages/settings/SettingsPlaceholder.h"

#include "ui/common/AppFluentButton.h"
#include "ui/common/AppProgressRing.h"
#include "ui/common/AppToggleSwitch.h"
#include "ui/common/UiPrimitives.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace rov
{

SettingsPlaceholder::SettingsPlaceholder(QWidget *parent) : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 10);
    root->setSpacing(10);
    root->addWidget(makePageHeader(QStringLiteral("设置"), QStringLiteral("配置界面预留。"),
                                   QStringLiteral("占位页面")));
    auto *card = new CardWidget(QStringLiteral("设置"), IconKind::Settings);

    // 仅在低风险的设置占位页验证成熟控件库的视觉和交互；不触碰导航、业务状态或通信逻辑。
    auto *intro = makeLabel(QStringLiteral("控件库接入验证（不影响现有功能）"),
                            QStringLiteral("metricValue"));
    card->contentLayout()->addWidget(intro);

    auto *switchRow = new QHBoxLayout();
    switchRow->setContentsMargins(0, 0, 0, 0);
    switchRow->setSpacing(12);
    switchRow->addWidget(makeLabel(QStringLiteral("启用 Fluent 控件演示")));
    auto *toggle = new AppToggleSwitch(card);
    toggle->setOnContent(QStringLiteral("开"));
    toggle->setOffContent(QStringLiteral("关"));
    switchRow->addWidget(toggle, 0, Qt::AlignVCenter);
    switchRow->addStretch();
    card->contentLayout()->addLayout(switchRow);

    auto *actionRow = new QHBoxLayout();
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->setSpacing(10);
    auto *button = new AppFluentButton(QStringLiteral("测试按钮"), card);
    button->setFluentStyle(fluent::basicinput::Button::Accent);
    auto *ring = new AppProgressRing(card);
    ring->setFixedSize(24, 24);
    ring->setIsActive(false);
    auto *state = makeLabel(QStringLiteral("等待操作。"), QStringLiteral("mutedLabel"));
    actionRow->addWidget(button);
    actionRow->addWidget(ring, 0, Qt::AlignVCenter);
    actionRow->addWidget(state, 1);
    card->contentLayout()->addLayout(actionRow);

    QObject::connect(button, &QPushButton::clicked, card,
                     [state]() { state->setText(QStringLiteral("按钮交互正常。")); });
    QObject::connect(toggle, &AppToggleSwitch::toggled, card,
                     [ring, state](const bool enabled)
                     {
                         ring->setIsActive(enabled);
                         state->setText(enabled ? QStringLiteral("开关已开启。")
                                                : QStringLiteral("开关已关闭。"));
                     });

    card->contentLayout()->addStretch();
    root->addWidget(card, 1);
}

} // namespace rov
