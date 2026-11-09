// 自绘实时波形（QPainter + 每像素列 min/max 抽点）——避开第三方绘图库的授权问题
// （计划书 §3.2：自绘最干净），性能策略等价于 §5.6。
#pragma once

#include <QWidget>

QT_BEGIN_NAMESPACE
class QPainter;
class QResizeEvent;
class QScrollBar;
QT_END_NAMESPACE

namespace smd {

class DataHub;

class WaveformView : public QWidget
{
    Q_OBJECT
public:
    explicit WaveformView(QWidget *parent = nullptr);

    void setDataHub(DataHub *hub) { m_hub = hub; }
    void setWindowSeconds(double s);
    double windowSeconds() const { return m_windowSeconds; }
    void setChannelVisible(int channel, bool on);
    bool channelVisible(int channel) const;
    void setOffset(int channel, double value);
    double offset(int channel) const;
    /// 每通道显示增益（示波器的 V/div 等价物）：显示值 = 原始值 × gain − offset
    void setGain(int channel, double gain);
    double gain(int channel) const;
    void setAutoScale(bool on) { m_autoScale = on; update(); }
    bool autoScale() const { return m_autoScale; }
    void setYRange(double lo, double hi);
    /// 暂停 = 冻结视窗（右端不再跟随最新样本）；恢复后回到实时跟随。
    void setPaused(bool on);
    bool paused() const { return m_paused; }
    /// 示波器式光标测量：X1/X2 竖线标时间、Y1/Y2 横线标数值，并在各「测量通道」的
    /// 波形上取点读出 ΔV；右上角输出一张测量表格。
    void setCursorMeasure(bool on);
    bool cursorMeasure() const { return m_measure; }
    void setChannelMeasured(int channel, bool on);
    bool channelMeasured(int channel) const;
    void resetView();                     // 回到实时跟随 + 自动量程
    void markDataAppended();              // 由 UI 定时器调用，触发重绘排队

signals:
    /// 拖动/进度条导致自动暂停时，通知主窗口同步「暂停显示」动作的勾选态。
    void pausedChanged(bool on);

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    void drawGrid(QPainter &p, const QRectF &plot) const;
    void drawCurves(QPainter &p, const QRectF &plot, double startSample, double endSample,
                    double ymin, double ymax) const;
    void drawLegend(QPainter &p, const QRectF &plot) const;
    void drawCursor(QPainter &p, const QRectF &plot, double startSample, double endSample) const;
    void drawMeasureCursors(QPainter &p, const QRectF &plot) const;
    int nearestHandle(const QPointF &pos) const;      // 距离最近的光标手柄，-1 为无
    void dragHandleTo(const QPointF &pos);             // 把当前手柄拖到鼠标处
    double sampleValueAt(int channel, double sampleAbs) const;   // 通道在样本处的显示值，无数据为 NaN
    void syncScrollBar(double startSample, double endSample);   // 视窗 -> 底部进度条
    QRectF plotRect() const;

    DataHub *m_hub = nullptr;
    double m_windowSeconds = 0.2;
    int m_channelCount = 10;
    bool m_visible[10] = {true, true, false, false, true, true, true, false, false, false};
    double m_gains[10] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    double m_offsets[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    bool m_autoScale = true;
    double m_manualMin = -1.0;
    double m_manualMax = 1.0;
    bool m_paused = false;
    double m_viewEnd = 0.0;               // 视窗右端（绝对样本号），唯一位置真源
    bool m_dragging = false;
    QPoint m_dragAnchor;
    double m_dragStartViewEnd = 0.0;
    int m_cursorX = -1;
    bool m_repaintQueued = false;
    QScrollBar *m_scroll = nullptr;       // 底部时间进度条（历史定位）
    bool m_syncingScroll = false;         // 回写进度条时抑制 valueChanged 递归

    // 示波器式双光标测量
    bool m_measure = false;
    bool m_cursorsInit = false;           // 打开测量时按当前视窗摆一次光标
    double m_mx1 = 0.0, m_mx2 = 0.0;      // X 光标位置（绝对样本号）
    double m_my1 = 0.0, m_my2 = 0.0;      // Y 光标位置（显示值，含增益偏移）
    bool m_measured[10] = {true, true, false, false, true, true, true, false, false, false};
    int m_activeHandle = -1;              // 正在拖动的光标：0=X1 1=X2 2=Y1 3=Y2
    int m_hoverHandle = -1;               // 鼠标悬停靠近的光标（高亮提示）
    double m_lastStart = 0.0, m_lastEnd = 1.0;    // 最近一次绘制的坐标映射（供鼠标换算）
    double m_lastYmin = -1.0, m_lastYmax = 1.0;
};

} // namespace smd
