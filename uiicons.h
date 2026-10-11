#pragma once

#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace FloatingIcons {
enum Icon { Orb, Screenshot, Terminal, Browser, Folder, Copy, Paste, Pin, Close, Pen, Undo, Save, Import, Path, Capture };

inline void draw(QPainter &p, Icon icon, const QRectF &rect, const QColor &color) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(rect.topLeft());
    p.scale(rect.width() / 24.0, rect.height() / 24.0);
    p.setPen(QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    QPainterPath path;
    switch (icon) {
    case Capture:
        path.moveTo(3,9);path.lineTo(3,3);path.lineTo(9,3);
        path.moveTo(15,3);path.lineTo(21,3);path.lineTo(21,9);
        path.moveTo(21,15);path.lineTo(21,21);path.lineTo(15,21);
        path.moveTo(9,21);path.lineTo(3,21);path.lineTo(3,15);
        path.moveTo(8,12);path.lineTo(16,12);path.moveTo(12,8);path.lineTo(12,16);
        break;
    case Orb:
        p.setPen(Qt::NoPen); p.setBrush(color);
        p.drawEllipse(QPointF(7, 7), 2.4, 2.4);
        p.drawEllipse(QPointF(7, 17), 2.4, 2.4);
        p.drawEllipse(QPointF(17, 17), 2.4, 2.4);
        p.setBrush(QColor("#a995eb")); p.drawEllipse(QPointF(17, 7), 3.6, 3.6);
        break;
    case Screenshot:
        p.drawRoundedRect(QRectF(4, 7, 16, 13), 2, 2);
        path.moveTo(7, 4); path.lineTo(17, 4); path.lineTo(17, 7);
        p.drawEllipse(QPointF(9, 11), 1, 1);
        path.moveTo(6, 17); path.lineTo(11, 13); path.lineTo(14, 16); path.lineTo(17, 13); path.lineTo(20, 16);
        break;
    case Terminal:
        p.drawRoundedRect(QRectF(3, 4, 18, 16), 2, 2);
        path.moveTo(7, 9); path.lineTo(10, 12); path.lineTo(7, 15);
        path.moveTo(13, 15); path.lineTo(17, 15);
        break;
    case Browser:
        p.drawEllipse(QRectF(3, 3, 18, 18)); p.drawEllipse(QRectF(8, 3, 8, 18));
        path.moveTo(3, 12); path.lineTo(21, 12);
        break;
    case Folder:
        path.moveTo(3, 8); path.lineTo(3, 5); path.lineTo(9, 5); path.lineTo(12, 8); path.lineTo(21, 8);
        path.lineTo(21, 19); path.lineTo(3, 19); path.closeSubpath();
        break;
    case Copy:
        p.drawRoundedRect(QRectF(8, 8, 12, 12), 2, 2);
        path.moveTo(15, 5); path.lineTo(15, 3); path.lineTo(4, 3); path.lineTo(4, 15); path.lineTo(5, 15);
        break;
    case Paste:
        p.drawRoundedRect(QRectF(5, 5, 14, 16), 2, 2);
        p.setBrush(QColor("#faf9fc")); p.drawRoundedRect(QRectF(9, 3, 6, 4), 1, 1);
        break;
    case Pin:
        path.moveTo(8, 3); path.lineTo(16, 3); path.lineTo(15, 9); path.lineTo(18, 13);
        path.lineTo(6, 13); path.lineTo(9, 9); path.closeSubpath();
        path.moveTo(12, 13); path.lineTo(12, 21);
        break;
    case Close:
        path.moveTo(7, 7); path.lineTo(17, 17); path.moveTo(17, 7); path.lineTo(7, 17);
        break;
    case Pen:
        path.moveTo(4, 16); path.lineTo(16, 4); path.lineTo(20, 8); path.lineTo(8, 20);
        path.lineTo(3, 21); path.closeSubpath(); path.moveTo(13, 7); path.lineTo(17, 11);
        break;
    case Undo:
        path.moveTo(8, 5); path.lineTo(3, 10); path.lineTo(8, 15);
        path.moveTo(3, 10); path.lineTo(14, 10); path.cubicTo(22, 10, 22, 20, 14, 20);
        break;
    case Save:
        path.moveTo(12, 3); path.lineTo(12, 15); path.moveTo(7, 10); path.lineTo(12, 15); path.lineTo(17, 10);
        path.moveTo(4, 16); path.lineTo(4, 21); path.lineTo(20, 21); path.lineTo(20, 16);
        break;
    case Import:
        path.moveTo(12, 3); path.lineTo(12, 13); path.moveTo(8, 9); path.lineTo(12, 13); path.lineTo(16, 9);
        path.moveTo(6, 10); path.lineTo(3, 18); path.lineTo(3, 21); path.lineTo(21, 21);
        path.lineTo(21, 18); path.lineTo(18, 10); path.moveTo(3, 18); path.lineTo(8, 18);
        path.lineTo(9, 16); path.lineTo(15, 16); path.lineTo(16, 18); path.lineTo(21, 18);
        break;
    case Path:
        path.moveTo(10, 15); path.lineTo(8, 17); path.cubicTo(3, 22, -1, 15, 4, 11); path.lineTo(7, 8);
        path.moveTo(14, 9); path.lineTo(16, 7); path.cubicTo(21, 2, 25, 9, 20, 13); path.lineTo(17, 16);
        path.moveTo(8, 16); path.lineTo(16, 8);
        break;
    }
    p.drawPath(path);
    p.restore();
}

inline QIcon make(Icon icon, const QColor &color = QColor("#605b6d")) {
    QPixmap pixmap(40, 40); pixmap.setDevicePixelRatio(2); pixmap.fill(Qt::transparent);
    QPainter p(&pixmap); draw(p, icon, QRectF(0, 0, 20, 20), color);
    return QIcon(pixmap);
}
}
