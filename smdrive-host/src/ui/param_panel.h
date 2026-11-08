// 参数面板：10 路浮点参数下发。方案(JSON)保存/加载由「文件」菜单触发（低频操作），
// 通过 promptSaveProfile()/promptLoadProfile() 弹出对话框。
// 关键点：即使输入 0，也必须发出合法 4 字节大端浮点（修掉原版半字节错位缺陷）。
#pragma once

#include <QVector>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QDoubleSpinBox;
QT_END_NAMESPACE

namespace smd {

class ParamPanel : public QWidget
{
    Q_OBJECT
public:
    explicit ParamPanel(QWidget *parent = nullptr);

    QVector<double> values() const;
    void setValues(const QVector<double> &values);

    bool saveProfile(const QString &path, QString *error) const;
    bool loadProfile(const QString &path, QString *error);

public slots:
    void promptSaveProfile();          // 「文件 → 保存参数方案…」
    void promptLoadProfile();          // 「文件 → 加载参数方案…」

signals:
    void requestParam(int index0, float value);
    void requestAllParams(const QVector<double> &values);
    void profileStatus(const QString &text);   // 供状态栏提示（成功/失败）

private:
    QVector<QDoubleSpinBox *> m_spins;
};

} // namespace smd
