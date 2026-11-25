#pragma once

#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QtMath>

namespace FloatingIcons {

enum Icon {
    Orb,
    Crosshair,
    Back,
    Forward,
    Overview,
    Clipboard,
    Deck,
    Screenshot,
    Capture,
    Terminal,
    Browser,
    Folder,
    Copy,
    Paste,
    Pin,
    Close,
    Minimize,
    Maximize,
    TabNext,
    Pen,
    Undo,
    Save,
    Import,
    Path,
    Volume,
    VolumeMute,
    Keyboard,
    Globe,
    Record,
    Settings,
    Search,
    Desktop,
    Downloads,
    Trash,
    TrayHub,
    SystemHub,
    WindowsHub,
    ToolsHub,
    Launcher,
    Wifi,
    Bluetooth,
    Bell,
    Moon,
    Bolt,
    Lock,
    Power,
    Proxy,
    Sun,
    Music,
    MediaPrev,
    MediaPlay,
    MediaPause,
    MediaNext,
    ChevronRight,
    ChevronLeft,
    Hotspot
};

inline void draw(QPainter &p, Icon icon, const QRectF &rect, const QColor &color)
{
    if (rect.isEmpty()) return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(rect.topLeft());
    p.scale(rect.width() / 24.0, rect.height() / 24.0);
    p.setPen(QPen(color, 1.85, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);

    QPainterPath path;
    switch (icon) {
    case Orb: {
        p.drawEllipse(QPointF(12, 12), 8.2, 8.2);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QPointF(12, 12), 3.4, 3.4);
        break;
    }
    case Crosshair: {
        p.drawEllipse(QPointF(12, 12), 6.8, 6.8);
        path.moveTo(12, 2.4); path.lineTo(12, 6.4);
        path.moveTo(12, 17.6); path.lineTo(12, 21.6);
        path.moveTo(2.4, 12); path.lineTo(6.4, 12);
        path.moveTo(17.6, 12); path.lineTo(21.6, 12);
        p.drawPath(path);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QPointF(12, 12), 2.0, 2.0);
        p.restore();
        return;
    }
    case Back: {
        path.moveTo(14.5, 5.5);
        path.lineTo(8.0, 12.0);
        path.lineTo(14.5, 18.5);
        path.moveTo(8.5, 12.0);
        path.lineTo(19.0, 12.0);
        break;
    }
    case Forward: {
        path.moveTo(9.5, 5.5);
        path.lineTo(16.0, 12.0);
        path.lineTo(9.5, 18.5);
        path.moveTo(5.0, 12.0);
        path.lineTo(15.5, 12.0);
        break;
    }
    case Overview: {
        p.drawRoundedRect(QRectF(3.5, 4.0, 7.2, 6.8), 1.8, 1.8);
        p.drawRoundedRect(QRectF(13.3, 4.0, 7.2, 6.8), 1.8, 1.8);
        p.drawRoundedRect(QRectF(3.5, 13.2, 7.2, 6.8), 1.8, 1.8);
        p.drawRoundedRect(QRectF(13.3, 13.2, 7.2, 6.8), 1.8, 1.8);
        break;
    }
    case Desktop: {
        p.drawRoundedRect(QRectF(3.0, 4.5, 18.0, 12.5), 2.2, 2.2);
        path.moveTo(8.0, 20.0); path.lineTo(16.0, 20.0);
        path.moveTo(12.0, 17.0); path.lineTo(12.0, 20.0);
        break;
    }
    case Clipboard: {
        p.drawRoundedRect(QRectF(5.5, 5.5, 13.0, 15.0), 2.2, 2.2);
        p.drawRoundedRect(QRectF(9.0, 3.2, 6.0, 3.8), 1.3, 1.3);
        path.moveTo(8.8, 11.5); path.lineTo(15.2, 11.5);
        path.moveTo(8.8, 15.2); path.lineTo(13.5, 15.2);
        break;
    }
    case Deck: {
        path.moveTo(4.0, 7.0); path.lineTo(20.0, 7.0);
        path.moveTo(4.0, 12.0); path.lineTo(20.0, 12.0);
        path.moveTo(4.0, 17.0); path.lineTo(20.0, 17.0);
        p.drawPath(path);
        p.setBrush(QColor(30, 34, 42));
        p.drawEllipse(QPointF(9.0, 7.0), 2.2, 2.2);
        p.drawEllipse(QPointF(15.5, 12.0), 2.2, 2.2);
        p.drawEllipse(QPointF(10.5, 17.0), 2.2, 2.2);
        p.restore();
        return;
    }
    case Capture: {
        path.moveTo(3, 9); path.lineTo(3, 3); path.lineTo(9, 3);
        path.moveTo(15, 3); path.lineTo(21, 3); path.lineTo(21, 9);
        path.moveTo(21, 15); path.lineTo(21, 21); path.lineTo(15, 21);
        path.moveTo(9, 21); path.lineTo(3, 21); path.lineTo(3, 15);
        path.moveTo(8.5, 12); path.lineTo(15.5, 12);
        path.moveTo(12, 8.5); path.lineTo(12, 15.5);
        break;
    }
    case Screenshot: {
        p.drawRoundedRect(QRectF(3.5, 6.5, 17.0, 13.5), 2.2, 2.2);
        path.moveTo(7.5, 4.0); path.lineTo(16.5, 4.0);
        p.drawEllipse(QPointF(8.8, 10.8), 1.2, 1.2);
        path.moveTo(5.8, 17.2); path.lineTo(10.8, 12.8); path.lineTo(14.0, 15.8);
        path.lineTo(16.8, 13.2); path.lineTo(19.5, 16.5);
        break;
    }
    case Terminal: {
        p.drawRoundedRect(QRectF(3, 4, 18, 16), 2.2, 2.2);
        path.moveTo(7, 9); path.lineTo(10.5, 12); path.lineTo(7, 15);
        path.moveTo(12.5, 15); path.lineTo(17, 15);
        break;
    }
    case Browser:
    case Globe: {
        p.drawEllipse(QRectF(3.2, 3.2, 17.6, 17.6));
        p.drawEllipse(QRectF(8.0, 3.2, 8.0, 17.6));
        path.moveTo(3.5, 12.0); path.lineTo(20.5, 12.0);
        break;
    }
    case Folder: {
        path.moveTo(3, 8); path.lineTo(3, 5.2); path.lineTo(9.2, 5.2);
        path.lineTo(11.8, 8); path.lineTo(21, 8);
        path.lineTo(21, 19); path.lineTo(3, 19); path.closeSubpath();
        break;
    }
    case Copy: {
        p.drawRoundedRect(QRectF(8.2, 8.2, 11.8, 11.8), 2.0, 2.0);
        path.moveTo(15.2, 5.2); path.lineTo(15.2, 3.8);
        path.lineTo(4.0, 3.8); path.lineTo(4.0, 15.2); path.lineTo(5.5, 15.2);
        break;
    }
    case Paste: {
        p.drawRoundedRect(QRectF(5.2, 5.5, 13.6, 15.2), 2.0, 2.0);
        p.drawRoundedRect(QRectF(9.0, 3.2, 6.0, 3.8), 1.2, 1.2);
        path.moveTo(12.0, 10.2); path.lineTo(12.0, 16.8);
        path.moveTo(9.2, 13.8); path.lineTo(12.0, 16.8); path.lineTo(14.8, 13.8);
        break;
    }
    case Pin: {
        path.moveTo(8, 3.5); path.lineTo(16, 3.5); path.lineTo(14.8, 9.2);
        path.lineTo(18, 13); path.lineTo(6, 13); path.lineTo(9.2, 9.2); path.closeSubpath();
        path.moveTo(12, 13); path.lineTo(12, 20.5);
        break;
    }
    case Close: {
        path.moveTo(6.8, 6.8); path.lineTo(17.2, 17.2);
        path.moveTo(17.2, 6.8); path.lineTo(6.8, 17.2);
        break;
    }
    case Minimize: {
        path.moveTo(5.5, 12.5); path.lineTo(18.5, 12.5);
        break;
    }
    case Maximize: {
        path.moveTo(14.5, 4.5); path.lineTo(19.5, 4.5); path.lineTo(19.5, 9.5);
        path.moveTo(19.2, 4.8); path.lineTo(13.5, 10.5);
        path.moveTo(9.5, 19.5); path.lineTo(4.5, 19.5); path.lineTo(4.5, 14.5);
        path.moveTo(4.8, 19.2); path.lineTo(10.5, 13.5);
        break;
    }
    case TabNext: {
        p.drawRoundedRect(QRectF(3.5, 5.5, 17.0, 13.5), 2.2, 2.2);
        path.moveTo(3.5, 9.5); path.lineTo(20.5, 9.5);
        path.moveTo(9.5, 14.2); path.lineTo(15.2, 14.2);
        path.moveTo(13.0, 11.8); path.lineTo(15.5, 14.2); path.lineTo(13.0, 16.6);
        break;
    }
    case Pen: {
        path.moveTo(4, 16.2); path.lineTo(16, 4.2); path.lineTo(19.8, 8);
        path.lineTo(7.8, 20); path.lineTo(3.2, 20.8); path.closeSubpath();
        path.moveTo(13.2, 7); path.lineTo(17, 10.8);
        break;
    }
    case Undo: {
        path.moveTo(8, 5.5); path.lineTo(3.5, 10); path.lineTo(8, 14.5);
        path.moveTo(4, 10); path.lineTo(14, 10);
        path.cubicTo(21.5, 10, 21.5, 19.5, 14, 19.5);
        break;
    }
    case Save: {
        path.moveTo(12, 3.5); path.lineTo(12, 15);
        path.moveTo(7.5, 10.5); path.lineTo(12, 15); path.lineTo(16.5, 10.5);
        path.moveTo(4.2, 16.2); path.lineTo(4.2, 20.2);
        path.lineTo(19.8, 20.2); path.lineTo(19.8, 16.2);
        break;
    }
    case Import: {
        path.moveTo(12, 3.5); path.lineTo(12, 13);
        path.moveTo(8.2, 9.2); path.lineTo(12, 13); path.lineTo(15.8, 9.2);
        path.moveTo(6, 10.5); path.lineTo(3.2, 18); path.lineTo(3.2, 20.5);
        path.lineTo(20.8, 20.5); path.lineTo(20.8, 18); path.lineTo(18, 10.5);
        break;
    }
    case Path: {
        path.moveTo(10, 15); path.lineTo(8, 17); path.cubicTo(3, 22, -1, 15, 4, 11); path.lineTo(7, 8);
        path.moveTo(14, 9); path.lineTo(16, 7); path.cubicTo(21, 2, 25, 9, 20, 13); path.lineTo(17, 16);
        path.moveTo(8.2, 15.8); path.lineTo(15.8, 8.2);
        break;
    }
    case Volume: {
        path.moveTo(4.5, 9.5); path.lineTo(8.5, 9.5); path.lineTo(13.2, 5.5);
        path.lineTo(13.2, 18.5); path.lineTo(8.5, 14.5); path.lineTo(4.5, 14.5); path.closeSubpath();
        path.moveTo(16.2, 9.0); path.cubicTo(18.2, 10.5, 18.2, 13.5, 16.2, 15.0);
        path.moveTo(18.5, 6.8); path.cubicTo(21.5, 9.5, 21.5, 14.5, 18.5, 17.2);
        break;
    }
    case VolumeMute: {
        path.moveTo(4.5, 9.5); path.lineTo(8.5, 9.5); path.lineTo(13.2, 5.5);
        path.lineTo(13.2, 18.5); path.lineTo(8.5, 14.5); path.lineTo(4.5, 14.5); path.closeSubpath();
        path.moveTo(16.2, 9.5); path.lineTo(20.5, 14.5);
        path.moveTo(20.5, 9.5); path.lineTo(16.2, 14.5);
        break;
    }
    case Keyboard: {
        p.drawRoundedRect(QRectF(2.8, 5.8, 18.4, 12.4), 2.2, 2.2);
        path.moveTo(6.5, 9.5); path.lineTo(7.5, 9.5);
        path.moveTo(11.5, 9.5); path.lineTo(12.5, 9.5);
        path.moveTo(16.5, 9.5); path.lineTo(17.5, 9.5);
        path.moveTo(6.5, 12.5); path.lineTo(7.5, 12.5);
        path.moveTo(11.5, 12.5); path.lineTo(12.5, 12.5);
        path.moveTo(16.5, 12.5); path.lineTo(17.5, 12.5);
        path.moveTo(8.0, 15.4); path.lineTo(16.0, 15.4);
        break;
    }
    case Record: {
        p.drawEllipse(QPointF(12, 12), 8.2, 8.2);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#ff5c77"));
        p.drawEllipse(QPointF(12, 12), 4.0, 4.0);
        p.restore();
        return;
    }
    case Settings: {
        p.drawEllipse(QPointF(12, 12), 3.3, 3.3);
        for (int i = 0; i < 8; ++i) {
            const qreal a = i * M_PI / 4.0;
            path.moveTo(12 + qCos(a) * 6.0, 12 + qSin(a) * 6.0);
            path.lineTo(12 + qCos(a) * 8.6, 12 + qSin(a) * 8.6);
        }
        break;
    }
    case Search: {
        p.drawEllipse(QPointF(10.5, 10.5), 6.2, 6.2);
        path.moveTo(15.2, 15.2); path.lineTo(20.2, 20.2);
        break;
    }
    case Downloads: {
        path.moveTo(12.0, 4.0); path.lineTo(12.0, 15.0);
        path.moveTo(7.5, 10.8); path.lineTo(12.0, 15.4); path.lineTo(16.5, 10.8);
        path.moveTo(4.5, 16.5); path.lineTo(4.5, 19.2); path.lineTo(19.5, 19.2); path.lineTo(19.5, 16.5);
        break;
    }
    case Trash: {
        path.moveTo(4.5, 6.8); path.lineTo(19.5, 6.8);
        path.moveTo(9.0, 6.8); path.lineTo(9.5, 4.2); path.lineTo(14.5, 4.2); path.lineTo(15.0, 6.8);
        path.moveTo(6.5, 6.8); path.lineTo(7.5, 19.5); path.lineTo(16.5, 19.5); path.lineTo(17.5, 6.8);
        path.moveTo(10.2, 10.5); path.lineTo(10.5, 16.2);
        path.moveTo(13.8, 10.5); path.lineTo(13.5, 16.2);
        break;
    }
    case TrayHub: {
        // Tray tray/drawer with 3 app dots inside
        p.drawRoundedRect(QRectF(3.5, 5.0, 17.0, 14.0), 3.0, 3.0);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QPointF(8.0, 12.0), 1.6, 1.6);
        p.drawEllipse(QPointF(12.0, 12.0), 1.6, 1.6);
        p.drawEllipse(QPointF(16.0, 12.0), 1.6, 1.6);
        p.restore();
        return;
    }
    case SystemHub: {
        // Control center dual toggles (iOS/KDE control center style)
        p.drawRoundedRect(QRectF(3.5, 5.2, 17.0, 5.6), 2.8, 2.8);
        p.drawRoundedRect(QRectF(3.5, 13.2, 17.0, 5.6), 2.8, 2.8);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QPointF(17.5, 8.0), 1.8, 1.8);
        p.drawEllipse(QPointF(6.5, 16.0), 1.8, 1.8);
        p.restore();
        return;
    }
    case WindowsHub: {
        // Overlapping desktop windows stack
        p.drawRoundedRect(QRectF(3.8, 7.2, 12.6, 11.4), 2.2, 2.2);
        path.moveTo(8.2, 7.2); path.lineTo(8.2, 5.4);
        path.quadTo(8.2, 4.2, 9.6, 4.2);
        path.lineTo(18.6, 4.2);
        path.quadTo(20.0, 4.2, 20.0, 5.6);
        path.lineTo(20.0, 14.0);
        path.quadTo(20.0, 15.2, 18.6, 15.2);
        path.lineTo(16.4, 15.2);
        break;
    }
    case ToolsHub: {
        // Minimalist touch toolspark / utility cross
        p.drawEllipse(QPointF(12, 12), 4.2, 4.2);
        path.moveTo(12, 3.4); path.lineTo(12, 6.2);
        path.moveTo(12, 17.8); path.lineTo(12, 20.6);
        path.moveTo(3.4, 12); path.lineTo(6.2, 12);
        path.moveTo(17.8, 12); path.lineTo(20.6, 12);
        path.moveTo(6.0, 6.0); path.lineTo(7.8, 7.8);
        path.moveTo(16.2, 16.2); path.lineTo(18.0, 18.0);
        path.moveTo(18.0, 6.0); path.lineTo(16.2, 7.8);
        path.moveTo(7.8, 16.2); path.lineTo(6.0, 18.0);
        break;
    }
    case Launcher: {
        // Classic 3x3 app-grid start menu icon
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                p.drawRoundedRect(QRectF(4.2 + col * 5.8, 4.2 + row * 5.8, 4.0, 4.0), 1.1, 1.1);
            }
        }
        p.restore();
        return;
    }
    case Wifi: {
        p.drawArc(QRectF(3.0, 5.0, 18.0, 18.0), 45 * 16, 90 * 16);
        p.drawArc(QRectF(6.5, 8.5, 11.0, 11.0), 45 * 16, 90 * 16);
        p.drawArc(QRectF(9.5, 11.5, 5.0, 5.0), 45 * 16, 90 * 16);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QPointF(12.0, 18.2), 1.5, 1.5);
        p.restore();
        return;
    }
    case Bluetooth: {
        path.moveTo(7.5, 7.5); path.lineTo(16.5, 16.0); path.lineTo(12.0, 20.0);
        path.lineTo(12.0, 4.0); path.lineTo(16.5, 8.0); path.lineTo(7.5, 16.5);
        break;
    }
    case Bell: {
        path.moveTo(6.0, 15.5);
        path.cubicTo(6.0, 10.5, 7.2, 5.5, 12.0, 5.5);
        path.cubicTo(16.8, 5.5, 18.0, 10.5, 18.0, 15.5);
        path.lineTo(19.5, 17.2); path.lineTo(4.5, 17.2); path.closeSubpath();
        path.moveTo(10.0, 19.5); path.quadTo(12.0, 21.0, 14.0, 19.5);
        break;
    }
    case Moon: {
        path.moveTo(18.5, 14.5);
        path.cubicTo(16.0, 15.2, 13.0, 14.2, 11.5, 12.0);
        path.cubicTo(10.0, 9.8, 10.2, 6.8, 12.0, 4.5);
        path.cubicTo(7.5, 4.8, 4.5, 8.6, 5.0, 13.2);
        path.cubicTo(5.6, 17.8, 9.8, 20.5, 14.5, 19.5);
        path.cubicTo(16.4, 19.1, 17.8, 17.0, 18.5, 14.5);
        break;
    }
    case Bolt: {
        path.moveTo(13.5, 3.5); path.lineTo(6.2, 13.0); path.lineTo(11.8, 13.0);
        path.lineTo(10.5, 20.5); path.lineTo(17.8, 11.0); path.lineTo(12.2, 11.0);
        path.closeSubpath();
        break;
    }
    case Lock: {
        p.drawRoundedRect(QRectF(5.5, 10.8, 13.0, 9.2), 2.2, 2.2);
        p.drawArc(QRectF(8.2, 4.5, 7.6, 9.0), 0, 180 * 16);
        path.moveTo(8.2, 9.0); path.lineTo(8.2, 10.8);
        path.moveTo(15.8, 9.0); path.lineTo(15.8, 10.8);
        break;
    }
    case Power: {
        p.drawArc(QRectF(5.0, 5.5, 14.0, 14.0), 128 * 16, 284 * 16);
        path.moveTo(12.0, 3.5); path.lineTo(12.0, 11.5);
        break;
    }
    case Proxy: {
        // Minimalist proxy routing diamond with dual traffic arcs/nodes
        path.moveTo(12.0, 3.2); path.lineTo(20.6, 12.0);
        path.lineTo(12.0, 20.8); path.lineTo(3.4, 12.0);
        path.closeSubpath();
        path.moveTo(7.4, 12.0); path.lineTo(16.6, 12.0);
        path.moveTo(12.0, 7.6); path.lineTo(12.0, 16.4);
        p.drawPath(path);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QPointF(12.0, 12.0), 2.3, 2.3);
        p.restore();
        return;
    }
    case Sun: {
        p.drawEllipse(QPointF(12.0, 12.0), 4.0, 4.0);
        for (int i = 0; i < 8; ++i) {
            const qreal a = i * M_PI / 4.0;
            path.moveTo(12.0 + qCos(a) * 6.4, 12.0 + qSin(a) * 6.4);
            path.lineTo(12.0 + qCos(a) * 8.9, 12.0 + qSin(a) * 8.9);
        }
        break;
    }
    case Music: {
        path.moveTo(9.5, 16.8); path.lineTo(9.5, 6.2); path.lineTo(18.5, 4.5); path.lineTo(18.5, 14.8);
        p.drawPath(path);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QPointF(7.3, 17.0), 2.5, 2.0);
        p.drawEllipse(QPointF(16.3, 15.0), 2.5, 2.0);
        p.restore();
        return;
    }
    case MediaPrev: {
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRoundedRect(QRectF(5.0, 6.0, 2.4, 12.0), 1.0, 1.0);
        QPolygonF tri;
        tri << QPointF(18.2, 6.0) << QPointF(18.2, 18.0) << QPointF(8.6, 12.0);
        p.drawPolygon(tri);
        p.restore();
        return;
    }
    case MediaPlay: {
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        QPolygonF tri;
        tri << QPointF(7.5, 5.4) << QPointF(7.5, 18.6) << QPointF(19.0, 12.0);
        p.drawPolygon(tri);
        p.restore();
        return;
    }
    case MediaPause: {
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRoundedRect(QRectF(6.5, 5.5, 3.8, 13.0), 1.4, 1.4);
        p.drawRoundedRect(QRectF(13.7, 5.5, 3.8, 13.0), 1.4, 1.4);
        p.restore();
        return;
    }
    case MediaNext: {
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        QPolygonF tri;
        tri << QPointF(5.8, 6.0) << QPointF(5.8, 18.0) << QPointF(15.4, 12.0);
        p.drawPolygon(tri);
        p.drawRoundedRect(QRectF(16.6, 6.0, 2.4, 12.0), 1.0, 1.0);
        p.restore();
        return;
    }
    case ChevronRight: {
        path.moveTo(9.2, 6.0); path.lineTo(15.2, 12.0); path.lineTo(9.2, 18.0);
        break;
    }
    case ChevronLeft: {
        path.moveTo(14.8, 6.0); path.lineTo(8.8, 12.0); path.lineTo(14.8, 18.0);
        break;
    }
    case Hotspot: {
        // Apple Personal Hotspot twin interlocking rings
        p.drawEllipse(QPointF(9.0, 12.0), 5.0, 5.0);
        p.drawEllipse(QPointF(15.0, 12.0), 5.0, 5.0);
        break;
    }
    }

    p.drawPath(path);
    p.restore();
}

inline QIcon make(Icon icon, const QColor &color = QColor("#dce3ed"), int logicalSize = 20)
{
    QPixmap pixmap(logicalSize * 2, logicalSize * 2);
    pixmap.setDevicePixelRatio(2);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    draw(p, icon, QRectF(0, 0, logicalSize, logicalSize), color);
    return QIcon(pixmap);
}

} // namespace FloatingIcons
