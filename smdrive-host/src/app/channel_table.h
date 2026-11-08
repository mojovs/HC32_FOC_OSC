// 通道/参数元数据单一维护点（计划书 §5.2）。名称与顺序取自固件
// user/pc_communication_init.c，改固件时同步改这里。
#pragma once

#include <QColor>
#include <QString>

#include <array>

namespace smd {

struct ChannelInfo {
    QString name;        // 图例短名
    QString desc;        // 说明
    QColor color;
    bool isInt;          // 整型波形（固件 int_wave_data_*）
    bool visible;        // 默认是否显示
};

struct ParamInfo {
    QString name;        // 固件变量名
    QString desc;        // 中文说明
    double minValue;
    double maxValue;
    double step;
    double defaultValue;
};

// 6 路浮点波形 + 4 路整型波形
inline const std::array<ChannelInfo, 10> &channelTable()
{
    static const std::array<ChannelInfo, 10> table = {{
        {"theta",   "电角度 theta",              QColor(0xE6, 0x39, 0x46), false, true},
        {"EKF[0]",  "EKF 状态 0",                QColor(0xF7, 0x8C, 0x6B), false, true},
        {"EKF[1]",  "EKF 状态 1",                QColor(0xF9, 0xC7, 0x4F), false, false},
        {"EKF[2]",  "EKF 状态 2",                QColor(0x2A, 0x9D, 0x8F), false, false},
        {"Ibeta",   "β 轴电流 Ibeta",            QColor(0x1D, 0x35, 0x57), false, true},
        {"Iq_ref",  "q 轴电流给定 Iq_ref",       QColor(0x8E, 0x7C, 0xC3), false, true},
        {"ia_test", "A 相电流原始值 ia_test",    QColor(0xFF, 0x9F, 0x1C), true,  true},
        {"intw2",   "整型波形 2 (int_test2)",    QColor(0x45, 0x7B, 0x9D), true,  false},
        {"intw3",   "整型波形 3 (int_test3)",    QColor(0xA1, 0x6E, 0x3C), true,  false},
        {"intw4",   "整型波形 4 (int_test4)",    QColor(0x6A, 0x99, 0x4D), true,  false},
    }};
    return table;
}

// 06 命令下传的 10 路浮点参数（线上 idx = index + 1）
inline const std::array<ParamInfo, 10> &paramTable()
{
    static const std::array<ParamInfo, 10> table = {{
        {"SPEED_PI_P", "速度环比例增益",          0.0, 1000.0, 0.01,  0.0},
        {"SPEED_PI_I", "速度环积分增益",          0.0, 1000.0, 0.001, 0.0},
        {"R_0_0",      "EKF 过程噪声 R(0,0)",     0.0, 1.0,    0.0001, 0.0},
        {"R_1_1",      "EKF 过程噪声 R(1,1)",     0.0, 1.0,    0.0001, 0.0},
        {"Q_0_0",      "EKF 观测噪声 Q(0,0)",     0.0, 1000.0, 0.01,  0.0},
        {"Q_1_1",      "EKF 观测噪声 Q(1,1)",     0.0, 1000.0, 0.01,  0.0},
        {"Q_2_2",      "EKF 观测噪声 Q(2,2)",     0.0, 1000.0, 0.01,  0.0},
        {"Q_3_3",      "EKF 观测噪声 Q(3,3)",     0.0, 1000.0, 0.01,  0.0},
        {"flux",       "永磁磁链",                0.0, 10.0,   0.0001, 0.0},
        {"spare",      "备用槽(固件映射 Q_0_0)",  0.0, 1000.0, 0.01,  0.0},
    }};
    return table;
}

// 数据表 10 路浮点（固件 float_data_upload 0..9）
inline const std::array<QString, 10> &floatDataNames()
{
    static const std::array<QString, 10> names = {
        "L_RF[0]", "L_RF[1]", "L_RF[2]", "Ibeta", "float_test2",
        "Ialpha", "Ibeta", "Q_3_3", "Q_0_0", "Q_0_0"
    };
    return names;
}

// 数据表 10 路整型（固件 int_data_upload 0..9）
inline const std::array<QString, 10> &intDataNames()
{
    static const std::array<QString, 10> names = {
        "int_test1", "int_test2", "int_test3", "int_test4", "int_test5",
        "int_test6", "int_test7", "int_test8", "int_test9", "int_test10"
    };
    return names;
}

} // namespace smd
