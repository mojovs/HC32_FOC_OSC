#include "ui/table_panel.h"

#include "app/channel_table.h"

#include <QHeaderView>
#include <QTableWidget>
#include <QVBoxLayout>

namespace smd {

namespace {
constexpr int kFloatRows = 10;
constexpr int kIntRows = 10;
constexpr int kRows = kFloatRows + kIntRows;
}

TablePanel::TablePanel(QWidget *parent)
    : QWidget(parent)
{
    m_table = new QTableWidget(kRows, 2, this);
    m_table->setHorizontalHeaderLabels({QStringLiteral("项目"), QStringLiteral("数值")});
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setAlternatingRowColors(true);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnWidth(0, 180);

    const auto &fnames = floatDataNames();
    const auto &inames = intDataNames();
    for (int i = 0; i < kFloatRows; ++i) {
        m_table->setItem(i, 0, new QTableWidgetItem(QStringLiteral("F%1 %2").arg(i + 1).arg(fnames[size_t(i)])));
        m_table->setItem(i, 1, new QTableWidgetItem(QStringLiteral("0")));
    }
    for (int i = 0; i < kIntRows; ++i) {
        const int row = kFloatRows + i;
        m_table->setItem(row, 0, new QTableWidgetItem(QStringLiteral("I%1 %2").arg(i + 1).arg(inames[size_t(i)])));
        m_table->setItem(row, 1, new QTableWidgetItem(QStringLiteral("0")));
    }

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_table);
}

void TablePanel::setValues(const float *floats, int floatCount, const quint32 *ints, int intCount)
{
    for (int i = 0; i < kFloatRows && i < floatCount; ++i) {
        if (auto *item = m_table->item(i, 1)) {
            const float v = floats[i];
            const QString text = (std::abs(v) >= 1e5f || (std::abs(v) < 1e-4f && v != 0.0f))
                                     ? QString::number(double(v), 'e', 6)
                                     : QString::number(double(v), 'f', 6);
            item->setText(text);
        }
    }
    for (int i = 0; i < kIntRows && i < intCount; ++i) {
        if (auto *item = m_table->item(kFloatRows + i, 1))
            item->setText(QString::number(qint32(ints[i])));   // 按有符号显示，兼容负值
    }
}

} // namespace smd
