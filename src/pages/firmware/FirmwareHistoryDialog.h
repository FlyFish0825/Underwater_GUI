#pragma once

#include "data/store/FirmwareHistoryStore.h"

#include <QDialog>
#include <QVector>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTextBrowser;

namespace rov
{

class FirmwareHistoryDialog final : public QDialog
{
    Q_OBJECT

  public:
    explicit FirmwareHistoryDialog(FirmwareHistoryStore *store, QWidget *parent = nullptr);

  private:
    void reload();
    void rebuildIndex();
    void rebuildPages();
    void renderPage();
    void updatePageControls();
    bool matchesFilter(const FirmwareHistoryEntry &entry) const;

    FirmwareHistoryStore *m_store = nullptr;
    QTextBrowser *m_view = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QComboBox *m_typeFilter = nullptr;
    QComboBox *m_pageSizeCombo = nullptr;
    QLabel *m_pageInfo = nullptr;
    QPushButton *m_previousButton = nullptr;
    QPushButton *m_nextButton = nullptr;
    QVector<FirmwareHistoryEntry> m_entries;
    QVector<int> m_filteredIndexes;
    QVector<QVector<int>> m_pages;
    int m_page = 0;
    int m_pageSize = 100;
};

} // namespace rov
