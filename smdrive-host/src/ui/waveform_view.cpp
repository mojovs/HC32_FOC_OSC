#include "ui/waveform_view.h"

#include "app/channel_table.h"
#include "data/datahub.h"

#include <QFontDatabase>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

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
// 测量光标配色：刻意避开波形通道用的红/黄/橙/蓝/紫/绿等色
const QColor kCurX1(0xFF, 0x2D, 0x95);   // X1 品红
const QColor kCurX2(0x00, 0xE5, 0xFF);   // X2 亮青
const QColor kCurY1(0xC7, 0x7D, 0xFF);   // Y1 紫罗兰
const QColor kCurY2(0xB2, 0xFF, 0x59);   // Y2 亮柠檬绿
const QColor kCurDx(0xFF, 0xD5, 0x4F);   // Δ 亮金
const QColor kReadHead(0x9A, 0xA0, 0xA6); // 读数框表头灰

/// 深色（如深蓝）在深色底上几乎看不见，用于文字时提亮一档
QColor readableText(const QColor &c)
{
    return (c.value() < 130) ? c.lighter(175) : c;
}
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
    m_hoverHandle = -1;
    m_dragReadout = false;
    m_readoutHover = false;
    if (on) {
        m_cursorsInit = false;     // 打开时按当前视窗重新摆放两条 X 光标
        setPaused(true);           // 光标测量在冻结画面上做，避免光标随数据漂走
    }
    update();
}

void WaveformView::setChannelMeasured(int channel, bool on)
{
    if (channel < 0 || channel >= m_channelCount)
        return;
    m_measured[channel] = on;
    update();
}

bool WaveformView::channelMeasured(int channel) const
{
    return channel >= 0 && channel < m_channelCount && m_measured[channel];
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
        // 首次打开（或复位后）：等有数据、量程有效时再按当前视窗摆好，避免 inf 量程出问题
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
                   QStringLiteral("等待数据…（先打开串口 → 认证 → 显示波形）"));
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

    const double x1 = xOf(m_mx1), x2 = xOf(m_mx2);
    const double y1 = yOf(m_my1), y2 = yOf(m_my2);

    // ---- 光标线：带深色描边(halo) + 端部标签，压在任意波形上都找得到 ----
    QFont cf = p.font();
    cf.setPointSizeF(8.0);
    cf.setBold(true);
    p.setFont(cf);
    const QFontMetrics cfm(cf);
    auto drawTag = [&](const QString &text, const QRectF &r0, const QColor &c) {
        QRectF r = r0;
        if (r.left() < plot.left())
            r.moveLeft(plot.left());
        if (r.right() > plot.right())
            r.moveRight(plot.right());
        if (r.top() < plot.top())
            r.moveTop(plot.top());
        if (r.bottom() > plot.bottom())
            r.moveBottom(plot.bottom());
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x00, 0x00, 0x00, 235));       // 描边
        p.drawRect(r.adjusted(-1.5, -1.5, 1.5, 1.5));
        p.setBrush(c);
        p.drawRect(r);
        p.setPen(QColor(0x10, 0x13, 0x17));              // 深色字，实底上清晰
        p.drawText(r, Qt::AlignCenter, text);
    };

    // ---- 竖线 X1 / X2 ----
    {
        const double xs[2] = {x1, x2};
        const QString xn[2] = {QStringLiteral("X1"), QStringLiteral("X2")};
        const QColor xc[2] = {kCurX1, kCurX2};
        for (int i = 0; i < 2; ++i) {
            if (xs[i] < plot.left() - 0.5 || xs[i] > plot.right() + 0.5)
                continue;
            const bool act = (m_activeHandle == i || m_hoverHandle == i);
            p.setPen(QPen(QColor(0, 0, 0, 215), act ? 5.0 : 3.5));   // halo
            p.drawLine(QPointF(xs[i], plot.top()), QPointF(xs[i], plot.bottom()));
            QPen pen(xc[i]);
            pen.setWidthF(act ? 2.2 : 1.5);
            pen.setStyle(Qt::DashLine);
            p.setPen(pen);
            p.drawLine(QPointF(xs[i], plot.top()), QPointF(xs[i], plot.bottom()));
            const int tw = cfm.horizontalAdvance(xn[i]) + 10;
            const int th = cfm.height() + 2;
            drawTag(xn[i], QRectF(xs[i] - tw / 2.0, plot.top() + 2, tw, th), xc[i]);
        }
    }
    // ---- 横线 Y1 / Y2 ----
    {
        const double ys[2] = {y1, y2};
        const QString yn[2] = {QStringLiteral("Y1"), QStringLiteral("Y2")};
        const QColor yc[2] = {kCurY1, kCurY2};
        for (int i = 0; i < 2; ++i) {
            if (ys[i] < plot.top() - 0.5 || ys[i] > plot.bottom() + 0.5)
                continue;
            const bool act = (m_activeHandle == 2 + i || m_hoverHandle == 2 + i);
            p.setPen(QPen(QColor(0, 0, 0, 215), act ? 5.0 : 3.5));   // halo
            p.drawLine(QPointF(plot.left(), ys[i]), QPointF(plot.right(), ys[i]));
            QPen pen(yc[i]);
            pen.setWidthF(act ? 2.2 : 1.5);
            pen.setStyle(Qt::DashLine);
            p.setPen(pen);
            p.drawLine(QPointF(plot.left(), ys[i]), QPointF(plot.right(), ys[i]));
            // 标签贴着左端（上/下都随线走）
            const int tw = cfm.horizontalAdvance(yn[i]) + 10;
            const int th = cfm.height() + 2;
            drawTag(yn[i], QRectF(plot.left() + 3, ys[i] - th / 2.0, tw, th), yc[i]);
        }
    }

    // ---- 各「测量通道」在 X1、X2 处的波形点（坐标点），两点连线即 ΔV ----
    const auto &meta = channelTable();
    auto fmtN = [](double v) -> QString {
        return std::isnan(v) ? QStringLiteral("--") : QString::number(v, 'g', 5);
    };
    const double nan = std::numeric_limits<double>::quiet_NaN();

    struct Cell { QString text; QColor color; };
    struct Row { Cell c[4]; };
    std::vector<Row> rows;

    const double dt = (m_mx2 - m_mx1) / rate;       // 秒
    rows.push_back(Row{{Cell{QStringLiteral("项"), kReadHead},
                        Cell{QStringLiteral("光标 1"), kReadHead},
                        Cell{QStringLiteral("光标 2"), kReadHead},
                        Cell{QStringLiteral("差值"), kReadHead}}});
    {
        QString d = QString::number(dt, 'f', 4) + QStringLiteral("s");
        if (std::abs(dt) > 1e-9)
            d += QStringLiteral(" (%1Hz)").arg(1.0 / std::abs(dt), 0, 'f', 1);
        rows.push_back(Row{{Cell{QStringLiteral("时间"), kReadHead},
                            Cell{QString::number(m_mx1 / rate, 'f', 4) + QStringLiteral("s"), kCurX1},
                            Cell{QString::number(m_mx2 / rate, 'f', 4) + QStringLiteral("s"), kCurX2},
                            Cell{d, kCurDx}}});
    }
    rows.push_back(Row{{Cell{QStringLiteral("Y 值"), kReadHead},
                        Cell{fmtN(m_my1), kCurY1},
                        Cell{fmtN(m_my2), kCurY2},
                        Cell{fmtN(m_my2 - m_my1), kCurDx}}});

    bool anyChan = false;
    for (int c = 0; c < m_channelCount; ++c)
        anyChan = anyChan || m_measured[c];
    if (anyChan) {
        rows.push_back(Row{{Cell{QStringLiteral("通道"), kReadHead},
                            Cell{QStringLiteral("@X1"), kReadHead},
                            Cell{QStringLiteral("@X2"), kReadHead},
                            Cell{QStringLiteral("ΔV"), kReadHead}}});
    }
    for (int c = 0; c < m_channelCount; ++c) {
        if (!m_measured[c])
            continue;
        const double v1 = sampleValueAt(c, m_mx1);
        const double v2 = sampleValueAt(c, m_mx2);
        const QColor col = readableText(meta[size_t(c)].color);
        rows.push_back(Row{{Cell{meta[size_t(c)].name.section(QLatin1Char(' '), 0, 0), col},
                            Cell{fmtN(v1), col},
                            Cell{fmtN(v2), col},
                            Cell{(std::isnan(v1) || std::isnan(v2)) ? fmtN(nan) : fmtN(v2 - v1), col}}});

        // 在曲线上标出这两个坐标点，并用细虚线相连（纵向跨度就是 ΔV）
        if (!std::isnan(v1) && !std::isnan(v2)) {
            const QPointF p1(x1, yOf(v1));
            const QPointF p2(x2, yOf(v2));
            QPen pen(col);
            pen.setWidthF(1.0);
            pen.setStyle(Qt::DotLine);
            p.setPen(pen);
            p.setBrush(col);
            p.drawLine(p1, p2);
            p.drawEllipse(p1, 2.5, 2.5);
            p.drawEllipse(p2, 2.5, 2.5);
        }
    }

    // ---- 读数表（右上角，PSpice 光标读数窗样式，等宽字体对齐；数值按来源配色）----
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSizeF(8.0);
    p.setFont(mono);
    const QFontMetrics fm(mono);
    const int padX = 8, padY = 3;
    int colW[4] = {0, 0, 0, 0};
    for (const Row &r : rows)
        for (int i = 0; i < 4; ++i)
            colW[i] = std::max(colW[i], fm.horizontalAdvance(r.c[i].text));
    for (int i = 0; i < 4; ++i)
        colW[i] += 2 * padX;
    const int rowH = fm.height() + 2 * padY;
    const int titleH = fm.height() + 8;
    const int tableW = colW[0] + colW[1] + colW[2] + colW[3];
    const int tableH = titleH + rowH * int(rows.size());
    QRectF win;
    if (m_readoutPos.x() >= 0.0) {                       // 用户拖过：用记住的位置，并夹在绘图区内
        win = QRectF(m_readoutPos, QSizeF(tableW, tableH));
        const double maxX = std::max(plot.left(), plot.right() - tableW);
        const double maxY = std::max(plot.top(), plot.bottom() - tableH);
        win.moveLeft(std::clamp(win.left(), plot.left(), maxX));
        win.moveTop(std::clamp(win.top(), plot.top(), maxY));
    } else {                                             // 默认停在右上角
        win = QRectF(plot.right() - tableW - 8, plot.top() + 8, tableW, tableH);
    }
    m_readoutRect = win;                                 // 记录，供鼠标命中测试

    // 背景 + 标题栏 + 外框（悬停时标题栏提亮，提示可拖）
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x10, 0x13, 0x17, 238));
    p.drawRect(win);
    p.setBrush(m_readoutHover ? QColor(0x2E, 0x35, 0x3F) : QColor(0x22, 0x27, 0x2E));
    p.drawRect(QRectF(win.left(), win.top(), win.width(), titleH));
    p.setPen(QColor(0x3C, 0x43, 0x4C));
    p.setBrush(Qt::NoBrush);
    p.drawRect(win);
    p.setPen(QColor(0xD5, 0xDA, 0xE0));
    p.drawText(QRectF(win.left() + 8, win.top(), win.width() - 30, titleH),
               Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("测量（光标）"));
    // 标题栏右侧三点抓取纹，提示整表可拖动
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x76, 0x7E, 0x88));
    for (int i = 0; i < 3; ++i)
        p.drawEllipse(QPointF(win.right() - 10 - i * 6, win.top() + titleH / 2.0), 1.6, 1.6);

    // 网格线
    const double gtop = win.top() + titleH;
    p.setPen(QColor(0x30, 0x36, 0x3E));
    for (size_t i = 0; i <= rows.size(); ++i) {
        const double y = gtop + rowH * double(i);
        p.drawLine(QPointF(win.left(), y), QPointF(win.right(), y));
    }
    double xcol = win.left();
    for (int i = 0; i < 4; ++i) {
        p.drawLine(QPointF(xcol, gtop), QPointF(xcol, win.bottom()));
        xcol += colW[i];
    }
    p.drawLine(QPointF(win.right() - 0.5, gtop), QPointF(win.right() - 0.5, win.bottom()));

    // 单元格文字
    for (size_t r = 0; r < rows.size(); ++r) {
        double x = win.left();
        for (int i = 0; i < 4; ++i) {
            p.setPen(rows[r].c[i].color);
            p.drawText(QRectF(x + padX, gtop + rowH * double(r) + padY, colW[i] - 2 * padX, fm.height()),
                       Qt::AlignLeft | Qt::AlignVCenter, rows[r].c[i].text);
            x += colW[i];
        }
    }
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

    int best = -1;
    double bestD = 14.0;                                 // 抓取半径（像素）
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

double WaveformView::sampleValueAt(int channel, double sampleAbs) const
{
    if (!m_hub || channel < 0 || channel >= m_channelCount)
        return std::numeric_limits<double>::quiet_NaN();
    const RingBuffer<float> &rb = m_hub->channel(channel);
    if (rb.size() == 0)
        return std::numeric_limits<double>::quiet_NaN();
    const quint64 idx = quint64(std::max(0.0, std::floor(sampleAbs + 0.5)));
    if (!rb.contains(idx))
        return std::numeric_limits<double>::quiet_NaN();
    return double(rb.at(size_t(idx - rb.absoluteIndex(0)))) * m_gains[channel] - m_offsets[channel];
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
    if (m_measure) {                            // 测量模式：拖动读数表或光标，不平移波形
        const QPointF pos(e->pos());
        if (m_readoutRect.contains(pos)) {      // 点在读数表上：按住拖动整张表
            m_dragReadout = true;
            m_readoutGrab = pos - m_readoutRect.topLeft();
            setCursor(Qt::SizeAllCursor);
            return;
        }
        m_activeHandle = nearestHandle(pos);
        if (m_activeHandle >= 0) {
            setCursor(m_activeHandle <= 1 ? Qt::SizeHorCursor : Qt::SizeVerCursor);
            m_hoverHandle = m_activeHandle;
            dragHandleTo(pos);
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
    if (m_measure) {                            // 测量模式：拖动读数表 / 移动光标
        const QPointF pos(e->pos());
        if (m_dragReadout) {                    // 拖动读数表（位置在 paint 里夹进绘图区）
            m_readoutPos = pos - m_readoutGrab;
            update();
            return;
        }
        if (m_activeHandle >= 0) {
            dragHandleTo(pos);
            update();
            return;
        }
        // 悬停探测：读数表优先，其次光标线
        const bool overTable = m_readoutRect.contains(pos);
        if (overTable != m_readoutHover) {
            m_readoutHover = overTable;
            update();
        }
        if (overTable) {
            if (m_hoverHandle != -1) {
                m_hoverHandle = -1;
                update();
            }
            setCursor(Qt::SizeAllCursor);       // 整表可拖
            return;
        }
        const int h = nearestHandle(pos);
        if (h != m_hoverHandle) {
            m_hoverHandle = h;
            update();
        }
        if (h == 0 || h == 1)
            setCursor(Qt::SizeHorCursor);       // X 竖线：左右移动
        else if (h == 2 || h == 3)
            setCursor(Qt::SizeVerCursor);       // Y 横线：上下移动
        else
            unsetCursor();
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

void WaveformView::mouseReleaseEvent(QMouseEvent *e)
{
    m_dragging = false;
    m_activeHandle = -1;
    m_dragReadout = false;
    if (m_measure) {
        const QPointF pos(e->pos());
        m_readoutHover = m_readoutRect.contains(pos);
        m_hoverHandle = m_readoutHover ? -1 : nearestHandle(pos);
        if (m_readoutHover)
            setCursor(Qt::SizeAllCursor);
        else if (m_hoverHandle == 0 || m_hoverHandle == 1)
            setCursor(Qt::SizeHorCursor);
        else if (m_hoverHandle == 2 || m_hoverHandle == 3)
            setCursor(Qt::SizeVerCursor);
        else
            unsetCursor();
    } else {
        unsetCursor();
    }
    update();
}

void WaveformView::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (m_measure && m_readoutRect.contains(QPointF(e->pos()))) {
        m_readoutPos = QPointF(-1.0, -1.0);     // 读数表复位到右上角
        update();
        return;
    }
    resetView();
}

void WaveformView::leaveEvent(QEvent *)
{
    m_cursorX = -1;
    m_hoverHandle = -1;
    m_readoutHover = false;
    unsetCursor();
    update();
}

} // namespace smd
