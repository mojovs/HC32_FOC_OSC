// 数据表：10 路浮点 + 10 路整型实时数值（对标原版右上/右下两张表）
#pragma once

#include <QWidget>

QT_BEGIN_NAMESPACE
class QTableWidget;
QT_END_NAMESPACE

namespace smd {

class TablePanel : public QWidget
{
    Q_OBJECT
public:
    explicit TablePanel(QWidget *parent = nullptr);

    void setValues(const float *floats, int floatCount, const quint32 *ints, int intCount);

private:
    QTableWidget *m_table = nullptr;
};

} // namespace smd
