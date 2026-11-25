#include "screenshotcapture.h"
#include "desktopactions.h"
#include "floatingwindow.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QCursor>
#include <QDir>
#include <QHBoxLayout>
#include <QImageReader>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QScreen>
#include <QStandardPaths>
#include <QStyle>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTouchEvent>
#include <QUrl>
#include <QUuid>
#include <QWindow>
#include <QtMath>
#include <cmath>

#ifdef Q_OS_MACOS
namespace DesktopActions {
QList<QRect> macVisibleWindowRects();
}
#endif

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
namespace {
QRect logicalWindowRect(HWND handle, const RECT &rect)
{
    MONITORINFOEXW monitor{};
    monitor.cbSize = sizeof(monitor);
    if (GetMonitorInfoW(MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST), &monitor)) {
        for (auto screen : QGuiApplication::screens()) {
            if (screen->name() == QString::fromWCharArray(monitor.szDevice)) {
                const qreal scale = screen->devicePixelRatio();
                return QRect(screen->geometry().topLeft() +
                                 QPoint(qRound((rect.left - monitor.rcMonitor.left) / scale),
                                        qRound((rect.top - monitor.rcMonitor.top) / scale)),
                             QSize(qRound((rect.right - rect.left) / scale),
                                   qRound((rect.bottom - rect.top) / scale)));
            }
        }
    }
    return QRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
}
void paintCursor(QImage &image, const QRect &bounds, qreal scale)
{
    CURSORINFO cursor{};
    cursor.cbSize = sizeof(cursor);
    if (!GetCursorInfo(&cursor) || !(cursor.flags & CURSOR_SHOWING)) return;
    ICONINFO icon{};
    if (!GetIconInfo(cursor.hCursor, &icon)) return;
    const QImage pixels = QImage::fromHICON(cursor.hCursor);
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    const qreal ratio = scale / (screen ? screen->devicePixelRatio() : scale);
    QPainter p(&image);
    p.drawImage(QRectF((QPointF(QCursor::pos() - bounds.topLeft()) * scale) -
                           QPointF(icon.xHotspot, icon.yHotspot) * ratio,
                       QSizeF(pixels.size()) * ratio),
                pixels);
    if (icon.hbmMask) DeleteObject(icon.hbmMask);
    if (icon.hbmColor) DeleteObject(icon.hbmColor);
}
} // namespace
#endif

#ifdef Q_OS_LINUX
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#endif

namespace {

void drawTaperedArrow(QPainter &p, const QPointF &a, const QPointF &b, const QColor &color, qreal width)
{
    const QPointF d = b - a;
    const qreal len = std::hypot(d.x(), d.y());
    if (len < 4.0) return;
    const qreal ux = d.x() / len;
    const qreal uy = d.y() / len;
    const qreal px = -uy;
    const qreal py = ux;

    const qreal headLen = qBound(12.0, width * 4.5, len * 0.55);
    const qreal headHalfW = headLen * 0.44;
    const qreal shaftHalfW = qMax(1.6, width * 0.62);
    const QPointF base = b - QPointF(ux * headLen, uy * headLen);

    QPainterPath path;
    path.moveTo(a + QPointF(px * (shaftHalfW * 0.45), py * (shaftHalfW * 0.45)));
    path.lineTo(base + QPointF(px * shaftHalfW, py * shaftHalfW));
    path.lineTo(base + QPointF(px * headHalfW, py * headHalfW));
    path.lineTo(b);
    path.lineTo(base - QPointF(px * headHalfW, py * headHalfW));
    path.lineTo(base - QPointF(px * shaftHalfW, py * shaftHalfW));
    path.lineTo(a - QPointF(px * (shaftHalfW * 0.45), py * (shaftHalfW * 0.45)));
    path.closeSubpath();

    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPath(path);
    p.restore();
}

void drawMosaicRect(QPainter &p, const QImage &srcFrame, const QSize &logicalSize,
                    const QRectF &logicalRect, qreal scaleX, qreal scaleY, const QPointF &originOffset)
{
    const QRect norm = logicalRect.normalized().toRect();
    if (norm.width() < 2 || norm.height() < 2 || logicalSize.isEmpty()) return;
    const QRect srcPx = ScreenshotRegionSelector::pixelRect(srcFrame.size(), logicalSize, norm);
    if (srcPx.width() < 2 || srcPx.height() < 2) return;

    const int block = qMax(8, qRound(10.0 * qMax(scaleX, scaleY)));
    const QSize downSize(qMax(1, srcPx.width() / block), qMax(1, srcPx.height() / block));
    const QImage pixelated = srcFrame.copy(srcPx)
                                 .scaled(downSize, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                                 .scaled(srcPx.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
    const QRectF dst((norm.x() * scaleX) - originOffset.x(),
                     (norm.y() * scaleY) - originOffset.y(),
                     norm.width() * scaleX,
                     norm.height() * scaleY);
    p.save();
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(dst, pixelated);
    p.restore();
}

} // namespace

// ============================================================================
// ScreenshotRegionSelector — Snipaste / PixPin / Flameshot Inline Studio
// ============================================================================

ScreenshotRegionSelector::ScreenshotRegionSelector(const QImage &image, const QRect &screenGeometry,
                                                   const QList<QRect> &windows, bool interactiveStudio)
    : frame(image), windowRects(windows), m_studioMode(interactiveStudio)
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setAttribute(Qt::WA_AcceptTouchEvents);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setWindowTitle(QStringLiteral("FloatingBall · 截图工作区"));
    setObjectName(QStringLiteral("captureSelector"));
    setCursor(Qt::CrossCursor);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setGeometry(screenGeometry);
    current = mapFromGlobal(QCursor::pos());
    if (!rect().contains(current)) current = rect().center();

    if (m_studioMode) {
        buildStudioToolbar();
    }
}

QRect ScreenshotRegionSelector::pixelRect(const QSize &imageSize, const QSize &logicalSize, const QRect &selected)
{
    if (imageSize.isEmpty() || logicalSize.isEmpty()) return {};
    const QRect rect = selected.intersected(QRect(QPoint(), logicalSize));
    if (rect.isEmpty()) return {};
    const qreal sx = qreal(imageSize.width()) / logicalSize.width();
    const qreal sy = qreal(imageSize.height()) / logicalSize.height();
    const int left = qFloor(rect.x() * sx), top = qFloor(rect.y() * sy);
    const int right = qCeil((rect.x() + rect.width()) * sx), bottom = qCeil((rect.y() + rect.height()) * sy);
    return QRect(left, top, right - left, bottom - top).intersected(QRect(QPoint(), imageSize));
}

QImage ScreenshotRegionSelector::crop(const QImage &image, const QSize &logicalSize, const QRect &selected)
{
    const QRect pixels = pixelRect(image.size(), logicalSize, selected);
    if (pixels.isEmpty()) return {};
    QImage result = image.copy(pixels);
    result.setDevicePixelRatio(1);
    return result;
}

QPoint ScreenshotRegionSelector::bounded(const QPoint &p) const
{
    return QPoint(qBound(0, p.x(), width()), qBound(0, p.y(), height()));
}

QRect ScreenshotRegionSelector::hoveredWindowRect(const QPoint &localPos) const
{
    if (windowRects.isEmpty()) return {};
    const QPoint global = mapToGlobal(localPos);
    for (const QRect &w : windowRects) {
        if (w.contains(global)) {
            const QRect loc = w.translated(-geometry().topLeft()).intersected(rect());
            if (loc.width() >= 24 && loc.height() >= 24) return loc;
        }
    }
    return {};
}

QRect ScreenshotRegionSelector::selection() const
{
    if (m_studioMode) {
        if (m_hasSelection) return m_selRect.normalized().intersected(rect());
        if (dragging) {
            return QRect(QPoint(qMin(first.x(), current.x()), qMin(first.y(), current.y())),
                         QSize(qAbs(current.x() - first.x()), qAbs(current.y() - first.y()))).intersected(rect());
        }
        return hoveredWindowRect(current);
    }
    if (!windowRects.isEmpty()) {
        return hoveredWindowRect(current);
    }
    return QRect(QPoint(qMin(first.x(), current.x()), qMin(first.y(), current.y())),
                 QSize(qAbs(current.x() - first.x()), qAbs(current.y() - first.y())));
}

QRect ScreenshotRegionSelector::cancelPillRect() const
{
    return QRect(qMax(12, width() - 108), qMin(28, qMax(8, height() - 50)), 92, 38);
}

ScreenshotRegionSelector::Handle ScreenshotRegionSelector::hitTestHandle(const QPoint &pos) const
{
    if (!m_hasSelection || m_selRect.isEmpty()) return HandleNone;
    const QRect r = m_selRect.normalized();
    constexpr int tol = 11;
    auto nearPt = [&](const QPoint &pt) {
        return qAbs(pos.x() - pt.x()) <= tol && qAbs(pos.y() - pt.y()) <= tol;
    };
    if (nearPt(r.topLeft())) return HandleTL;
    if (nearPt(r.topRight())) return HandleTR;
    if (nearPt(r.bottomLeft())) return HandleBL;
    if (nearPt(r.bottomRight())) return HandleBR;
    if (nearPt(QPoint(r.center().x(), r.top()))) return HandleT;
    if (nearPt(QPoint(r.center().x(), r.bottom()))) return HandleB;
    if (nearPt(QPoint(r.left(), r.center().y()))) return HandleL;
    if (nearPt(QPoint(r.right(), r.center().y()))) return HandleR;
    if (m_tool == ToolNone && r.contains(pos)) return HandleMove;
    return HandleNone;
}

void ScreenshotRegionSelector::updateHoverCursor(const QPoint &pos)
{
    if (!m_studioMode || !m_hasSelection) {
        setCursor(Qt::CrossCursor);
        return;
    }
    if (m_tool != ToolNone && m_selRect.contains(pos)) {
        setCursor(m_tool == ToolText ? Qt::IBeamCursor : Qt::CrossCursor);
        return;
    }
    switch (hitTestHandle(pos)) {
    case HandleTL:
    case HandleBR: setCursor(Qt::SizeFDiagCursor); break;
    case HandleTR:
    case HandleBL: setCursor(Qt::SizeBDiagCursor); break;
    case HandleT:
    case HandleB:  setCursor(Qt::SizeVerCursor); break;
    case HandleL:
    case HandleR:  setCursor(Qt::SizeHorCursor); break;
    case HandleMove: setCursor(Qt::SizeAllCursor); break;
    default: setCursor(Qt::CrossCursor); break;
    }
}

void ScreenshotRegionSelector::buildStudioToolbar()
{
    // Main floating 2026 Liquid Glass toolbar (Snipaste / PixPin / Flameshot style)
    m_toolbar = new QWidget(this);
    m_toolbar->setObjectName(QStringLiteral("studioToolbar"));
    m_toolbar->setStyleSheet(QStringLiteral(
        "QWidget#studioToolbar {"
        "  background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 rgba(36,44,64,234), stop:1 rgba(18,22,34,242));"
        "  border: 1px solid rgba(255,255,255,56);"
        "  border-radius: 15px;"
        "}"
        "QToolButton {"
        "  background: transparent; color: #e2e8f0; border: 1px solid transparent;"
        "  border-radius: 10px; padding: 5px 8px; font-size: 13px; font-weight: 600;"
        "  min-width: 24px; min-height: 24px;"
        "}"
        "QToolButton:hover { background: rgba(255,255,255,26); color: #ffffff; }"
        "QToolButton[active=\"true\"] {"
        "  background: rgba(59,130,246,165); border: 1px solid rgba(147,197,253,190); color: #ffffff;"
        "}"
        "QToolButton#studioPrimary {"
        "  background: rgba(59,130,246,215); border: 1px solid rgba(147,197,253,210);"
        "  color: #ffffff; padding: 5px 12px; font-size: 12.5px; font-weight: 700;"
        "}"
        "QToolButton#studioPrimary:hover { background: rgba(59,130,246,245); }"
        "QToolButton#studioCancel { color: #fda4af; }"
        "QToolButton#studioCancel:hover { background: rgba(244,63,94,65); color: #ffe4e6; }"
        "QWidget#studioSep { background: rgba(255,255,255,34); }"));

    auto *row = new QHBoxLayout(m_toolbar);
    row->setContentsMargins(8, 5, 8, 5);
    row->setSpacing(3);

    struct ToolDef { Tool tool; const char *glyph; const char *tip; };
    static const ToolDef kTools[]{
        {ToolRect,    "□",  "矩形框 (1)"},
        {ToolEllipse, "○",  "椭圆框 (2)"},
        {ToolArrow,   "↗",  "箭头标注 (3)"},
        {ToolPen,     "✎",  "自由画笔 (4)"},
        {ToolMosaic,  "▦",  "马赛克打码 (5)"},
        {ToolStep,    "①",  "步骤序号标注 (6)"},
        {ToolText,    "A",  "文字标注 (7)"}
    };

    m_toolBtns.clear();
    for (const ToolDef &td : kTools) {
        auto *b = new QToolButton(m_toolbar);
        b->setText(QString::fromUtf8(td.glyph));
        b->setToolTip(QString::fromUtf8(td.tip));
        b->setProperty("toolId", static_cast<int>(td.tool));
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QToolButton::clicked, this, [this, t = td.tool] {
            setActiveTool(m_tool == t ? ToolNone : t);
        });
        row->addWidget(b);
        m_toolBtns.append(b);
    }

    m_undoBtn = new QToolButton(m_toolbar);
    m_undoBtn->setText(QStringLiteral("↩"));
    m_undoBtn->setToolTip(QStringLiteral("撤销上一笔 (⌘Z / Ctrl+Z)"));
    m_undoBtn->setCursor(Qt::PointingHandCursor);
    m_undoBtn->setEnabled(false);
    connect(m_undoBtn, &QToolButton::clicked, this, &ScreenshotRegionSelector::undoAnnotation);
    row->addWidget(m_undoBtn);

    auto *sep = new QWidget(m_toolbar);
    sep->setObjectName(QStringLiteral("studioSep"));
    sep->setFixedSize(1, 20);
    row->addSpacing(3);
    row->addWidget(sep);
    row->addSpacing(3);

    auto makeAction = [&](const QString &label, const QString &tip, CaptureStudioAction act, const char *objName = nullptr) {
        auto *b = new QToolButton(m_toolbar);
        if (objName) b->setObjectName(QLatin1String(objName));
        b->setText(label);
        b->setToolTip(tip);
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QToolButton::clicked, this, [this, act] { finishStudioAction(act); });
        return b;
    };

    row->addWidget(makeAction(QStringLiteral("抠字"), QStringLiteral("提取选区中的文字 (OCR)"), CaptureStudioAction::Ocr));
    row->addWidget(makeAction(QStringLiteral("贴图"), QStringLiteral("钉在桌面悬浮置顶（滚轮缩放）"), CaptureStudioAction::Pin));
    row->addWidget(makeAction(QStringLiteral("保存"), QStringLiteral("保存无损 PNG (⌘S)"), CaptureStudioAction::Save));

    auto *cancelBtn = new QToolButton(m_toolbar);
    cancelBtn->setObjectName(QStringLiteral("studioCancel"));
    cancelBtn->setText(QStringLiteral("×"));
    cancelBtn->setToolTip(QStringLiteral("取消截图 (Esc / 右键)"));
    cancelBtn->setCursor(Qt::PointingHandCursor);
    connect(cancelBtn, &QToolButton::clicked, this, [this] {
        resolved = true;
        emit cancelled();
    });
    row->addWidget(cancelBtn);

    row->addWidget(makeAction(QStringLiteral("✓ 复制"), QStringLiteral("复制到剪贴板并完成 (Enter / 双击)"),
                              CaptureStudioAction::Copy, "studioPrimary"));
    m_toolbar->adjustSize();
    m_toolbar->hide();

    // Secondary floating Color + Stroke Weight sub-pill (shown only while an annotation tool is active)
    m_styleBar = new QWidget(this);
    m_styleBar->setObjectName(QStringLiteral("studioStyleBar"));
    m_styleBar->setStyleSheet(QStringLiteral(
        "QWidget#studioStyleBar {"
        "  background: rgba(22,28,42,232); border: 1px solid rgba(255,255,255,44); border-radius: 13px;"
        "}"
        "QToolButton { background: transparent; color: #cbd5e1; border: 1px solid transparent; border-radius: 8px; padding: 2px 6px; font-size: 11px; font-weight: 700; }"
        "QToolButton:hover { background: rgba(255,255,255,22); color: #ffffff; }"
        "QToolButton[active=\"true\"] { background: rgba(255,255,255,34); border-color: rgba(255,255,255,95); color: #ffffff; }"));
    auto *sRow = new QHBoxLayout(m_styleBar);
    sRow->setContentsMargins(8, 4, 8, 4);
    sRow->setSpacing(6);

    static const qreal kWidths[3]{2.0, 3.5, 6.0};
    static const char *kWLabels[3]{"细", "中", "粗"};
    for (int i = 0; i < 3; ++i) {
        auto *wb = new QToolButton(m_styleBar);
        wb->setText(QString::fromUtf8(kWLabels[i]));
        wb->setProperty("strokeW", kWidths[i]);
        wb->setProperty("active", i == 1);
        wb->setCursor(Qt::PointingHandCursor);
        connect(wb, &QToolButton::clicked, this, [this, w = kWidths[i]] {
            m_strokeWidth = w;
            for (auto *b : m_styleBar->findChildren<QToolButton *>()) {
                if (b->property("strokeW").isValid()) {
                    b->setProperty("active", qFuzzyCompare(b->property("strokeW").toDouble(), w));
                    b->style()->unpolish(b);
                    b->style()->polish(b);
                }
            }
        });
        sRow->addWidget(wb);
    }

    auto *sSep = new QWidget(m_styleBar);
    sSep->setFixedSize(1, 14);
    sSep->setStyleSheet(QStringLiteral("background: rgba(255,255,255,36);"));
    sRow->addWidget(sSep);

    static const char *kColors[6]{"#ef4444", "#f59e0b", "#22c55e", "#3b82f6", "#a855f7", "#ffffff"};
    for (const char *hex : kColors) {
        const QColor col(QString::fromLatin1(hex));
        auto *cb = new QToolButton(m_styleBar);
        cb->setFixedSize(20, 20);
        cb->setCursor(Qt::PointingHandCursor);
        cb->setProperty("swatchHex", QString::fromLatin1(hex));
        const bool sel = (col == m_color);
        cb->setStyleSheet(QStringLiteral(
            "QToolButton { background:%1; border:%2 solid %3; border-radius:10px; }")
            .arg(QString::fromLatin1(hex), sel ? QStringLiteral("2.5px") : QStringLiteral("1px"),
                 sel ? QStringLiteral("#ffffff") : QStringLiteral("rgba(255,255,255,95)")));
        connect(cb, &QToolButton::clicked, this, [this, col, hexStr = QString::fromLatin1(hex)] {
            m_color = col;
            for (auto *b : m_styleBar->findChildren<QToolButton *>()) {
                const QString h = b->property("swatchHex").toString();
                if (h.isEmpty()) continue;
                const bool s = (h == hexStr);
                b->setStyleSheet(QStringLiteral(
                    "QToolButton { background:%1; border:%2 solid %3; border-radius:10px; }")
                    .arg(h, s ? QStringLiteral("2.5px") : QStringLiteral("1px"),
                         s ? QStringLiteral("#ffffff") : QStringLiteral("rgba(255,255,255,95)")));
            }
            if (m_inlineTextEdit) {
                m_inlineTextEdit->setStyleSheet(QStringLiteral(
                    "QLineEdit { background:rgba(12,16,26,185); color:%1; border:1px solid %1;"
                    " border-radius:6px; padding:2px 6px; font-size:15px; font-weight:700; }")
                    .arg(m_color.name()));
            }
        });
        sRow->addWidget(cb);
    }
    m_styleBar->adjustSize();
    m_styleBar->hide();
}

void ScreenshotRegionSelector::setActiveTool(Tool tool)
{
    commitInlineText();
    m_tool = tool;
    for (QToolButton *b : m_toolBtns) {
        const bool act = (b->property("toolId").toInt() == static_cast<int>(m_tool));
        b->setProperty("active", act);
        b->style()->unpolish(b);
        b->style()->polish(b);
    }
    if (m_styleBar) {
        const bool showStyle = m_hasSelection && (m_tool != ToolNone) && (m_tool != ToolMosaic);
        m_styleBar->setVisible(showStyle);
        updateToolbarGeometry();
    }
    updateHoverCursor(mapFromGlobal(QCursor::pos()));
}

void ScreenshotRegionSelector::undoAnnotation()
{
    commitInlineText();
    if (m_annotations.isEmpty()) return;
    if (m_annotations.last().tool == ToolStep && m_nextStep > 1)
        --m_nextStep;
    m_annotations.removeLast();
    if (m_undoBtn) m_undoBtn->setEnabled(!m_annotations.isEmpty());
    update();
}

void ScreenshotRegionSelector::commitInlineText()
{
    if (!m_inlineTextEdit) return;
    const QString txt = m_inlineTextEdit->text().trimmed();
    m_inlineTextEdit->deleteLater();
    m_inlineTextEdit = nullptr;
    if (!txt.isEmpty()) {
        Annotation ann;
        ann.tool = ToolText;
        ann.color = m_color;
        ann.width = m_strokeWidth;
        ann.p1 = m_inlineTextAnchor;
        ann.text = txt;
        m_annotations.append(ann);
        if (m_undoBtn) m_undoBtn->setEnabled(true);
        update();
    }
}

void ScreenshotRegionSelector::updateToolbarGeometry()
{
    if (!m_toolbar) return;
    if (!m_studioMode || !m_hasSelection || m_selRect.isEmpty()) {
        m_toolbar->hide();
        if (m_styleBar) m_styleBar->hide();
        return;
    }
    m_toolbar->adjustSize();
    const QRect r = m_selRect.normalized();
    const int tw = m_toolbar->width();
    const int th = m_toolbar->height();
    const int pad = 10;

    int x = r.right() - tw + 1;
    x = qBound(pad, x, qMax(pad, width() - tw - pad));

    int y = r.bottom() + 10;
    bool placedAbove = false;
    if (y + th + pad > height()) {
        // Flip above selection if near bottom edge
        if (r.top() - th - 10 >= pad) {
            y = r.top() - th - 10;
            placedAbove = true;
        } else {
            // Selection fills vertical screen -> tuck inside bottom-right
            y = qMax(pad, r.bottom() - th - 12);
        }
    }
    m_toolbar->move(x, y);
    m_toolbar->show();
    m_toolbar->raise();

    if (m_styleBar && m_styleBar->isVisible()) {
        m_styleBar->adjustSize();
        const int sw = m_styleBar->width();
        const int sh = m_styleBar->height();
        int sx = qBound(pad, x, qMax(pad, width() - sw - pad));
        int sy = placedAbove ? (y - sh - 6) : (y + th + 6);
        if (sy + sh + pad > height()) sy = y - sh - 6;
        if (sy < pad) sy = y + th + 6;
        m_styleBar->move(sx, sy);
        m_styleBar->raise();
    }
}

QColor ScreenshotRegionSelector::samplePixelColor(const QPoint &localPos) const
{
    if (frame.isNull() || width() <= 0 || height() <= 0) return Qt::black;
    const int px = qBound(0, qRound(localPos.x() * qreal(frame.width()) / width()), frame.width() - 1);
    const int py = qBound(0, qRound(localPos.y() * qreal(frame.height()) / height()), frame.height() - 1);
    return frame.pixelColor(px, py);
}

void ScreenshotRegionSelector::paintAnnotations(QPainter &p, const QRect &clipLogical,
                                                qreal scaleX, qreal scaleY, const QPointF &originOffset) const
{
    if (m_annotations.isEmpty() && !m_drawingAnnotation) return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF clipRect((clipLogical.x() * scaleX) - originOffset.x(),
                          (clipLogical.y() * scaleY) - originOffset.y(),
                          clipLogical.width() * scaleX,
                          clipLogical.height() * scaleY);
    p.setClipRect(clipRect);

    auto mapPt = [&](const QPointF &pt) {
        return QPointF(pt.x() * scaleX - originOffset.x(), pt.y() * scaleY - originOffset.y());
    };
    const qreal avgScale = (scaleX + scaleY) * 0.5;

    auto renderOne = [&](const Annotation &ann) {
        if (ann.tool == ToolMosaic) {
            drawMosaicRect(p, frame, size(), QRectF(ann.p1, ann.p2), scaleX, scaleY, originOffset);
        } else if (ann.tool == ToolRect) {
            p.setPen(QPen(ann.color, ann.width * avgScale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(mapPt(ann.p1), mapPt(ann.p2)).normalized(), 4.0 * avgScale, 4.0 * avgScale);
        } else if (ann.tool == ToolEllipse) {
            p.setPen(QPen(ann.color, ann.width * avgScale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QRectF(mapPt(ann.p1), mapPt(ann.p2)).normalized());
        } else if (ann.tool == ToolArrow) {
            drawTaperedArrow(p, mapPt(ann.p1), mapPt(ann.p2), ann.color, ann.width * avgScale);
        } else if (ann.tool == ToolPen && ann.points.size() >= 2) {
            p.setPen(QPen(ann.color, ann.width * avgScale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            QPainterPath path;
            path.moveTo(mapPt(ann.points.first()));
            for (int i = 1; i < ann.points.size(); ++i) path.lineTo(mapPt(ann.points.at(i)));
            p.drawPath(path);
        } else if (ann.tool == ToolStep) {
            const QPointF c = mapPt(ann.p1);
            const qreal r = qMax(11.0, (9.0 + ann.width * 1.4)) * avgScale;
            p.setPen(QPen(QColor("#ffffff"), 2.0 * avgScale));
            p.setBrush(ann.color);
            p.drawEllipse(c, r, r);
            QFont f = font();
            f.setPixelSize(qRound(r * 1.25));
            f.setWeight(QFont::Bold);
            p.setFont(f);
            p.setPen(ann.color.lightness() > 210 ? QColor("#0f172a") : QColor("#ffffff"));
            p.drawText(QRectF(c.x() - r, c.y() - r, r * 2, r * 2), Qt::AlignCenter, QString::number(ann.stepNumber));
        } else if (ann.tool == ToolText && !ann.text.isEmpty()) {
            const QPointF pt = mapPt(ann.p1);
            QFont f = font();
            f.setPixelSize(qRound((14.0 + ann.width * 2.2) * avgScale));
            f.setWeight(QFont::Bold);
            p.setFont(f);
            p.setPen(QColor(0, 0, 0, 155));
            p.drawText(pt + QPointF(1.2 * avgScale, 1.2 * avgScale), ann.text);
            p.setPen(ann.color);
            p.drawText(pt, ann.text);
        }
    };

    for (const Annotation &ann : m_annotations) renderOne(ann);
    if (m_drawingAnnotation) renderOne(m_liveAnn);
    p.restore();
}

QImage ScreenshotRegionSelector::renderAnnotatedCrop() const
{
    const QRect area = selection();
    if (area.width() < 2 || area.height() < 2) return {};
    const QRect pxRect = pixelRect(frame.size(), size(), area);
    if (pxRect.isEmpty()) return {};
    QImage out = frame.copy(pxRect);
    out.setDevicePixelRatio(1);
    if (!m_annotations.isEmpty()) {
        const qreal sx = qreal(frame.width()) / width();
        const qreal sy = qreal(frame.height()) / height();
        QPainter p(&out);
        paintAnnotations(p, area, sx, sy, QPointF(pxRect.topLeft()));
    }
    return out;
}

void ScreenshotRegionSelector::finishStudioAction(CaptureStudioAction action)
{
    if (resolved) return;
    commitInlineText();
    const QRect area = selection();
    if (area.width() < 2 || area.height() < 2) return;
    const QImage result = renderAnnotatedCrop();
    if (result.isNull()) return;
    resolved = true;
    emit studioActionSelected(action, result, area.translated(geometry().topLeft()));
}

void ScreenshotRegionSelector::paintMagnifierLoupe(QPainter &p) const
{
    if (frame.isNull() || width() <= 0 || height() <= 0) return;
    constexpr int gridCells = 19; // odd -> exact center pixel
    constexpr int cellPx = 6;
    constexpr int previewSize = gridCells * cellPx; // 114
    constexpr int boxW = previewSize + 14;
    constexpr int boxH = previewSize + 44;

    int lx = current.x() + 22;
    int ly = current.y() + 22;
    if (lx + boxW + 10 > width()) lx = current.x() - boxW - 22;
    if (ly + boxH + 10 > height()) ly = current.y() - boxH - 22;
    lx = qBound(10, lx, qMax(10, width() - boxW - 10));
    ly = qBound(10, ly, qMax(10, height() - boxH - 10));

    const QRectF card(lx, ly, boxW, boxH);
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(255, 255, 255, 62), 1.0));
    p.setBrush(QColor(18, 22, 34, 236));
    p.drawRoundedRect(card, 14, 14);

    // Zoomed pixel region from native HiDPI frame
    const int cx = qBound(0, qRound(current.x() * qreal(frame.width()) / width()), frame.width() - 1);
    const int cy = qBound(0, qRound(current.y() * qreal(frame.height()) / height()), frame.height() - 1);
    const int half = gridCells / 2;
    const QRectF imgBox(lx + 7, ly + 7, previewSize, previewSize);

    QPainterPath clip;
    clip.addRoundedRect(imgBox, 9, 9);
    p.save();
    p.setClipPath(clip);
    p.fillRect(imgBox, QColor("#0b0f18"));
    for (int dy = -half; dy <= half; ++dy) {
        const int py = cy + dy;
        if (py < 0 || py >= frame.height()) continue;
        for (int dx = -half; dx <= half; ++dx) {
            const int px = cx + dx;
            if (px < 0 || px >= frame.width()) continue;
            p.fillRect(QRectF(imgBox.left() + (dx + half) * cellPx,
                              imgBox.top() + (dy + half) * cellPx, cellPx, cellPx),
                       frame.pixelColor(px, py));
        }
    }
    // Crosshair reticle on center pixel
    const QRectF centerCell(imgBox.left() + half * cellPx, imgBox.top() + half * cellPx, cellPx, cellPx);
    p.setPen(QPen(QColor(56, 189, 248, 175), 1.0));
    p.drawLine(QPointF(imgBox.left(), centerCell.center().y()), QPointF(centerCell.left(), centerCell.center().y()));
    p.drawLine(QPointF(centerCell.right(), centerCell.center().y()), QPointF(imgBox.right(), centerCell.center().y()));
    p.drawLine(QPointF(centerCell.center().x(), imgBox.top()), QPointF(centerCell.center().x(), centerCell.top()));
    p.drawLine(QPointF(centerCell.center().x(), centerCell.bottom()), QPointF(centerCell.center().x(), imgBox.bottom()));
    p.setPen(QPen(QColor("#ffffff"), 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawRect(centerCell);
    p.restore();

    // Footer: color swatch + HEX + C copy hint
    const QColor c = frame.pixelColor(cx, cy);
    const QString hex = c.name(QColor::HexRgb).toUpper();
    p.setPen(QPen(QColor(255, 255, 255, 110), 1.0));
    p.setBrush(c);
    p.drawRoundedRect(QRectF(lx + 9, ly + previewSize + 15, 14, 14), 4, 4);

    QFont f = font();
    f.setPixelSize(11);
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    p.setPen(QColor("#f8fafc"));
    p.drawText(QRectF(lx + 28, ly + previewSize + 12, boxW - 36, 16), Qt::AlignLeft | Qt::AlignVCenter, hex);

    f.setPixelSize(10);
    f.setWeight(QFont::Normal);
    p.setFont(f);
    p.setPen(QColor("#94a3b8"));
    p.drawText(QRectF(lx + 9, ly + previewSize + 27, boxW - 18, 14), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("按 C 取色 · %1,%2").arg(cx).arg(cy));
    p.restore();
}

bool ScreenshotRegionSelector::event(QEvent *ev)
{
    if (ev->type() == QEvent::TouchBegin || ev->type() == QEvent::TouchUpdate ||
        ev->type() == QEvent::TouchEnd || ev->type() == QEvent::TouchCancel) {
        auto *te = static_cast<QTouchEvent *>(ev);
        const auto pts = te->points();
        if (ev->type() == QEvent::TouchCancel || pts.size() >= 2) {
            resolved = true;
            emit cancelled();
            return true;
        }
        if (pts.size() == 1 && !m_hasSelection) {
            const QPoint pos = bounded(pts.first().position().toPoint());
            if (ev->type() == QEvent::TouchBegin) {
                if (!m_studioMode && cancelPillRect().contains(pos)) {
                    resolved = true;
                    emit cancelled();
                    return true;
                }
                first = current = pos;
                dragging = true;
                update();
            } else if (ev->type() == QEvent::TouchUpdate && dragging) {
                current = pos;
                update();
            } else if (ev->type() == QEvent::TouchEnd && dragging && !resolved) {
                current = pos;
                const QRect area = selection();
                dragging = false;
                if (area.width() >= 4 && area.height() >= 4) {
                    if (m_studioMode) {
                        m_hasSelection = true;
                        m_selRect = area;
                        updateToolbarGeometry();
                        update();
                    } else {
                        resolved = true;
                        emit selected(crop(frame, size(), area), area.translated(geometry().topLeft()));
                    }
                } else {
                    update();
                }
            }
            return true;
        }
    }
    return QWidget::event(ev);
}

void ScreenshotRegionSelector::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.drawImage(rect(), frame);
    p.fillRect(rect(), QColor(10, 14, 24, 118));

    const QRect area = selection();
    const bool showArea = m_hasSelection || dragging || (!windowRects.isEmpty() && !area.isEmpty());
    if (showArea && !area.isEmpty()) {
        p.save();
        p.setClipRect(area);
        p.drawImage(rect(), frame);
        p.restore();

        // Live inline annotations over selection
        if (m_studioMode && m_hasSelection) {
            paintAnnotations(p, area, 1.0, 1.0, QPointF(0, 0));
        }

        // Crisp luminous selection border (Snipaste / macOS 27 blue)
        p.setPen(QPen(QColor("#38bdf8"), m_hasSelection ? 2.0 : 2.2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(area).adjusted(0.5, 0.5, -0.5, -0.5));

        // 8 crisp resize handles when adjusted/selected (4 corners + 4 edge midpoints)
        if (m_studioMode && m_hasSelection) {
            const QPoint handles[8]{
                area.topLeft(), QPoint(area.center().x(), area.top()), area.topRight(),
                QPoint(area.right(), area.center().y()), area.bottomRight(),
                QPoint(area.center().x(), area.bottom()), area.bottomLeft(),
                QPoint(area.left(), area.center().y())
            };
            p.setPen(QPen(QColor("#38bdf8"), 1.6));
            p.setBrush(QColor("#ffffff"));
            for (const QPoint &h : handles) p.drawEllipse(QPointF(h), 4.5, 4.5);
        } else {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#ffffff"));
            for (const QPoint &corner : {area.topLeft(), area.topRight(), area.bottomLeft(), area.bottomRight()})
                p.drawEllipse(corner, 4, 4);
        }

        // Compact floating dimension badge anchored top-left of selection (Snipaste/Flameshot style!)
        const QSize px = pixelRect(frame.size(), size(), area).size();
        const QString dimTxt = QStringLiteral("%1 × %2").arg(px.width()).arg(px.height());
        QFont df = p.font();
        df.setPixelSize(12);
        df.setWeight(QFont::DemiBold);
        p.setFont(df);
        const int dw = p.fontMetrics().horizontalAdvance(dimTxt) + 16;
        const int dh = 24;
        int dx = qBound(8, area.left(), qMax(8, width() - dw - 8));
        int dy = (area.top() - dh - 6 >= 8) ? (area.top() - dh - 6) : (area.top() + 8);
        p.setPen(QPen(QColor(255, 255, 255, 46), 1.0));
        p.setBrush(QColor(18, 22, 34, 224));
        p.drawRoundedRect(QRect(dx, dy, dw, dh), 8, 8);
        p.setPen(QColor("#f8fafc"));
        p.drawText(QRect(dx, dy, dw, dh), Qt::AlignCenter, dimTxt);
    }

    if (m_studioMode) {
        if (!m_hasSelection) {
            paintMagnifierLoupe(p);
            // Minimal unobtrusive top-center hint pill
            const QString hintTxt = !windowRects.isEmpty()
                ? QStringLiteral("单击选中窗口 · 拖动框选区域 · 按 C 取色 · 右键 / Esc 取消")
                : QStringLiteral("拖动框选区域 · 按 C 取色 · 右键 / Esc 取消");
            QFont hf = p.font();
            hf.setPixelSize(12);
            hf.setWeight(QFont::DemiBold);
            p.setFont(hf);
            const int hw = p.fontMetrics().horizontalAdvance(hintTxt) + 28;
            const QRect hr(qMax(12, (width() - hw) / 2), 20, qMin(hw, width() - 24), 32);
            p.setPen(QPen(QColor(255, 255, 255, 44), 1.0));
            p.setBrush(QColor(18, 22, 34, 218));
            p.drawRoundedRect(hr, 16, 16);
            p.setPen(QColor("#e2e8f0"));
            p.drawText(hr, Qt::AlignCenter, hintTxt);
        }
        return;
    }

    // Legacy non-studio mode HUD
    const QSize pixels = pixelRect(frame.size(), size(), area).size();
    const QString text = !windowRects.isEmpty()
        ? QStringLiteral("轻点选择窗口 · 右上角或 Esc 取消")
        : dragging ? QStringLiteral("%1 × %2 px · 松手完成").arg(pixels.width()).arg(pixels.height())
                   : QStringLiteral("手指或鼠标拖动框选 · 双指 / Esc 取消");
    QFont font = p.font();
    font.setPixelSize(13);
    font.setWeight(QFont::DemiBold);
    p.setFont(font);
    const int w = p.fontMetrics().horizontalAdvance(text) + 36;
    const QRect hint(qMax(12, (width() - w) / 2), qMin(28, qMax(8, height() - 50)), qMin(w, width() - 140), 38);
    p.setPen(QPen(QColor(255, 255, 255, 45), 1.0));
    p.setBrush(QColor(20, 24, 32, 232));
    p.drawRoundedRect(hint, 19, 19);
    p.setPen(QColor("#f5f7fc"));
    p.drawText(hint, Qt::AlignCenter, text);

    const QRect cancelRect = cancelPillRect();
    p.setPen(QPen(QColor(255, 120, 140, 90), 1.0));
    p.setBrush(QColor(46, 22, 30, 230));
    p.drawRoundedRect(cancelRect, 19, 19);
    p.setPen(QColor("#ffd6de"));
    p.drawText(cancelRect, Qt::AlignCenter, QStringLiteral("× 取消"));
}

void ScreenshotRegionSelector::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        if (m_studioMode && m_hasSelection && m_tool != ToolNone) {
            setActiveTool(ToolNone);
            return;
        }
        resolved = true;
        emit cancelled();
        return;
    }
    if (event->button() != Qt::LeftButton) return;

    const QPoint pos = bounded(event->pos());
    if (!m_studioMode) {
        if (cancelPillRect().contains(pos)) {
            resolved = true;
            emit cancelled();
            return;
        }
        first = current = pos;
        dragging = true;
        update();
        return;
    }

    commitInlineText();

    if (!m_hasSelection) {
        m_hoverPressActive = true;
        m_hoverPressPos = pos;
        first = current = pos;
        dragging = false;
        update();
        return;
    }

    // Active selection in studio mode: check resize handles first!
    const Handle h = hitTestHandle(pos);
    if (h != HandleNone && h != HandleMove) {
        m_dragHandle = h;
        m_dragStartPos = pos;
        m_dragStartRect = m_selRect.normalized();
        return;
    }

    // Active annotation tool inside selection
    if (m_tool != ToolNone && m_selRect.contains(pos)) {
        if (m_tool == ToolStep) {
            Annotation ann;
            ann.tool = ToolStep;
            ann.color = m_color;
            ann.width = m_strokeWidth;
            ann.p1 = pos;
            ann.stepNumber = m_nextStep++;
            m_annotations.append(ann);
            if (m_undoBtn) m_undoBtn->setEnabled(true);
            update();
            return;
        }
        if (m_tool == ToolText) {
            m_inlineTextAnchor = QPointF(pos.x(), pos.y() + 14);
            auto *ed = new QLineEdit(this);
            ed->setPlaceholderText(QStringLiteral("输入文字，回车确认…"));
            ed->setStyleSheet(QStringLiteral(
                "QLineEdit { background:rgba(12,16,26,195); color:%1; border:1px solid %1;"
                " border-radius:6px; padding:3px 7px; font-size:15px; font-weight:700; }")
                .arg(m_color.name()));
            ed->setFixedWidth(qMin(220, qMax(120, m_selRect.right() - pos.x() - 6)));
            ed->move(qBound(m_selRect.left() + 4, pos.x(), qMax(m_selRect.left() + 4, m_selRect.right() - ed->width() - 4)),
                     qBound(m_selRect.top() + 4, pos.y() - 14, qMax(m_selRect.top() + 4, m_selRect.bottom() - 32)));
            connect(ed, &QLineEdit::returnPressed, this, &ScreenshotRegionSelector::commitInlineText);
            ed->show();
            ed->raise();
            ed->setFocus();
            m_inlineTextEdit = ed;
            return;
        }
        m_drawingAnnotation = true;
        m_liveAnn = Annotation{};
        m_liveAnn.tool = m_tool;
        m_liveAnn.color = m_color;
        m_liveAnn.width = m_strokeWidth;
        m_liveAnn.p1 = pos;
        m_liveAnn.p2 = pos;
        if (m_tool == ToolPen) m_liveAnn.points.append(pos);
        update();
        return;
    }

    if (h == HandleMove) {
        m_dragHandle = HandleMove;
        m_dragStartPos = pos;
        m_dragStartRect = m_selRect.normalized();
        return;
    }

    // Clicked outside selection with no tool -> start a fresh selection!
    m_hasSelection = false;
    m_annotations.clear();
    m_nextStep = 1;
    if (m_undoBtn) m_undoBtn->setEnabled(false);
    setActiveTool(ToolNone);
    updateToolbarGeometry();
    m_hoverPressActive = true;
    m_hoverPressPos = pos;
    first = current = pos;
    dragging = false;
    update();
}

void ScreenshotRegionSelector::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint pos = bounded(event->pos());
    current = pos;

    if (!m_studioMode) {
        if (dragging || !windowRects.isEmpty()) update();
        return;
    }

    if (m_hoverPressActive && !m_hasSelection) {
        if (!dragging && (pos - m_hoverPressPos).manhattanLength() >= 4) {
            dragging = true;
        }
        update();
        return;
    }

    if (m_drawingAnnotation) {
        m_liveAnn.p2 = pos;
        if (m_liveAnn.tool == ToolPen) m_liveAnn.points.append(pos);
        update();
        return;
    }

    if (m_dragHandle != HandleNone) {
        const QPoint d = pos - m_dragStartPos;
        QRect r = m_dragStartRect;
        if (m_dragHandle == HandleMove) {
            r.translate(d);
            if (r.left() < 0) r.moveLeft(0);
            if (r.top() < 0) r.moveTop(0);
            if (r.right() > width() - 1) r.moveRight(width() - 1);
            if (r.bottom() > height() - 1) r.moveBottom(height() - 1);
        } else {
            if (m_dragHandle == HandleTL || m_dragHandle == HandleL || m_dragHandle == HandleBL)
                r.setLeft(qBound(0, r.left() + d.x(), r.right() - 12));
            if (m_dragHandle == HandleTR || m_dragHandle == HandleR || m_dragHandle == HandleBR)
                r.setRight(qBound(r.left() + 12, r.right() + d.x(), width() - 1));
            if (m_dragHandle == HandleTL || m_dragHandle == HandleT || m_dragHandle == HandleTR)
                r.setTop(qBound(0, r.top() + d.y(), r.bottom() - 12));
            if (m_dragHandle == HandleBL || m_dragHandle == HandleB || m_dragHandle == HandleBR)
                r.setBottom(qBound(r.top() + 12, r.bottom() + d.y(), height() - 1));
        }
        m_selRect = r.normalized();
        updateToolbarGeometry();
        update();
        return;
    }

    updateHoverCursor(pos);
    if (!m_hasSelection) update();
}

void ScreenshotRegionSelector::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || resolved) return;
    current = bounded(event->pos());

    if (!m_studioMode) {
        if (!dragging) return;
        const QRect area = selection();
        dragging = false;
        if (area.width() < 2 || area.height() < 2) {
            update();
            return;
        }
        resolved = true;
        emit selected(crop(frame, size(), area), area.translated(geometry().topLeft()));
        return;
    }

    if (m_drawingAnnotation) {
        m_drawingAnnotation = false;
        m_liveAnn.p2 = current;
        if (m_liveAnn.tool == ToolPen) {
            if (m_liveAnn.points.size() >= 2) m_annotations.append(m_liveAnn);
        } else if ((m_liveAnn.p2 - m_liveAnn.p1).manhattanLength() >= 4.0) {
            m_annotations.append(m_liveAnn);
        }
        if (m_undoBtn) m_undoBtn->setEnabled(!m_annotations.isEmpty());
        update();
        return;
    }

    if (m_dragHandle != HandleNone) {
        m_dragHandle = HandleNone;
        updateToolbarGeometry();
        updateHoverCursor(current);
        return;
    }

    if (m_hoverPressActive && !m_hasSelection) {
        m_hoverPressActive = false;
        QRect chosen;
        if (dragging) {
            chosen = QRect(QPoint(qMin(first.x(), current.x()), qMin(first.y(), current.y())),
                           QSize(qAbs(current.x() - first.x()), qAbs(current.y() - first.y()))).intersected(rect());
        } else {
            // Single click on auto-detected window!
            chosen = hoveredWindowRect(current);
        }
        dragging = false;
        if (chosen.width() >= 8 && chosen.height() >= 8) {
            m_hasSelection = true;
            m_selRect = chosen;
            updateToolbarGeometry();
            updateHoverCursor(current);
        }
        update();
    }
}

void ScreenshotRegionSelector::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (m_studioMode && m_hasSelection && m_tool == ToolNone &&
        event->button() == Qt::LeftButton && m_selRect.contains(event->pos())) {
        finishStudioAction(CaptureStudioAction::Copy);
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void ScreenshotRegionSelector::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        if (m_inlineTextEdit) {
            m_inlineTextEdit->deleteLater();
            m_inlineTextEdit = nullptr;
            return;
        }
        if (m_studioMode && m_tool != ToolNone) {
            setActiveTool(ToolNone);
            return;
        }
        resolved = true;
        emit cancelled();
        return;
    }
    if (m_studioMode) {
        if (!m_hasSelection && event->key() == Qt::Key_C && event->modifiers() == Qt::NoModifier) {
            const QString hex = samplePixelColor(current).name(QColor::HexRgb).toUpper();
            QApplication::clipboard()->setText(hex);
            DesktopActions::showToast(QStringLiteral("✓ 已复制色值 %1").arg(hex), mapToGlobal(current));
            resolved = true;
            emit cancelled();
            return;
        }
        if (m_hasSelection) {
            const bool ctrl = (event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) != 0;
            if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
                (ctrl && event->key() == Qt::Key_C)) {
                finishStudioAction(CaptureStudioAction::Copy);
                return;
            }
            if (ctrl && event->key() == Qt::Key_S) {
                finishStudioAction(CaptureStudioAction::Save);
                return;
            }
            if (ctrl && event->key() == Qt::Key_Z) {
                undoAnnotation();
                return;
            }
            if (event->modifiers() == Qt::NoModifier) {
                if (event->key() == Qt::Key_1) { setActiveTool(ToolRect); return; }
                if (event->key() == Qt::Key_2) { setActiveTool(ToolEllipse); return; }
                if (event->key() == Qt::Key_3) { setActiveTool(ToolArrow); return; }
                if (event->key() == Qt::Key_4) { setActiveTool(ToolPen); return; }
                if (event->key() == Qt::Key_5) { setActiveTool(ToolMosaic); return; }
                if (event->key() == Qt::Key_6) { setActiveTool(ToolStep); return; }
                if (event->key() == Qt::Key_7) { setActiveTool(ToolText); return; }
            }
        }
    }
    QWidget::keyPressEvent(event);
}

void ScreenshotRegionSelector::closeEvent(QCloseEvent *event)
{
    if (!resolved) {
        resolved = true;
        emit cancelled();
    }
    event->accept();
}

// ============================================================================
// ScreenshotCapture
// ============================================================================

ScreenshotCapture::ScreenshotCapture(QObject *parent) : QObject(parent) {}
ScreenshotCapture::~ScreenshotCapture()
{
    active = false;
    cleanUp();
}

void ScreenshotCapture::start(const QPoint &anchor, const CaptureOptions &requested, bool interactiveStudio)
{
    if (active) return;
    active = true;
    studioMode = interactiveStudio;
    pendingGlobalRect = {};
    options = requested;
    options.delaySeconds = qBound(0, options.delaySeconds, 30);
    const quint64 run = ++generation;
    emit countdownChanged(options.delaySeconds);
    for (int second = 1; second <= options.delaySeconds; ++second) {
        QTimer::singleShot(second * 1000, this, [this, run, second] {
            if (active && generation == run)
                emit countdownChanged(options.delaySeconds - second);
        });
    }
    QTimer::singleShot(options.delaySeconds * 1000 + 200, this, [this, anchor, run] {
        if (active && generation == run) captureDesktop(anchor);
    });
    QTimer::singleShot(600000, this, [this, run] {
        if (active && generation == run) finish({}, QStringLiteral("截图等待超时，请重试"));
    });
}

void ScreenshotCapture::startDirectRect(QScreen *screen, const QRect &globalRect, int settleMs)
{
    if (active) return;
    if (!screen) screen = QGuiApplication::screenAt(globalRect.center());
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen || globalRect.width() < 4 || globalRect.height() < 4) {
        emit finished({}, QStringLiteral("框选范围太小"), true);
        return;
    }
    active = true;
    studioMode = false;
    pendingGlobalRect = globalRect;
    const quint64 run = ++generation;
    QPointer<QScreen> targetScreen(screen);
    QTimer::singleShot(qMax(10, settleMs), this, [this, targetScreen, globalRect, run] {
        if (active && generation == run) captureSilentRect(targetScreen, globalRect);
    });
    QTimer::singleShot(30000, this, [this, run] {
        if (active && generation == run) finish({}, QStringLiteral("准星截图超时，请重试"));
    });
}

void ScreenshotCapture::cancel()
{
    if (active) finish({}, {}, true);
}

void ScreenshotCapture::launchSelector(const QImage &image, const QRect &screenBounds, const QList<QRect> &windows)
{
    auto *selector = new ScreenshotRegionSelector(image, screenBounds, windows, studioMode);
    selectors.append(selector);
    if (studioMode) {
        connect(selector, &ScreenshotRegionSelector::studioActionSelected, this,
                [this](CaptureStudioAction action, const QImage &value, const QRect &globalSel) {
            if (!active) return;
            active = false;
            cleanUp();
            emit studioFinished(action, value, globalSel);
        });
    } else {
        connect(selector, &ScreenshotRegionSelector::selected, this,
                [this](const QImage &value, const QRect &) { finish(value); });
    }
    connect(selector, &ScreenshotRegionSelector::cancelled, this, &ScreenshotCapture::cancel);
    if (QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))) {
        FloatingWindow::prepare(selector, true);
    }
    selector->show();
    selector->raise();
    selector->activateWindow();
    selector->setFocus();
}

void ScreenshotCapture::captureSilentRect(QScreen *display, const QRect &globalRect)
{
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
        finish({}, QStringLiteral("当前显示环境不支持屏幕截图"));
        return;
    }
    if (!display) display = QGuiApplication::primaryScreen();
    if (!display) {
        finish({}, QStringLiteral("没有可截图的显示器"));
        return;
    }
    const QRect screenGeo = display->geometry();
    const QRect localRect = globalRect.intersected(screenGeo).translated(-screenGeo.topLeft());
    if (localRect.width() < 2 || localRect.height() < 2) {
        finish({}, {}, true);
        return;
    }

#ifdef Q_OS_MACOS
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (!QDir().mkpath(cache)) { finish({}, QStringLiteral("无法创建截图临时目录")); return; }
    temporary = std::make_unique<QTemporaryDir>(cache + QStringLiteral("/capture-XXXXXX"));
    if (!temporary->isValid()) { finish({}, QStringLiteral("无法创建截图临时目录")); return; }
    const QString file = temporary->filePath(QStringLiteral("crosshair.png"));
    process = new QProcess(this);
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finish({}, QStringLiteral("无法启动系统截图工具"));
    });
    const QSize logicalSize = screenGeo.size();
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, file, logicalSize, localRect](int, QProcess::ExitStatus exit) {
        if (!active) return;
        QImageReader reader(file);
        reader.setAutoTransform(true);
        const QImage full = reader.read();
        if (!full.isNull()) {
            const QImage cropped = ScreenshotRegionSelector::crop(full, logicalSize, localRect);
            if (!cropped.isNull()) { finish(cropped); return; }
        }
        if (exit == QProcess::NormalExit && full.isNull()) {
            finish({}, QStringLiteral("系统截图失败，请检查屏幕录制权限"));
            return;
        }
        finish({}, QStringLiteral("截图裁剪失败"));
    });
    const int displayIndex = qMax(0, QGuiApplication::screens().indexOf(display)) + 1;
    process->start(QStringLiteral("/usr/sbin/screencapture"),
                   {QStringLiteral("-x"), QStringLiteral("-t"), QStringLiteral("png"),
                    QStringLiteral("-D"), QString::number(displayIndex), file});
    return;
#elif defined(Q_OS_LINUX)
    if (QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))) {
        const QString spectacle = QStandardPaths::findExecutable(QStringLiteral("spectacle"));
        if (!spectacle.isEmpty()) {
            temporary = std::make_unique<QTemporaryDir>();
            if (!temporary->isValid()) { finish({}, QStringLiteral("无法创建截图临时目录")); return; }
            const QString file = temporary->filePath(QStringLiteral("crosshair.png"));
            process = new QProcess(this);
            connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart) finish({}, QStringLiteral("无法启动桌面截图服务"));
            });
            QRect allBounds;
            for (QScreen *s : QGuiApplication::screens()) allBounds = allBounds.united(s->geometry());
            const QRect allLocal = globalRect.intersected(allBounds).translated(-allBounds.topLeft());
            const QSize allLogical = allBounds.size();
            connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                    [this, file, allLogical, allLocal](int, QProcess::ExitStatus) {
                if (!active) return;
                QImageReader reader(file);
                reader.setAutoTransform(true);
                const QImage full = reader.read();
                if (!full.isNull()) {
                    const QImage cropped = ScreenshotRegionSelector::crop(full, allLogical, allLocal);
                    if (!cropped.isNull()) { finish(cropped); return; }
                }
                finish({}, QStringLiteral("Wayland 准星截图失败，请重试或改用普通区域模式"));
            });
            process->start(spectacle, {QStringLiteral("--new-instance"), QStringLiteral("--background"),
                                       QStringLiteral("--nonotify"), QStringLiteral("--fullscreen"),
                                       QStringLiteral("--output"), file});
            return;
        }
    }
#endif
    const QImage full = display->grabWindow(0).toImage();
    if (full.isNull()) {
        finish({}, QStringLiteral("无法读取屏幕内容，请检查截图权限"));
        return;
    }
    const QImage cropped = ScreenshotRegionSelector::crop(full, screenGeo.size(), localRect);
    if (cropped.isNull()) {
        finish({}, QStringLiteral("框选区域无效"));
        return;
    }
    finish(cropped);
}

void ScreenshotCapture::captureDesktop(const QPoint &anchor)
{
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
        finish({}, QStringLiteral("当前显示环境不支持屏幕截图"));
        return;
    }
#ifdef Q_OS_MACOS
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (!QDir().mkpath(cache)) { finish({}, QStringLiteral("无法创建截图临时目录")); return; }
    temporary = std::make_unique<QTemporaryDir>(cache + QStringLiteral("/capture-XXXXXX"));
    if (!temporary->isValid()) { finish({}, QStringLiteral("无法创建截图临时目录")); return; }
    const QString file = temporary->filePath(QStringLiteral("capture.png"));
    QScreen *display = QGuiApplication::screenAt(anchor);
    if (!display) display = QGuiApplication::primaryScreen();
    const QRect regionBounds = display->geometry();
    const QList<QRect> macWindows = (options.mode == CaptureOptions::Region && studioMode)
        ? DesktopActions::macVisibleWindowRects()
        : QList<QRect>{};
    process = new QProcess(this);
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finish({}, QStringLiteral("无法启动系统截图工具"));
    });
    const auto singleDisplayFinished = connect(
        process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this, file, regionBounds, macWindows](int code, QProcess::ExitStatus exit) {
            if (!active) return;
            QImageReader reader(file);
            reader.setAutoTransform(true);
            const QImage image = reader.read();
            const QString error = process ? QString::fromLocal8Bit(process->readAllStandardError()).trimmed() : QString();
            if (!image.isNull()) {
                if (options.mode == CaptureOptions::Region) {
                    launchSelector(image, regionBounds, macWindows);
                } else {
                    finish(image);
                }
                return;
            }
            if (exit == QProcess::NormalExit && error.isEmpty()) { finish({}, {}, true); return; }
            Q_UNUSED(code);
            finish({}, QStringLiteral("系统截图失败，请检查屏幕录制权限") +
                           (error.isEmpty() ? QString() : QStringLiteral("：") + error.left(240)));
        });
    QStringList args{QStringLiteral("-x"), QStringLiteral("-t"), QStringLiteral("png")};
    if (options.mode == CaptureOptions::Region) {
        args << QStringLiteral("-D") << QString::number(qMax(0, QGuiApplication::screens().indexOf(display)) + 1);
    } else if (options.mode == CaptureOptions::Window) {
        args << QStringLiteral("-i") << QStringLiteral("-w") << QStringLiteral("-o");
    } else {
        if (options.includeCursor) args << QStringLiteral("-C");
        if (options.mode == CaptureOptions::CurrentScreen) {
            auto screens = QGuiApplication::screens();
            QScreen *screen = QGuiApplication::screenAt(anchor);
            args << QStringLiteral("-D") << QString::number(qMax(0, screens.indexOf(screen)) + 1);
        } else if (QGuiApplication::screens().size() > 1) {
            QStringList files;
            for (int i = 0; i < QGuiApplication::screens().size(); ++i)
                files << temporary->filePath(QStringLiteral("display-%1.png").arg(i));
            disconnect(singleDisplayFinished);
            connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                    [this, files](int, QProcess::ExitStatus) {
                QRect bounds;
                for (auto screen : QGuiApplication::screens()) bounds = bounds.united(screen->geometry());
                qreal scale = 1;
                for (auto screen : QGuiApplication::screens()) scale = qMax(scale, screen->devicePixelRatio());
                QImage result(QSize(qRound(bounds.width() * scale), qRound(bounds.height() * scale)),
                              QImage::Format_ARGB32);
                if (result.isNull()) { finish({}, QStringLiteral("桌面尺寸过大，无法创建截图")); return; }
                result.fill(Qt::transparent);
                QPainter p(&result);
                auto screens = QGuiApplication::screens();
                for (int i = 0; i < files.size(); ++i) {
                    QImage image(files[i]);
                    if (image.isNull()) { p.end(); finish({}, QStringLiteral("无法读取屏幕截图，请检查权限")); return; }
                    QRectF target(screens[i]->geometry());
                    target.translate(-bounds.topLeft());
                    target = QRectF(target.topLeft() * scale, target.size() * scale);
                    p.drawImage(target, image);
                }
                p.end();
                finish(result);
            });
            args << files;
            process->start(QStringLiteral("/usr/sbin/screencapture"), args);
            return;
        }
    }
    args << file;
    process->start(QStringLiteral("/usr/sbin/screencapture"), args);
    return;
#elif defined(Q_OS_LINUX)
    if (QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))) {
        const QString spectacle = QStandardPaths::findExecutable(QStringLiteral("spectacle"));
        if (!spectacle.isEmpty()) {
            temporary = std::make_unique<QTemporaryDir>();
            if (!temporary->isValid()) { finish({}, QStringLiteral("无法创建截图临时目录")); return; }
            const QString file = temporary->filePath(QStringLiteral("capture.png"));
            QScreen *display = QGuiApplication::screenAt(anchor);
            if (!display) display = QGuiApplication::primaryScreen();
            const QRect regionBounds = display ? display->geometry() : QRect(0, 0, 1280, 800);
            process = new QProcess(this);
            connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart) finish({}, QStringLiteral("无法启动系统截图服务"));
            });
            const bool useInlineStudio = (studioMode && options.mode == CaptureOptions::Region);
            connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                    [this, file, regionBounds, useInlineStudio](int code, QProcess::ExitStatus exit) {
                QImageReader reader(file);
                reader.setAutoTransform(true);
                const QImage image = reader.read();
                if (!image.isNull()) {
                    if (useInlineStudio) {
                        launchSelector(image, regionBounds, {});
                    } else {
                        finish(image);
                    }
                    return;
                }
                if (exit == QProcess::NormalExit && (code == 0 || code == 1)) { finish({}, {}, true); return; }
                finish({}, QStringLiteral("系统截图失败，请检查桌面截图服务"));
            });
            const QString modes[] = {
                useInlineStudio ? QStringLiteral("--current") : QStringLiteral("--region"),
                QStringLiteral("--windowundercursor"),
                QStringLiteral("--current"),
                QStringLiteral("--fullscreen")
            };
            QStringList args{QStringLiteral("--new-instance"), QStringLiteral("--background"),
                             QStringLiteral("--nonotify"), QStringLiteral("--output"), file,
                             modes[options.mode]};
            if (options.mode == CaptureOptions::Window) args << QStringLiteral("--onclick");
            if (options.includeCursor) args << QStringLiteral("--pointer");
            process->start(spectacle, args);
            return;
        }
        auto bus = QDBusConnection::sessionBus();
        if (!bus.isConnected()) { finish({}, QStringLiteral("无法连接桌面截图服务")); return; }
        const QString token = QStringLiteral("floatingball_") + QUuid::createUuid().toString(QUuid::Id128);
        QString sender = bus.baseService().mid(1);
        sender.replace('.', '_');
        portalPath = QStringLiteral("/org/freedesktop/portal/desktop/request/") + sender + '/' + token;
        if (!bus.connect(QStringLiteral("org.freedesktop.portal.Desktop"), portalPath,
                         QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
                         this, SLOT(portalResponse(uint, QVariantMap)))) {
            finish({}, QStringLiteral("无法接收桌面截图结果"));
            return;
        }
        QVariantMap portalOptions{{QStringLiteral("handle_token"), token},
                                  {QStringLiteral("interactive"),
                                   options.mode == CaptureOptions::Region || options.mode == CaptureOptions::Window},
                                  {QStringLiteral("modal"), false}};
        QDBusInterface portal(QStringLiteral("org.freedesktop.portal.Desktop"),
                              QStringLiteral("/org/freedesktop/portal/desktop"),
                              QStringLiteral("org.freedesktop.portal.Screenshot"), bus);
        portal.setTimeout(3000);
        const uint target = options.mode == CaptureOptions::Region ? 4 : options.mode == CaptureOptions::Window ? 2 : 1;
        if (portal.property("version").toUInt() >= 3 && (portal.property("AvailableTargets").toUInt() & target))
            portalOptions.insert(QStringLiteral("target"), target);
        auto message = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.portal.Desktop"),
            QStringLiteral("/org/freedesktop/portal/desktop"),
            QStringLiteral("org.freedesktop.portal.Screenshot"),
            QStringLiteral("Screenshot"));
        message.setArguments({QString(), portalOptions});
        auto watcher = new QDBusPendingCallWatcher(bus.asyncCall(message, 5000), this);
        const quint64 run = generation;
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, run](QDBusPendingCallWatcher *call) {
            QDBusPendingReply<QDBusObjectPath> reply = *call;
            call->deleteLater();
            if (!active || generation != run) return;
            if (reply.isError()) {
                finish({}, QStringLiteral("桌面截图服务不可用：") + reply.error().message());
                return;
            }
            const QString path = reply.value().path();
            if (path != portalPath) {
                auto bus = QDBusConnection::sessionBus();
                bus.disconnect(QStringLiteral("org.freedesktop.portal.Desktop"), portalPath,
                               QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
                               this, SLOT(portalResponse(uint, QVariantMap)));
                portalPath = path;
                bus.connect(QStringLiteral("org.freedesktop.portal.Desktop"), portalPath,
                            QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
                            this, SLOT(portalResponse(uint, QVariantMap)));
            }
        });
        return;
    }
#endif
    QList<QPair<QScreen *, QImage>> frames;
    for (QScreen *screen : QGuiApplication::screens()) {
        const QImage image = screen->grabWindow(0).toImage();
        if (image.isNull()) { finish({}, QStringLiteral("无法读取屏幕内容，请检查截图权限")); return; }
        frames.append({screen, image});
    }
    if (frames.isEmpty()) { finish({}, QStringLiteral("没有可截图的显示器")); return; }
    QRect bounds;
    qreal scale = 1;
    for (const auto &frame : frames) {
        bounds = bounds.united(frame.first->geometry());
        scale = qMax(scale, frame.first->devicePixelRatio());
    }
    QImage desktop(QSize(qRound(bounds.width() * scale), qRound(bounds.height() * scale)), QImage::Format_ARGB32);
    if (desktop.isNull()) { finish({}, QStringLiteral("桌面尺寸过大，无法创建截图")); return; }
    desktop.fill(Qt::transparent);
    {
        QPainter p(&desktop);
        for (const auto &frame : frames) {
            QRectF r(frame.first->geometry());
            r.translate(-bounds.topLeft());
            p.drawImage(QRectF(r.topLeft() * scale, r.size() * scale), frame.second);
        }
    }
#ifdef Q_OS_WIN
    if (options.includeCursor) paintCursor(desktop, bounds, scale);
#endif
    if (options.mode == CaptureOptions::AllScreens) { finish(desktop); return; }
    if (options.mode == CaptureOptions::CurrentScreen) {
        for (const auto &frame : frames) {
            if (frame.first->geometry().contains(anchor)) {
                QImage image = frame.second;
                image.setDevicePixelRatio(1);
#ifdef Q_OS_WIN
                if (options.includeCursor) paintCursor(image, frame.first->geometry(), frame.first->devicePixelRatio());
#endif
                finish(image);
                return;
            }
        }
        finish(frames.first().second);
        return;
    }
    QList<QRect> windows;
#ifdef Q_OS_WIN
    if (options.mode == CaptureOptions::Window) {
        EnumWindows([](HWND handle, LPARAM param) -> BOOL {
            DWORD pid = 0;
            GetWindowThreadProcessId(handle, &pid);
            if (!IsWindowVisible(handle) || IsIconic(handle) || pid == GetCurrentProcessId()) return TRUE;
            RECT rect;
            if (!GetWindowRect(handle, &rect) || rect.right - rect.left < 30 || rect.bottom - rect.top < 30) return TRUE;
            reinterpret_cast<QList<QRect> *>(param)->append(logicalWindowRect(handle, rect));
            return TRUE;
        }, reinterpret_cast<LPARAM>(&windows));
        if (windows.isEmpty()) { finish({}, QStringLiteral("没有可截取的窗口")); return; }
    }
#else
    if (options.mode == CaptureOptions::Window) {
        finish({}, QStringLiteral("当前桌面请使用区域模式选择窗口"));
        return;
    }
#endif
    launchSelector(desktop, bounds, windows);
}

#ifdef Q_OS_LINUX
void ScreenshotCapture::portalResponse(uint response, const QVariantMap &results)
{
    if (!active) return;
    if (response == 1) { finish({}, {}, true); return; }
    if (response != 0) { finish({}, QStringLiteral("系统未能完成截图，请检查桌面截图权限")); return; }
    const QUrl uri(results.value(QStringLiteral("uri")).toString());
    if (!uri.isLocalFile()) { finish({}, QStringLiteral("桌面返回了无法读取的截图路径")); return; }
    QImageReader reader(uri.toLocalFile());
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) { finish({}, QStringLiteral("无法读取系统截图：") + reader.errorString()); return; }
    finish(image);
}
#endif

void ScreenshotCapture::finish(const QImage &image, const QString &error, bool cancelled)
{
    if (!active) return;
    active = false;
    cleanUp();
    emit finished(image, error, cancelled);
}

void ScreenshotCapture::cleanUp()
{
    for (const auto &selector : selectors) {
        if (selector) {
            selector->hide();
            selector->deleteLater();
        }
    }
    selectors.clear();
    if (process) {
        process->disconnect(this);
        if (process->state() != QProcess::NotRunning) {
            process->terminate();
            if (!process->waitForFinished(400)) {
                process->kill();
                process->waitForFinished(400);
            }
        }
        process->deleteLater();
        process = nullptr;
    }
    temporary.reset();
#ifdef Q_OS_LINUX
    if (!portalPath.isEmpty()) {
        auto bus = QDBusConnection::sessionBus();
        bus.disconnect(QStringLiteral("org.freedesktop.portal.Desktop"), portalPath,
                       QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
                       this, SLOT(portalResponse(uint, QVariantMap)));
        bus.asyncCall(QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.portal.Desktop"), portalPath,
            QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Close")));
        portalPath.clear();
    }
#endif
}
