#include "ui/waveform_view.h"

#include "app/channel_table.h"
#include "data/datahub.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace smd {

namespace {
constexpr int kLeftMargin = 64;
constexpr int kRightMargin = 12;
constexpr int kTopMargin = 10;
constexpr int kBottomMargin = 26;
const QColor kBg(0x1B, 0x1E, 0x22);
const QColor kPlotBg(0x11, 0x13, 0x16);
const QColor kGrid(0x2E, 0x34, 0x3B);
const QColor kAxis(0x8A, 0x93, 0x9E);
} // namespace

WaveformView::WaveformView(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(220);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setFocusPolicy(Qt::StrongFocus);
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

void WaveformView::setYRange(double lo, double hi)
{
    if (hi <= lo)
        hi = lo + 1.0;
    m_manualMin = lo;
    m_manualMax = hi;
    update();
}

void WaveformView::resetView()
{
    m_panSamples = 0.0;
    m_autoScale = true;
    m_paused = false;
    for (double &o : m_offsets)
        o = 0.0;
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

void WaveformView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), kBg);

    const QRectF plot(kLeftMargin, kTopMargin,
                      width() - kLeftMargin - kRightMargin,
                      height() - kTopMargin - kBottomMargin);
    p.fillRect(plot, kPlotBg);

    if (!m_hub) {
        p.setPen(kAxis);
        p.drawText(plot, Qt::AlignCenter, QStringLiteral("未绑定数据源"));
        return;
    }

    const quint64 total = m_hub->samplesPerChannel();
    const double rate = double(m_hub->sampleRate());
    const double winSamples = m_windowSeconds * rate;

    double endSample = double(total) - m_panSamples;
    double startSample = endSample - winSamples;
    if (startSample < 0) {
        startSample = 0;
        endSample = std::max(winSamples, 0.0);
    }

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
                for (quint64 idx = i0; idx < i1; ++idx) {
                    if (!rb.contains(idx))
                        continue;
                    const double v = double(rb.at(size_t(idx - rb.absoluteIndex(0))));
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

    drawGrid(p, plot);
    drawCurves(p, plot, startSample, endSample, ymin, ymax);

    // ---- 纵轴刻度 ----
    p.setPen(kAxis);
    QFont f = p.font();
    f.setPointSizeF(8.0);
    p.setFont(f);
    for (int i = 0; i <= 4; ++i) {
        const double v = ymin + (ymax - ymin) * i / 4.0;
        const double y = plot.bottom() - plot.height() * i / 4.0;
        p.drawText(QRectF(0, y - 9, kLeftMargin - 6, 18), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(v, 'g', 4));
    }

    // ---- 横轴刻度（相对最新点的秒数）----
    for (int i = 0; i <= 4; ++i) {
        const double t = -m_windowSeconds + m_windowSeconds * i / 4.0;
        const double x = plot.left() + plot.width() * i / 4.0;
        p.drawText(QRectF(x - 40, plot.bottom() + 4, 80, 18), Qt::AlignCenter,
                   QString::number(t, 'f', 3) + QStringLiteral(" s"));
    }

    drawLegend(p, plot);
    if (m_cursorX >= 0)
        drawCursor(p, plot, startSample, endSample);

    if (m_paused) {
        p.setPen(QColor(0xFF, 0xC1, 0x07));
        p.drawText(plot.adjusted(8, 8, -8, -8), Qt::AlignRight | Qt::AlignTop,
                   QStringLiteral("已暂停/历史浏览中（双击复位）"));
    }
    if (total == 0) {
        p.setPen(kAxis);
        p.drawText(plot, Qt::AlignCenter,
                   QStringLiteral("等待数据…（先打开串口 → 认证 → 开始上传）"));
    }
}

void WaveformView::drawGrid(QPainter &p, const QRectF &plot) const
{
    QPen pen(kGrid);
    pen.setWidth(1);
    p.setPen(pen);
    for (int i = 0; i <= 8; ++i) {
        const double x = plot.left() + plot.width() * i / 8.0;
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    }
    for (int i = 0; i <= 4; ++i) {
        const double y = plot.top() + plot.height() * i / 4.0;
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
                const double v = double(rb.at(size_t(idx - firstAbs))) - m_offsets[c];
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
        const QString label = meta[size_t(c)].name;
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
    const double tRel = -m_windowSeconds + m_windowSeconds * frac;
    p.setPen(QColor(0xFF, 0xC1, 0x07));
    p.drawText(QRectF(m_cursorX - 60, plot.top() + 2, 120, 16), Qt::AlignCenter,
               QStringLiteral("Δ %1 s").arg(tRel, 0, 'f', 4));
    Q_UNUSED(startSample)
    Q_UNUSED(endSample)
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
    if (e->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragAnchor = e->pos();
        m_dragStartPan = m_panSamples;
        setCursor(Qt::ClosedHandCursor);
    }
}

void WaveformView::mouseMoveEvent(QMouseEvent *e)
{
    m_cursorX = e->pos().x();
    if (m_dragging && m_hub) {
        const double rate = double(m_hub->sampleRate());
        const double samplesPerPixel = (m_windowSeconds * rate) / std::max(1, width() - kLeftMargin - kRightMargin);
        const double dx = e->pos().x() - m_dragAnchor.x();
        m_panSamples = std::max(0.0, m_dragStartPan + dx * samplesPerPixel);
        if (m_panSamples > 0.0)
            m_paused = true;             // 一旦翻历史就停止实时跟随
    }
    update();
}

void WaveformView::mouseReleaseEvent(QMouseEvent *)
{
    m_dragging = false;
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
