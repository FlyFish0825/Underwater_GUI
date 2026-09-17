#include "pages/settings/SettingsPlaceholder.h"

#include "ui/common/UiPrimitives.h"

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
    card->contentLayout()->addStretch();
    card->contentLayout()->addWidget(
        makeLabel(QStringLiteral("配置界面预留。"), QStringLiteral("metricValue")), 0,
        Qt::AlignCenter);
    card->contentLayout()->addWidget(
        makeLabel(QStringLiteral("尚未实现。"), QStringLiteral("mutedLabel")), 0, Qt::AlignCenter);
    card->contentLayout()->addStretch();
    root->addWidget(card, 1);
}

} // namespace rov
