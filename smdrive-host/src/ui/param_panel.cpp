#include "ui/param_panel.h"

#include "app/channel_table.h"

#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace smd {

ParamPanel::ParamPanel(QWidget *parent)
    : QWidget(parent)
{
    const auto &table = paramTable();
    auto *box = new QGroupBox(QStringLiteral("参数下发（06 命令：55 55 06 <idx> <f32BE> AA AA）"), this);
    auto *grid = new QGridLayout(box);
    grid->addWidget(new QLabel(QStringLiteral("idx")), 0, 0);
    grid->addWidget(new QLabel(QStringLiteral("参数")), 0, 1);
    grid->addWidget(new QLabel(QStringLiteral("数值")), 0, 2);
    grid->addWidget(new QLabel(QStringLiteral("下发")), 0, 3);

    for (int i = 0; i < int(table.size()); ++i) {
        const ParamInfo &info = table[size_t(i)];
        const int row = i + 1;
        grid->addWidget(new QLabel(QStringLiteral("%1").arg(i + 1, 2, 10, QLatin1Char('0'))), row, 0);
        auto *name = new QLabel(QStringLiteral("%1  (%2)").arg(info.name, info.desc));
        name->setToolTip(QStringLiteral("固件变量 %1").arg(info.name));
        grid->addWidget(name, row, 1);

        auto *spin = new QDoubleSpinBox(box);
        spin->setDecimals(6);
        spin->setRange(info.minValue, info.maxValue);
        spin->setSingleStep(info.step);
        spin->setValue(info.defaultValue);
        spin->setMinimumWidth(140);
        m_spins.append(spin);
        grid->addWidget(spin, row, 2);

        auto *btn = new QPushButton(QStringLiteral("发送"), box);
        connect(btn, &QPushButton::clicked, this, [this, i, spin] {
            emit requestParam(i, float(spin->value()));
        });
        grid->addWidget(btn, row, 3);
    }

    auto *sendAll = new QPushButton(QStringLiteral("全部下发"), box);
    connect(sendAll, &QPushButton::clicked, this, [this] { emit requestAllParams(values()); });

    auto *saveBtn = new QPushButton(QStringLiteral("保存方案…"), box);
    connect(saveBtn, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("保存参数方案"),
                                                         QStringLiteral("params.json"),
                                                         QStringLiteral("JSON (*.json)"));
        if (path.isEmpty())
            return;
        QString err;
        if (!saveProfile(path, &err))
            QMessageBox::warning(this, QStringLiteral("保存失败"), err);
    });

    auto *loadBtn = new QPushButton(QStringLiteral("加载方案…"), box);
    connect(loadBtn, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("加载参数方案"),
                                                         QString(), QStringLiteral("JSON (*.json)"));
        if (path.isEmpty())
            return;
        QString err;
        if (!loadProfile(path, &err))
            QMessageBox::warning(this, QStringLiteral("加载失败"), err);
    });

    const int row = int(table.size()) + 1;
    grid->addWidget(sendAll, row, 1);
    grid->addWidget(saveBtn, row, 2);
    grid->addWidget(loadBtn, row, 3);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(box);
    layout->addStretch(1);
}

QVector<double> ParamPanel::values() const
{
    QVector<double> out;
    out.reserve(m_spins.size());
    for (auto *spin : m_spins)
        out.append(spin->value());
    return out;
}

void ParamPanel::setValues(const QVector<double> &values)
{
    for (int i = 0; i < m_spins.size() && i < values.size(); ++i)
        m_spins[i]->setValue(values[i]);
}

bool ParamPanel::saveProfile(const QString &path, QString *error) const
{
    const auto &table = paramTable();
    QJsonArray params;
    for (int i = 0; i < int(table.size()) && i < m_spins.size(); ++i) {
        QJsonObject o;
        o.insert(QStringLiteral("index"), i + 1);                 // 线上 idx
        o.insert(QStringLiteral("name"), table[size_t(i)].name);
        o.insert(QStringLiteral("value"), m_spins[i]->value());
        params.append(o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("smdrive-param-profile"));
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("params"), params);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QStringLiteral("无法写入 %1: %2").arg(path, f.errorString());
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool ParamPanel::loadProfile(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取 %1: %2").arg(path, f.errorString());
        return false;
    }
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = QStringLiteral("JSON 解析失败: %1").arg(perr.errorString());
        return false;
    }
    const QJsonArray params = doc.object().value(QStringLiteral("params")).toArray();
    if (params.isEmpty()) {
        if (error)
            *error = QStringLiteral("方案里没有 params 数组");
        return false;
    }
    const auto &table = paramTable();
    for (const QJsonValue &v : params) {
        const QJsonObject o = v.toObject();
        const int idx = o.value(QStringLiteral("index")).toInt(0);
        if (idx < 1 || idx > m_spins.size())
            continue;
        const double value = o.value(QStringLiteral("value")).toDouble();
        const QString name = o.value(QStringLiteral("name")).toString();
        if (!name.isEmpty() && name != table[size_t(idx - 1)].name) {
            if (error)
                *error = QStringLiteral("idx %1 的名称不匹配（方案 %2 / 当前 %3）")
                             .arg(idx).arg(name, table[size_t(idx - 1)].name);
            return false;
        }
        m_spins[idx - 1]->setValue(value);
    }
    return true;
}

} // namespace smd
