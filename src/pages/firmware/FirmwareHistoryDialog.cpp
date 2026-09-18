#include "pages/firmware/FirmwareHistoryDialog.h"

#include "data/store/FirmwareHistoryStore.h"
#include "pages/firmware/FirmwareLogFormatter.h"
#include "ui/common/AppComboBox.h"
#include "ui/common/AppFluentButton.h"
#include "ui/common/AppLineEdit.h"
#include "ui/common/UiPrimitives.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextEdit>
#include <QVBoxLayout>

namespace
{

bool isUpgradeMessage(const QString &message)
{
    const QString upper = message.toUpper();
    static const QStringList keywords = {
        QStringLiteral("SESSION_BEGIN"), QStringLiteral("SESSION_CRC32"),
        QStringLiteral("ERASE"),         QStringLiteral("WRITE"),
        QStringLiteral("WRITE_END"),     QStringLiteral("VERIFY"),
        QStringLiteral("COMMIT"),        QStringLiteral("MISSING_"),
        QStringLiteral("FULL_STREAM"),   QStringLiteral("ROLLBACK"),
    };
    for (const QString &keyword : keywords)
    {
        if (upper.contains(keyword))
            return true;
    }
    return false;
}

} // namespace

namespace rov
{

FirmwareHistoryDialog::FirmwareHistoryDialog(FirmwareHistoryStore *store, QWidget *parent)
    : QDialog(parent), m_store(store)
{
    setWindowTitle(QStringLiteral("固件升级历史记录"));
    setObjectName(QStringLiteral("historyDialog"));
    // 复用主界面的品牌图标，避免 Windows 标题栏显示默认空白图标。
    setWindowIcon(makeIcon(IconKind::Brand));
    // 顶层窗口的初始尺寸由 FirmwarePage::showHistory() 按主窗口动态设置。
    // 这里保留一个足够大的默认值，避免在窗口尚未完成布局时出现极小窗口。
    resize(900, 600);
    auto *root = new QVBoxLayout(this);
    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(QStringLiteral("历史事件（实时日志清理不会删除；升级会话按整块显示）")));
    m_searchEdit = new AppLineEdit;
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索节点、命令、CAN ID 或关键字"));
    m_searchEdit->setMinimumWidth(260);
    top->addWidget(m_searchEdit, 1);
    m_typeFilter = new AppComboBox;
    m_typeFilter->addItems({QStringLiteral("全部"), QStringLiteral("发送"), QStringLiteral("接收"),
                            QStringLiteral("错误")});
    top->addWidget(m_typeFilter);
    m_pageSizeCombo = new AppComboBox;
    m_pageSizeCombo->addItem(QStringLiteral("50 条/页"), 50);
    m_pageSizeCombo->addItem(QStringLiteral("100 条/页"), 100);
    m_pageSizeCombo->addItem(QStringLiteral("200 条/页"), 200);
    m_pageSizeCombo->setCurrentIndex(1);
    top->addWidget(m_pageSizeCombo);
    auto *reloadButton = new AppFluentButton(QStringLiteral("刷新"));
    top->addWidget(reloadButton);
    root->addLayout(top);
    m_view = new QTextBrowser;
    m_view->setReadOnly(true);
    m_view->setLineWrapMode(QTextEdit::NoWrap);
    m_view->setOpenLinks(false);
    m_view->setOpenExternalLinks(false);
    root->addWidget(m_view);

    auto *bottom = new QHBoxLayout;
    m_pageInfo = new QLabel;
    bottom->addWidget(m_pageInfo);
    bottom->addStretch();
    m_previousButton = new AppFluentButton(QStringLiteral("上一页"));
    m_nextButton = new AppFluentButton(QStringLiteral("下一页"));
    bottom->addWidget(m_previousButton);
    bottom->addWidget(m_nextButton);
    root->addLayout(bottom);

    connect(reloadButton, &QPushButton::clicked, this, &FirmwareHistoryDialog::reload);
    connect(m_searchEdit, &QLineEdit::textChanged, this,
            [this]()
            {
                m_page = 0;
                rebuildIndex();
            });
    connect(m_typeFilter, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this]()
            {
                m_page = 0;
                rebuildIndex();
            });
    connect(m_pageSizeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int index)
            {
                m_pageSize = m_pageSizeCombo->itemData(index).toInt();
                m_page = 0;
                rebuildIndex();
            });
    connect(m_previousButton, &QPushButton::clicked, this,
            [this]()
            {
                if (m_page > 0)
                {
                    --m_page;
                    renderPage();
                }
            });
    connect(m_nextButton, &QPushButton::clicked, this,
            [this]()
            {
                if (m_page + 1 < m_pages.size())
                {
                    ++m_page;
                    renderPage();
                }
            });
    reload();
}

void FirmwareHistoryDialog::reload()
{
    if (m_store == nullptr)
        return;
    m_entries = m_store->load();
    m_page = 0;
    rebuildIndex();
}

bool FirmwareHistoryDialog::matchesFilter(const FirmwareHistoryEntry &entry) const
{
    const QString message = entry.message;
    const QString keyword = m_searchEdit == nullptr ? QString() : m_searchEdit->text().trimmed();
    if (!keyword.isEmpty() && !message.contains(keyword, Qt::CaseInsensitive))
        return false;

    const QString filter = m_typeFilter == nullptr ? QStringLiteral("全部")
                                                    : m_typeFilter->currentText();
    if (filter == QStringLiteral("发送"))
        return message.startsWith(QStringLiteral("TX "));
    if (filter == QStringLiteral("接收"))
        return message.startsWith(QStringLiteral("RX ")) || message.startsWith(QStringLiteral("Peer "))
               || message.startsWith(QStringLiteral("解析到 CAN"));
    if (filter == QStringLiteral("错误"))
        return message.contains(QStringLiteral("错误")) || message.contains(QStringLiteral("失败"))
               || message.contains(QStringLiteral("超时")) || message.contains(QStringLiteral("未连接"))
               || message.contains(QStringLiteral("断开"));
    return true;
}

void FirmwareHistoryDialog::rebuildIndex()
{
    m_filteredIndexes.clear();
    // 新记录在后面写入，索引倒序后最新事件总是出现在第一页。
    for (int index = m_entries.size() - 1; index >= 0; --index)
    {
        if (matchesFilter(m_entries.at(index)))
            m_filteredIndexes.append(index);
    }
    rebuildPages();
    renderPage();
}

void FirmwareHistoryDialog::rebuildPages()
{
    m_pages.clear();
    if (m_pageSize <= 0)
        m_pageSize = 100;

    // 普通日志按页切分；连续的升级传输日志（尤其是大量 WRITE）视为一个整体，
    // 即使超过页容量也不拆开，用户可以在同一页的滚动区域查看完整 APP 包。
    int cursor = 0;
    while (cursor < m_filteredIndexes.size())
    {
        QVector<int> page;
        while (cursor < m_filteredIndexes.size())
        {
            page.append(m_filteredIndexes.at(cursor));
            ++cursor;
            if (page.size() < m_pageSize || cursor >= m_filteredIndexes.size())
                continue;

            const bool currentIsUpgrade = isUpgradeMessage(m_entries.at(page.constLast()).message);
            const bool nextIsUpgrade = isUpgradeMessage(m_entries.at(m_filteredIndexes.at(cursor)).message);
            if (!currentIsUpgrade || !nextIsUpgrade)
                break;
        }
        m_pages.append(page);
    }
}

void FirmwareHistoryDialog::renderPage()
{
    if (m_view == nullptr)
        return;
    const int pageCount = m_pages.size();
    if (pageCount == 0)
        m_page = 0;
    else
        m_page = qBound(0, m_page, pageCount - 1);

    QStringList lines;
    const QVector<int> page = pageCount > 0 ? m_pages.at(m_page) : QVector<int>();
    for (const int index : page)
    {
        const auto &entry = m_entries.at(index);
        const QString timestamped =
            QStringLiteral("[%1] %2")
                .arg(entry.timestamp.toString("yyyy-MM-dd HH:mm:ss.zzz"), entry.message);
        lines.append(formatFirmwareLogHtml(timestamped));
    }
    if (lines.isEmpty())
        lines.append(QStringLiteral("<span style=\"color:#8798aa;\">暂无匹配的历史记录</span>"));
    m_view->setHtml(lines.join(QStringLiteral("<br/>")));
    m_view->moveCursor(QTextCursor::Start);
    updatePageControls();
}

void FirmwareHistoryDialog::updatePageControls()
{
    const int total = m_filteredIndexes.size();
    const int pageCount = m_pages.size();
    if (m_pageInfo != nullptr)
    {
        m_pageInfo->setText(pageCount == 0
                                ? QStringLiteral("共 0 条")
                                : QStringLiteral("第 %1/%2 页 · 共 %3 条")
                                      .arg(m_page + 1)
                                      .arg(pageCount)
                                      .arg(total));
    }
    if (m_previousButton != nullptr)
        m_previousButton->setEnabled(m_page > 0);
    if (m_nextButton != nullptr)
        m_nextButton->setEnabled(pageCount > 0 && m_page + 1 < pageCount);
}

} // namespace rov
