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
};

} // namespace smd
