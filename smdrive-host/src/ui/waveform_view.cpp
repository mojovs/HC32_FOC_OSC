#include "ui/waveform_view.h"

#include "app/channel_table.h"
#include "data/datahub.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QScrollBar>
#include <QStringList>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace smd {

namespace {
constexpr int kLeftMargin = 64;
constexpr int kRightMargin = 12;
constexpr int kTopMargin = 10;
constexpr int kBottomMargin = 26;       // x 轴刻度区
constexpr int kScrollHeight = 14;       // 底部时间进度条
constexpr int kScrollGap = 4;
constexpr int kXDiv = 10;               // 横轴格数（刻度与网格共用，数值更好读）
constexpr int kYDiv = 8;                // 纵轴格数
const QColor kBg(0x1B, 0x1E, 0x22);
const QColor kPlotBg(0x11, 0x13, 0x16);
const QColor kGrid(0x2E, 0x34, 0x3B);
const QColor kAxis(0x8A, 0x93, 0x9E);
// 测量光标配色（X 竖线 / Y 横线各一对，颜色区分便于读数）
const QColor kCurX1(0xFF, 0xC1, 0x07);
const QColor kCurX2(0xFF, 0x52, 0x52);
const QColor kCurY1(0x4F, 0xC3, 0xF7);
const QColor kCurY2(0x69, 0xF0, 0xAE);
} // namespace

WaveformView::WaveformView(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(220);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setFocusPolicy(Qt::StrongFocus);

    // 底部进度条：在整个环形历史中拖动定位；一拖即自动暂停（停止跟随）
    m_scroll = new QScrollBar(Qt::Horizontal, this);
    m_scroll->setRange(0, 0);
    m_scroll->setTracking(true);
    connect(m_scroll, &QScrollBar::valueChanged, this, [this](int v) {
        if (m_syncingScroll || !m_hub)
            return;
        const quint64 oldest = m_hub->channel(0).absoluteIndex(0);
        const double win = m_windowSeconds * double(m_hub->sampleRate());
        m_viewEnd = double(oldest) + double(v) + win;   // 进度条值 = 视窗左端相对最旧样本
        setPaused(true);                                 // 定位历史即停止实时跟随
        update();
    });
}

QRectF WaveformView::plotRect() const
{
    const double w = width() - kLeftMargin - kRightMargin;
    const double h = height() - kTopMargin - kBottomMargin - kScrollHeight - kScrollGap;
    return QRectF(kLeftMargin, kTopMargin, std::max(1.0, w), std::max(1.0, h));
}

void WaveformView::setWindowSeconds(double s)
{
    m_windowSeconds = std::clamp(s, 0.005, 60.0);
    update();
}

void WaveformView::setChannelVisible(int channel, bool on)
{
    if (channel < 0 || channel >= m_channelCount)
        return;
    m_visible[channel] = on;
    update();
}

bool WaveformView::channelVisible(int channel) const
{
    return channel >= 0 && channel < m_channelCount && m_visible[channel];
}

void WaveformView::setOffset(int channel, double value)
{
    if (channel < 0 || channel >= m_channelCount)
        return;
    m_offsets[channel] = value;
    update();
}

double WaveformView::offset(int channel) const
{
    return (channel >= 0 && channel < m_channelCount) ? m_offsets[channel] : 0.0;
}

void WaveformView::setGain(int channel, double gain)
{
    if (channel < 0 || channel >= m_channelCount)
        return;
    m_gains[channel] = (gain == 0.0) ? 1.0 : gain;   // 0 会让通道彻底消失，按 1 处理
    update();
}

double WaveformView::gain(int channel) const
{
    return (channel >= 0 && channel < m_channelCount) ? m_gains[channel] : 1.0;
}

void WaveformView::setYRange(double lo, double hi)
{
    if (hi <= lo)
        hi = lo + 1.0;
    m_manualMin = lo;
    m_manualMax = hi;
    update();
}

void WaveformView::setPaused(bool on)
{
    if (on == m_paused) {
        update();
        return;
    }
    m_paused = on;
    // 恢复跟随时 m_viewEnd 会在下一次重绘锁到最新样本；暂停时它保持当前值。
    update();
    emit pausedChanged(m_paused);
}

void WaveformView::setCursorMeasure(bool on)
{
    if (on == m_measure) {
        update();
        return;
    }
    m_measure = on;
    m_activeHandle = -1;
    if (on) {
        m_cursorsInit = false;     // 打开时按当前视窗重新摆放两条 X / 两条 Y
        setPaused(true);           // 光标测量在冻结画面上做，避免光标随数据漂走
    }
    update();
}

void WaveformView::resetView()
{
    m_autoScale = true;
    for (double &o : m_offsets)
        o = 0.0;
    for (double &g : m_gains)
        g = 1.0;
    m_cursorsInit = false;
    setPaused(false);       // 回到实时跟随
    update();
}

void WaveformView::markDataAppended()
{
    if (m_repaintQueued)
        return;
    m_repaintQueued = true;
    // 排队重绘：多次数据到达只触发一次 paint（等价 QCustomPlot::rpQueuedReplot）
    QMetaObject::invokeMethod(this, [this] {
        m_repaintQueued = false;
        update();
    }, Qt::QueuedConnection);
}

void WaveformView::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    if (m_scroll) {
        const QRectF plot = plotRect();
        m_scroll->setGeometry(int(plot.left()), height() - kScrollHeight - 2,
                              int(plot.width()), kScrollHeight);
    }
}

void WaveformView::syncScrollBar(double startSample, double endSample)
{
    if (!m_scroll || !m_hub)
        return;

    const double rate = double(m_hub->sampleRate());
    const quint64 oldest = m_hub->channel(0).absoluteIndex(0);
    const quint64 total = m_hub->samplesPerChannel();
    const double win = m_windowSeconds * rate;
    const double avail = double(total) - double(oldest);

    const int hi = int(std::max(0.0, avail - win));                 // 可滚动到的最大左端
    const int val = int(std::clamp(startSample - double(oldest), 0.0, double(hi)));
    const int page = int(std::max(1.0, win));

    m_syncingScroll = true;
    m_scroll->setRange(0, hi);
    m_scroll->setPageStep(page);
    m_scroll->setValue(val);
    m_syncingScroll = false;
}

void WaveformView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), kBg);

    const QRectF plot = plotRect();
    p.fillRect(plot, kPlotBg);

    if (!m_hub) {
        p.setPen(kAxis);
        p.drawText(plot, Qt::AlignCenter, QStringLiteral("未绑定数据源"));
        return;
    }

    const quint64 total = m_hub->samplesPerChannel();
    const double rate = double(m_hub->sampleRate());
    const double winSamples = m_windowSeconds * rate;

    // 视窗右端：跟随态锁到最新样本；暂停态用冻结位置。再夹到有效历史范围，
    // 避免暂停久了旧样本被环形缓冲覆盖后画面跑空。
    double endSample = m_paused ? m_viewEnd : double(total);
    double startSample = endSample - winSamples;
    const double oldest = double(m_hub->channel(0).absoluteIndex(0));
    const double newest = double(total);
    if (startSample < oldest) { startSample = oldest; endSample = oldest + winSamples; }
    if (endSample > newest)   { endSample = newest;  startSample = newest - winSamples; }
    if (startSample < 0.0)    { startSample = 0.0;   endSample = winSamples; }
    m_viewEnd = endSample;

    // ---- 量程 ----
    double ymin, ymax;
    if (m_autoScale) {
        ymin = std::numeric_limits<double>::infinity();
        ymax = -std::numeric_limits<double>::infinity();
        const int cols = std::max(1, int(plot.width()));
        for (int c = 0; c < m_channelCount; ++c) {
            if (!m_visible[c])
                continue;
            const RingBuffer<float> &rb = m_hub->channel(c);
            for (int col = 0; col < cols; ++col) {
                const double s0 = startSample + (endSample - startSample) * col / cols;
                const double s1 = startSample + (endSample - startSample) * (col + 1) / cols;
                const quint64 i0 = quint64(std::max(0.0, std::floor(s0)));
                const quint64 i1 = quint64(std::max(0.0, std::ceil(s1)));
                const quint64 firstAbs = rb.absoluteIndex(0);
                for (quint64 idx = i0; idx < i1; ++idx) {
                    if (!rb.contains(idx))
                        continue;
                    // 量程必须用“显示值”（含增益与偏移），否则调增益后曲线会被压平
                    const double v = double(rb.at(size_t(idx - firstAbs))) * m_gains[c] - m_offsets[c];
                    ymin = std::min(ymin, v);
                    ymax = std::max(ymax, v);
                }
            }
            if (std::isinf(ymin) || std::isinf(ymax))
                continue;
        }
        if (std::isinf(ymin) || std::isinf(ymax) || ymax - ymin < 1e-12) {
            ymin -= 0.5;
            ymax += 0.5;
        } else {
            const double pad = (ymax - ymin) * 0.08;
            ymin -= pad;
            ymax += pad;
        }
    } else {
        ymin = m_manualMin;
        ymax = m_manualMax;
    }

    // 记下本帧的坐标映射，供鼠标换算光标位置（拖动时用）
    m_lastStart = startSample;
    m_lastEnd = endSample;
    m_lastYmin = ymin;
    m_lastYmax = ymax;

    drawGrid(p, plot);
    drawCurves(p, plot, startSample, endSample, ymin, ymax);

    // ---- 纵轴刻度 ----
    p.setPen(kAxis);
    QFont f = p.font();
    f.setPointSizeF(8.0);
    p.setFont(f);
    for (int i = 0; i <= kYDiv; ++i) {
        const double v = ymin + (ymax - ymin) * i / double(kYDiv);
        const double y = plot.bottom() - plot.height() * i / double(kYDiv);
        p.drawText(QRectF(0, y - 9, kLeftMargin - 6, 18), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(v, 'g', 4));
    }

    // ---- 横轴刻度（自数据开始采集起算的秒数，随视窗/拖动移动）----
    const double tStart = startSample / rate;
    const double tEnd   = endSample   / rate;
    for (int i = 0; i <= kXDiv; ++i) {
        const double t = tStart + (tEnd - tStart) * i / double(kXDiv);
        const double x = plot.left() + plot.width() * i / double(kXDiv);
        p.drawText(QRectF(x - 38, plot.bottom() + 4, 76, 18), Qt::AlignCenter,
                   QString::number(t, 'f', 3) + QStringLiteral(" s"));
    }

    drawLegend(p, plot);
    if (!m_measure && m_cursorX >= 0)
        drawCursor(p, plot, startSample, endSample);

    if (m_measure) {
        // 首次打开（或复位后）：等有数据、量程有效时再按当前视窗摆好，避免 inf 量程算出 nan
        if (!m_cursorsInit && total > 0 && std::isfinite(ymin) && std::isfinite(ymax)) {
            const double span = endSample - startSample;
            m_mx1 = startSample + span * 0.25;
            m_mx2 = startSample + span * 0.75;
            m_my1 = ymin + (ymax - ymin) * 0.35;
            m_my2 = ymin + (ymax - ymin) * 0.65;
            m_cursorsInit = true;
        }
        if (m_cursorsInit)
            drawMeasureCursors(p, plot);
    }

    if (m_paused && !m_measure) {
        p.setPen(QColor(0xFF, 0xC1, 0x07));
        p.drawText(plot.adjusted(8, 8, -8, -8), Qt::AlignRight | Qt::AlignTop,
                   QStringLiteral("已暂停 (停止跟随最新，可拖动进度条/波形浏览历史)"));
    }
    if (total == 0) {
        p.setPen(kAxis);
        p.drawText(plot, Qt::AlignCenter,
                   QStringLiteral("等待数据…（先打开串口 → 认证 → 开始上传）"));
    }

    syncScrollBar(startSample, endSample);
}

void WaveformView::drawGrid(QPainter &p, const QRectF &plot) const
{
    QPen pen(kGrid);
    pen.setWidth(1);
    p.setPen(pen);
    for (int i = 0; i <= kXDiv; ++i) {
        const double x = plot.left() + plot.width() * i / double(kXDiv);
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    }
    for (int i = 0; i <= kYDiv; ++i) {
        const double y = plot.top() + plot.height() * i / double(kYDiv);
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }
    p.setPen(QPen(kAxis, 1));
    p.drawRect(plot);
}

void WaveformView::drawCurves(QPainter &p, const QRectF &plot, double startSample,
                              double endSample, double ymin, double ymax) const
{
    const int cols = std::max(1, int(plot.width()));
    const double span = std::max(1.0, endSample - startSample);
    const double vs = (ymax - ymin);
    const auto &meta = channelTable();

    p.setRenderHint(QPainter::Antialiasing, false);
    for (int c = 0; c < m_channelCount; ++c) {
        if (!m_visible[c])
            continue;
        const RingBuffer<float> &rb = m_hub->channel(c);
        if (rb.size() == 0)
            continue;

        QPainterPath path;
        bool started = false;
        for (int col = 0; col < cols; ++col) {
            const double s0 = startSample + span * col / cols;
            const double s1 = startSample + span * (col + 1) / cols;
            const quint64 i0 = quint64(std::max(0.0, std::floor(s0)));
            const quint64 i1 = quint64(std::max(0.0, std::ceil(s1)));
            bool any = false;
            double lo = 0.0, hi = 0.0;
            const quint64 firstAbs = rb.absoluteIndex(0);
            for (quint64 idx = i0; idx < i1; ++idx) {
                if (!rb.contains(idx))
                    continue;
                const double v = double(rb.at(size_t(idx - firstAbs))) * m_gains[c] - m_offsets[c];
                if (!any) {
                    lo = hi = v;
                    any = true;
                } else {
                    lo = std::min(lo, v);
                    hi = std::max(hi, v);
                }
            }
            if (!any)
                continue;
            const double x = plot.left() + plot.width() * col / cols;
            const double yHi = plot.bottom() - (hi - ymin) / vs * plot.height();
            const double yLo = plot.bottom() - (lo - ymin) / vs * plot.height();
            const QPointF a(x, yHi), b(x, yLo);
            if (!started) {
                path.moveTo(a);
                started = true;
            } else {
                path.lineTo(a);
            }
            if (yLo != yHi)
                path.lineTo(b);
        }
        QPen pen(meta[size_t(c)].color);
        pen.setWidthF(1.2);
        p.setPen(pen);
        p.drawPath(path);
    }
}

void WaveformView::drawLegend(QPainter &p, const QRectF &plot) const
{
    const auto &meta = channelTable();
    QFont f = p.font();
    f.setPointSizeF(8.0);
    p.setFont(f);
    double x = plot.left() + 8;
    const double y = plot.top() + 14;
    for (int c = 0; c < m_channelCount; ++c) {
        if (!m_visible[c])
            continue;
        QString label = meta[size_t(c)].name;
        if (!qFuzzyCompare(m_gains[c], 1.0)) {                 // 增益非 1 时在图例里标出来
            label += QStringLiteral(" ×%1").arg(m_gains[c], 0, 'g', 4);
        }
        const double w = p.fontMetrics().horizontalAdvance(label) + 24;
        if (x + w > plot.right() - 8) {
            x = plot.left() + 8;
        }
        p.setPen(QPen(meta[size_t(c)].color, 2));
        p.drawLine(QPointF(x, y - 4), QPointF(x + 14, y - 4));
        p.setPen(QColor(0xD5, 0xDA, 0xE0));
        p.drawText(QPointF(x + 18, y), label);
        x += w;
    }
}

void WaveformView::drawCursor(QPainter &p, const QRectF &plot, double startSample,
                             double endSample) const
{
    if (m_cursorX < plot.left() || m_cursorX > plot.right())
        return;
    p.setPen(QPen(QColor(0xFF, 0xC1, 0x07), 1, Qt::DashLine));
    p.drawLine(QPointF(m_cursorX, plot.top()), QPointF(m_cursorX, plot.bottom()));
    const double frac = (m_cursorX - plot.left()) / std::max(1.0, plot.width());
    double tAbs = 0.0;
    if (m_hub) {
        const double rate = double(m_hub->sampleRate());
        tAbs = (startSample + (endSample - startSample) * frac) / rate;   // 自采集起点的秒数
    }
    p.setPen(QColor(0xFF, 0xC1, 0x07));
    p.drawText(QRectF(m_cursorX - 70, plot.top() + 2, 140, 16), Qt::AlignCenter,
               QStringLiteral("t %1 s").arg(tAbs, 0, 'f', 4));
}

void WaveformView::drawMeasureCursors(QPainter &p, const QRectF &plot) const
{
    const double rate = (m_hub && m_hub->sampleRate() > 0) ? double(m_hub->sampleRate()) : 1.0;
    const double span = std::max(1e-9, m_lastEnd - m_lastStart);
    const double yRange = std::max(1e-12, m_lastYmax - m_lastYmin);

    auto xOf = [&](double sample) {
        return plot.left() + (sample - m_lastStart) / span * plot.width();
    };
    auto yOf = [&](double value) {
        return plot.bottom() - (value - m_lastYmin) / yRange * plot.height();
    };

    QFont f = p.font();
    f.setPointSizeF(8.0);
    p.setFont(f);

    const double x1 = xOf(m_mx1), x2 = xOf(m_mx2);
    const double y1 = yOf(m_my1), y2 = yOf(m_my2);
    const double xs[2] = {x1, x2};
    const double ys[2] = {y1, y2};
    const QColor xc[2] = {kCurX1, kCurX2};
    const QColor yc[2] = {kCurY1, kCurY2};

    // ---- 竖线 X1 / X2 ----
    for (int i = 0; i < 2; ++i) {
        if (xs[i] < plot.left() - 0.5 || xs[i] > plot.right() + 0.5)
            continue;
        QPen pen(xc[i]);
        pen.setWidth(i == 0 ? (m_activeHandle == 0 ? 2 : 1) : (m_activeHandle == 1 ? 2 : 1));
        pen.setStyle(Qt::DashLine);
        p.setPen(pen);
        p.drawLine(QPointF(xs[i], plot.top()), QPointF(xs[i], plot.bottom()));
        p.setBrush(xc[i]);
        p.setPen(Qt::NoPen);
        p.drawRect(QRectF(xs[i] - 4, plot.top(), 8, 5));        // 顶部抓手
    }

    // ---- 横线 Y1 / Y2 ----
    for (int i = 0; i < 2; ++i) {
        if (ys[i] < plot.top() - 0.5 || ys[i] > plot.bottom() + 0.5)
            continue;
        QPen pen(yc[i]);
        pen.setWidth(i == 0 ? (m_activeHandle == 2 ? 2 : 1) : (m_activeHandle == 3 ? 2 : 1));
        pen.setStyle(Qt::DashLine);
        p.setPen(pen);
        p.drawLine(QPointF(plot.left(), ys[i]), QPointF(plot.right(), ys[i]));
        p.setBrush(yc[i]);
        p.setPen(Qt::NoPen);
        p.drawRect(QRectF(plot.left(), ys[i] - 4, 5, 8));       // 左侧抓手
    }

    // ---- 读数框（右上角）：两条 X 的时间、两条 Y 的数值，以及各自差值 ----
    const double dt = (m_mx2 - m_mx1) / rate;                   // 秒
    const double dy = m_my2 - m_my1;
    QStringList lines;
    lines << QStringLiteral("X1 %1 s").arg(m_mx1 / rate, 0, 'f', 4)
          << QStringLiteral("X2 %1 s").arg(m_mx2 / rate, 0, 'f', 4)
          << QStringLiteral("ΔX %1 s").arg(dt, 0, 'f', 4);
    if (std::abs(dt) > 1e-9)
        lines << QStringLiteral("1/ΔX %1 Hz").arg(1.0 / std::abs(dt), 0, 'f', 1);
    lines << QStringLiteral("Y1 %1").arg(m_my1, 0, 'g', 6)
          << QStringLiteral("Y2 %1").arg(m_my2, 0, 'g', 6)
          << QStringLiteral("ΔY %1").arg(dy, 0, 'g', 6);

    const QFontMetrics fm(p.font());
    int w = 0;
    for (const QString &s : lines)
        w = std::max(w, fm.horizontalAdvance(s));
    w += 18;
    const int lh = fm.height();
    const QRectF box(plot.right() - w - 8, plot.top() + 8, w, lh * lines.size() + 10);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 180));
    p.drawRoundedRect(box, 4, 4);
    p.setPen(QColor(0xE6, 0xEA, 0xEF));
    for (int i = 0; i < lines.size(); ++i)
        p.drawText(QRectF(box.left() + 9, box.top() + 5 + lh * i, w - 18, lh),
                   Qt::AlignLeft | Qt::AlignVCenter, lines[i]);
}

int WaveformView::nearestHandle(const QPointF &pos) const
{
    const QRectF plot = plotRect();
    const double span = std::max(1e-9, m_lastEnd - m_lastStart);
    const double yRange = std::max(1e-12, m_lastYmax - m_lastYmin);
    const double x1 = plot.left() + (m_mx1 - m_lastStart) / span * plot.width();
    const double x2 = plot.left() + (m_mx2 - m_lastStart) / span * plot.width();
    const double y1 = plot.bottom() - (m_my1 - m_lastYmin) / yRange * plot.height();
    const double y2 = plot.bottom() - (m_my2 - m_lastYmin) / yRange * plot.height();

    const double kGrab = 14.0;                                  // 抓取半径（像素）
    int best = -1;
    double bestD = kGrab;
    if (pos.y() >= plot.top() && pos.y() <= plot.bottom()) {
        if (std::abs(pos.x() - x1) < bestD) { bestD = std::abs(pos.x() - x1); best = 0; }
        if (std::abs(pos.x() - x2) < bestD) { bestD = std::abs(pos.x() - x2); best = 1; }
    }
    if (pos.x() >= plot.left() && pos.x() <= plot.right()) {
        if (std::abs(pos.y() - y1) < bestD) { bestD = std::abs(pos.y() - y1); best = 2; }
        if (std::abs(pos.y() - y2) < bestD) { bestD = std::abs(pos.y() - y2); best = 3; }
    }
    return best;
}

void WaveformView::dragHandleTo(const QPointF &pos)
{
    const QRectF plot = plotRect();
    const double span = std::max(1e-9, m_lastEnd - m_lastStart);
    const double yRange = std::max(1e-12, m_lastYmax - m_lastYmin);
    const double cx = std::clamp(pos.x(), plot.left(), plot.right());
    const double cy = std::clamp(pos.y(), plot.top(), plot.bottom());
    const double sample = m_lastStart + (cx - plot.left()) / std::max(1.0, plot.width()) * span;
    const double value = m_lastYmin + (plot.bottom() - cy) / std::max(1.0, plot.height()) * yRange;

    switch (m_activeHandle) {
    case 0: m_mx1 = sample; break;
    case 1: m_mx2 = sample; break;
    case 2: m_my1 = value;  break;
    case 3: m_my2 = value;  break;
    default: break;
    }
}

void WaveformView::wheelEvent(QWheelEvent *e)
{
    const double steps = e->angleDelta().y() / 120.0;
    if (steps == 0.0)
        return;
    const double factor = std::pow(0.8, steps);
    setWindowSeconds(m_windowSeconds * factor);
    e->accept();
}

void WaveformView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    if (m_measure) {                            // 测量模式：左键拖动光标，不平移波形
        m_activeHandle = nearestHandle(QPointF(e->pos()));
        if (m_activeHandle >= 0) {
            setCursor(Qt::SizeAllCursor);
            dragHandleTo(QPointF(e->pos()));
            update();
        }
        return;
    }
    if (!m_paused && m_hub)             // 未暂停时从当前位置开始拖（等价锁定右端）
        m_viewEnd = double(m_hub->samplesPerChannel());
    m_dragging = true;
    m_dragAnchor = e->pos();
    m_dragStartViewEnd = m_viewEnd;
    setCursor(Qt::ClosedHandCursor);
}

void WaveformView::mouseMoveEvent(QMouseEvent *e)
{
    if (m_measure) {                            // 测量模式：只移动被抓住的光标
        if (m_activeHandle >= 0) {
            dragHandleTo(QPointF(e->pos()));
            update();
        }
        return;
    }
    m_cursorX = e->pos().x();
    if (m_dragging && m_hub) {
        const QRectF plot = plotRect();
        const double rate = double(m_hub->sampleRate());
        const double samplesPerPixel = (m_windowSeconds * rate) / std::max(1.0, plot.width());
        const double dx = e->pos().x() - m_dragAnchor.x();
        m_viewEnd = m_dragStartViewEnd - dx * samplesPerPixel;   // 向右拖 = 看更早
        setPaused(true);                                         // 一旦翻历史就停止跟随
    }
    update();
}

void WaveformView::mouseReleaseEvent(QMouseEvent *)
{
    m_dragging = false;
    m_activeHandle = -1;
    unsetCursor();
}

void WaveformView::mouseDoubleClickEvent(QMouseEvent *)
{
    resetView();
}

void WaveformView::leaveEvent(QEvent *)
{
    m_cursorX = -1;
    update();
}

} // namespace smd
