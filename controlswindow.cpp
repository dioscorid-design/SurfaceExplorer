#include "controlswindow.h"
#include "uistylemanager.h"

#include <QFile>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QRadialGradient>
#include <QRegularExpression>
#include <QScreen>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QtMath>
#include <cmath>
#include <functional>
#include <map>

// ==========================================================
// LE FIGURE
// ==========================================================
// Disegnate su una griglia di 64 x 64 unita', scalata al lato chiesto. I
// colori sono quelli del tema scuro dell'app (UiStyleManager): grigio chiaro
// per le forme, verde acqua (lo stesso dei titoli della guida) per i moti,
// ambra per cio' che si preme o si tocca.
namespace {

const QColor kLine(208, 208, 214);
const QColor kBody(58, 58, 66);
const QColor kDim(112, 112, 122);
const QColor kAccent(78, 201, 176);
const QColor kHot(240, 176, 64);
constexpr qreal kHead = 4.2;   // mezza larghezza della punta di una freccia

QPen strokePen(const QColor &c, qreal w, Qt::PenStyle s = Qt::SolidLine)
{
    return QPen(c, w, s, Qt::RoundCap, Qt::RoundJoin);
}

void head(QPainter &p, const QPointF &tip, QPointF dir, const QColor &c)
{
    const qreal len = std::hypot(dir.x(), dir.y());
    if (len <= 0.0) return;
    dir /= len;
    const QPointF n(-dir.y(), dir.x());
    QPolygonF tri;
    tri << tip << tip - dir * kHead * 1.7 + n * kHead << tip - dir * kHead * 1.7 - n * kHead;
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawPolygon(tri);
    p.restore();
}

void arrow(QPainter &p, const QPointF &a, const QPointF &b, const QColor &c = kAccent, bool both = false)
{
    QPointF d = b - a;
    const qreal len = std::hypot(d.x(), d.y());
    if (len <= 0.0) return;
    d /= len;
    const qreal back = kHead * 1.5;
    p.save();
    p.setPen(strokePen(c, 2.6));
    p.drawLine(both ? a + d * back : a, b - d * back);
    p.restore();
    head(p, b, d, c);
    if (both) head(p, a, -d, c);
}

// Arco dell'ellisse inscritta in r, da startDeg per spanDeg gradi (come Qt: 0
// a destra, positivi in senso antiorario a schermo), con la punta alla fine.
void arcArrow(QPainter &p, const QRectF &r, qreal startDeg, qreal spanDeg, const QColor &c = kAccent)
{
    const QPointF ctr = r.center();
    const qreal rx = r.width() / 2.0, ry = r.height() / 2.0;
    const qreal sign = spanDeg >= 0.0 ? 1.0 : -1.0;
    const qreal trimDeg = qRadiansToDegrees(kHead * 1.5 / std::max(rx, ry));
    p.save();
    p.setPen(strokePen(c, 2.6));
    p.setBrush(Qt::NoBrush);
    p.drawArc(r, qRound(startDeg * 16.0), qRound((spanDeg - sign * trimDeg) * 16.0));
    p.restore();
    const qreal a = qDegreesToRadians(startDeg + spanDeg);
    const QPointF tip(ctr.x() + rx * std::cos(a), ctr.y() - ry * std::sin(a));
    head(p, tip, QPointF(-rx * std::sin(a), -ry * std::cos(a)) * sign, c);
}

// Quattro frecce dal centro verso fuori, da `inner` a `outer` unita':
// diritte (spostare) o in diagonale (ingrandire).
void fourArrows(QPainter &p, const QPointF &c, qreal inner, qreal outer, bool diagonal)
{
    const qreal k = diagonal ? M_SQRT1_2 : 1.0;
    const QPointF dirs[4] = { diagonal ? QPointF(k, k) : QPointF(1, 0), diagonal ? QPointF(-k, k) : QPointF(-1, 0),
                              diagonal ? QPointF(k, -k) : QPointF(0, 1), diagonal ? QPointF(-k, -k) : QPointF(0, -1) };
    for (const QPointF &d : dirs) arrow(p, c + d * inner, c + d * outer);
}

void torus(QPainter &p, const QPointF &c, qreal rx, qreal ry, bool dim = false)
{
    QPainterPath outer;
    outer.addEllipse(c, rx, ry);
    QPainterPath hole;
    hole.addEllipse(QPointF(c.x(), c.y() - ry * 0.08), rx * 0.42, ry * 0.34);
    p.save();
    p.setPen(strokePen(dim ? kDim : kLine, 2.2));
    p.setBrush(dim ? QColor(46, 46, 52) : kBody);
    p.drawPath(outer.subtracted(hole));
    p.restore();
}

void sphere(QPainter &p, const QPointF &c, qreal r, bool selected)
{
    p.save();
    QRadialGradient g(c - QPointF(r * 0.35, r * 0.35), r * 1.3);
    g.setColorAt(0.0, QColor(120, 120, 130));
    g.setColorAt(1.0, kBody);
    p.setPen(strokePen(kLine, 2.2));
    p.setBrush(g);
    p.drawEllipse(c, r, r);
    p.setPen(strokePen(kLine, 1.3));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(c.x() - r, c.y() - r * 0.35, 2.0 * r, r * 0.7), 180 * 16, 180 * 16);
    if (selected) {
        p.setPen(strokePen(kHot, 2.2));
        p.drawEllipse(c, r + 3.5, r + 3.5);
    }
    p.restore();
}

enum class MouseHot { None, Left, Right, Wheel };

void mouse(QPainter &p, const QRectF &body, MouseHot hot)
{
    p.save();
    QPainterPath shape;
    shape.addRoundedRect(body, body.width() * 0.46, body.width() * 0.46);
    p.setPen(Qt::NoPen);
    p.setBrush(kBody);
    p.drawPath(shape);
    const qreal splitY = body.top() + body.height() * 0.42;
    const qreal cx = body.center().x();
    if (hot == MouseHot::Left || hot == MouseHot::Right) {
        QPainterPath half;
        half.addRect(hot == MouseHot::Left ? QRectF(body.left(), body.top(), cx - body.left(), splitY - body.top())
                                           : QRectF(cx, body.top(), body.right() - cx, splitY - body.top()));
        p.setBrush(kHot);
        p.drawPath(shape.intersected(half));
    }
    p.setPen(strokePen(kLine, 2.2));
    p.setBrush(Qt::NoBrush);
    p.drawPath(shape);
    p.drawLine(QPointF(body.left(), splitY), QPointF(body.right(), splitY));
    p.drawLine(QPointF(cx, body.top()), QPointF(cx, splitY));
    p.setBrush(hot == MouseHot::Wheel ? kHot : kBody);
    p.drawRoundedRect(QRectF(cx - 3.0, body.top() + 4.5, 6.0, 10.0), 3.0, 3.0);
    p.restore();
}

// Un dito sullo schermo: il punto toccato con l'alone.
void touch(QPainter &p, const QPointF &c, const QColor &col = kHot)
{
    p.save();
    QColor halo = col;
    halo.setAlpha(90);
    p.setPen(Qt::NoPen);
    p.setBrush(halo);
    p.drawEllipse(c, 10.0, 10.0);
    p.setBrush(col);
    p.drawEllipse(c, 6.0, 6.0);
    p.restore();
}

// La camera (come l'icona di una videocamera), che guarda verso lookTo.
void cameraGlyph(QPainter &p, const QPointF &at, const QPointF &lookTo)
{
    p.save();
    p.translate(at);
    p.rotate(qRadiansToDegrees(std::atan2(lookTo.y() - at.y(), lookTo.x() - at.x())));
    p.setPen(strokePen(QColor(30, 30, 30), 1.2));
    p.setBrush(kLine);
    p.drawRoundedRect(QRectF(-11.0, -6.0, 14.0, 12.0), 2.5, 2.5);
    QPolygonF lens;
    lens << QPointF(3.5, 0.0) << QPointF(10.0, -6.0) << QPointF(10.0, 6.0);
    p.drawPolygon(lens);
    p.restore();
}

// La traiettoria di un path della camera, tratteggiata, con la camera sopra.
void orbit(QPainter &p, const QPointF &c, qreal rx, qreal ry, bool camera = true, bool dim = false)
{
    p.save();
    QPen dash = strokePen(dim ? kDim : kLine, 2.0, Qt::CustomDashLine);
    dash.setDashPattern({ 2.2, 2.2 });
    p.setPen(dash);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(c, rx, ry);
    p.restore();
    if (camera) {
        const qreal a = qDegreesToRadians(-38.0);
        cameraGlyph(p, QPointF(c.x() + rx * std::cos(a), c.y() - ry * std::sin(a)), c);
    }
}

void playMark(QPainter &p)
{
    QPolygonF tri;
    tri << QPointF(4, 3) << QPointF(4, 15) << QPointF(14, 9);
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(kAccent);
    p.drawPolygon(tri);
    p.restore();
}

// Un'immagine (la texture della vista 2D): cornice, monti e sole.
void picture(QPainter &p, const QRectF &r)
{
    p.save();
    p.setPen(strokePen(kLine, 2.2));
    p.setBrush(kBody);
    p.drawRoundedRect(r, 3.0, 3.0);
    QPainterPath hills;
    hills.moveTo(r.left() + 3.0, r.bottom() - 3.0);
    hills.lineTo(r.left() + r.width() * 0.38, r.top() + r.height() * 0.42);
    hills.lineTo(r.left() + r.width() * 0.58, r.top() + r.height() * 0.68);
    hills.lineTo(r.left() + r.width() * 0.74, r.top() + r.height() * 0.52);
    hills.lineTo(r.right() - 3.0, r.bottom() - 3.0);
    hills.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(kDim);
    p.drawPath(hills);
    p.setBrush(kHot);
    p.drawEllipse(QPointF(r.right() - r.width() * 0.24, r.top() + r.height() * 0.28), r.width() * 0.08, r.width() * 0.08);
    p.restore();
}

void magnifier(QPainter &p, const QPointF &c, qreal r, const QPointF &handleEnd)
{
    p.save();
    QPointF d = handleEnd - c;
    d /= std::hypot(d.x(), d.y());
    p.setPen(strokePen(kLine, 5.0));
    p.drawLine(c + d * (r + 1.5), handleEnd);
    p.setPen(strokePen(kLine, 2.6));
    p.setBrush(QColor(40, 40, 46, 220));
    p.drawEllipse(c, r, r);
    p.setPen(strokePen(kAccent, 2.6));
    p.drawLine(QPointF(c.x() - r * 0.5, c.y()), QPointF(c.x() + r * 0.5, c.y()));
    p.drawLine(QPointF(c.x(), c.y() - r * 0.5), QPointF(c.x(), c.y() + r * 0.5));
    p.restore();
}

void textMark(QPainter &p, const QRectF &r, const QString &text, const QColor &c, qreal pixelSize)
{
    p.save();
    QFont f = p.font();
    f.setPixelSize(qRound(pixelSize));
    f.setBold(true);
    p.setFont(f);
    p.setPen(c);
    p.drawText(r, Qt::AlignCenter, text);
    p.restore();
}

// --- IL CONTROLLER ---
// I simboli dei quattro tasti a destra, nella posizione del pad: in alto il
// triangolo, a destra il cerchio, in basso la croce, a sinistra il quadrato
// (su un pad Xbox, nelle stesse posizioni, Y B A X).
enum class Face { Triangle, Circle, Cross, Square };

void faceSymbol(QPainter &p, const QPointF &c, Face face, qreal r, const QColor &col)
{
    p.save();
    p.setPen(strokePen(col, std::max<qreal>(1.6, r * 0.22)));
    p.setBrush(Qt::NoBrush);
    const qreal s = r * 0.5;
    switch (face) {
    case Face::Cross:
        p.drawLine(c + QPointF(-s, -s), c + QPointF(s, s));
        p.drawLine(c + QPointF(-s, s), c + QPointF(s, -s));
        break;
    case Face::Circle:
        p.drawEllipse(c, s * 1.05, s * 1.05);
        break;
    case Face::Triangle: {
        QPolygonF t;
        t << c + QPointF(0, -s * 1.15) << c + QPointF(s * 1.1, s * 0.75) << c + QPointF(-s * 1.1, s * 0.75);
        p.drawPolygon(t);
        break;
    }
    case Face::Square:
        p.drawRect(QRectF(c.x() - s * 0.9, c.y() - s * 0.9, s * 1.8, s * 1.8));
        break;
    }
    p.restore();
}

// I quattro tasti a rombo attorno a c; `hot` quello acceso (o nessuno, -1).
void faceDiamond(QPainter &p, const QPointF &c, qreal gap, qreal r, int hot)
{
    const QPointF at[4] = { c + QPointF(0, -gap), c + QPointF(gap, 0), c + QPointF(0, gap), c + QPointF(-gap, 0) };
    const Face faces[4] = { Face::Triangle, Face::Circle, Face::Cross, Face::Square };
    for (int i = 0; i < 4; ++i) {
        const bool on = hot == int(faces[i]);
        p.save();
        p.setPen(strokePen(on ? kHot : kDim, 1.8));
        p.setBrush(on ? kHot : kBody);
        p.drawEllipse(at[i], r, r);
        p.restore();
        faceSymbol(p, at[i], faces[i], r, on ? QColor(30, 30, 30) : kLine);
    }
}

// La croce direzionale attorno a c; accesi i bracci in alto e in basso
// (vertical) o a sinistra e a destra, o nessuno.
enum class DpadHot { None, Vertical, Horizontal };

void dpad(QPainter &p, const QPointF &c, qreal arm, qreal width, DpadHot hot)
{
    const qreal h = width / 2.0;
    QPainterPath cross;
    cross.addRoundedRect(QRectF(c.x() - h, c.y() - arm, width, 2 * arm), 3, 3);
    cross.addRoundedRect(QRectF(c.x() - arm, c.y() - h, 2 * arm, width), 3, 3);
    cross = cross.simplified();
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(kBody);
    p.drawPath(cross);
    if (hot != DpadHot::None) {
        QPainterPath arms;
        if (hot == DpadHot::Vertical) {
            arms.addRect(QRectF(c.x() - h, c.y() - arm, width, arm - h));
            arms.addRect(QRectF(c.x() - h, c.y() + h, width, arm - h));
        } else {
            arms.addRect(QRectF(c.x() - arm, c.y() - h, arm - h, width));
            arms.addRect(QRectF(c.x() + h, c.y() - h, arm - h, width));
        }
        p.setBrush(kHot);
        p.drawPath(cross.intersected(arms));
    }
    p.setPen(strokePen(kLine, 1.8));
    p.setBrush(Qt::NoBrush);
    p.drawPath(cross);
    // Le frecce stampate sui bracci.
    const qreal t = width * 0.22, d = arm - width * 0.45;
    const QPointF dirs[4] = { QPointF(0, -1), QPointF(1, 0), QPointF(0, 1), QPointF(-1, 0) };
    for (const QPointF &dir : dirs) {
        const bool on = (hot == DpadHot::Vertical && dir.x() == 0) || (hot == DpadHot::Horizontal && dir.y() == 0);
        const QPointF tip = c + dir * (d + t);
        const QPointF n(-dir.y(), dir.x());
        QPolygonF tri;
        tri << tip << c + dir * (d - t) + n * t * 1.2 << c + dir * (d - t) - n * t * 1.2;
        p.setPen(Qt::NoPen);
        p.setBrush(on ? QColor(30, 30, 30) : kDim);
        p.drawPolygon(tri);
    }
    p.restore();
}

// Uno stick visto dall'alto: la base, il cappuccio con la lettera.
void stick(QPainter &p, const QPointF &c, qreal r, const QString &letter, bool arrows)
{
    p.save();
    p.setPen(strokePen(kDim, 1.8));
    p.setBrush(QColor(38, 38, 44));
    p.drawEllipse(c, r, r);
    p.setPen(strokePen(QColor(30, 30, 30), 1.4));
    p.setBrush(kHot);
    p.drawEllipse(c, r * 0.62, r * 0.62);
    p.restore();
    textMark(p, QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), letter, QColor(30, 30, 30), r * 0.62);
    if (arrows) {
        const QPointF dirs[4] = { QPointF(0, -1), QPointF(1, 0), QPointF(0, 1), QPointF(-1, 0) };
        for (const QPointF &dir : dirs) {
            const QPointF n(-dir.y(), dir.x());
            const QPointF tip = c + dir * (r * 0.95);
            QPolygonF tri;
            tri << tip << c + dir * (r * 0.75) + n * r * 0.13 << c + dir * (r * 0.75) - n * r * 0.13;
            p.save();
            p.setPen(Qt::NoPen);
            p.setBrush(kLine);
            p.drawPolygon(tri);
            p.restore();
        }
    }
}

// Un tasto sul dorso (grilletto o dorsale) con la sua sigla.
void shoulder(QPainter &p, const QRectF &r, qreal radius, const QString &label, qreal textSize)
{
    p.save();
    p.setPen(strokePen(QColor(30, 30, 30), 1.4));
    p.setBrush(kHot);
    p.drawRoundedRect(r, radius, radius);
    p.restore();
    textMark(p, r, label, QColor(30, 30, 30), textSize);
}

// Un cubo in fil di ferro: davanti il quadrato `front`, dietro lo stesso
// spostato di `back` e scalato di `shrink` (1 = proiezione ortogonale).
void cube(QPainter &p, const QRectF &front, const QPointF &back, qreal shrink)
{
    const QPointF fc = front.center();
    QRectF rear(0, 0, front.width() * shrink, front.height() * shrink);
    rear.moveCenter(fc + back);
    const QPointF f[4] = { front.topLeft(), front.topRight(), front.bottomRight(), front.bottomLeft() };
    const QPointF b[4] = { rear.topLeft(), rear.topRight(), rear.bottomRight(), rear.bottomLeft() };
    p.save();
    p.setBrush(Qt::NoBrush);
    p.setPen(strokePen(kDim, 1.6));
    p.drawRect(rear);
    p.setPen(strokePen(kLine, 1.8));
    for (int i = 0; i < 4; ++i) p.drawLine(f[i], b[i]);
    p.drawRect(front);
    p.restore();
}

using Painter = std::function<void(QPainter &)>;

#include "controlswindow_drive.inc"   // 2.1: le figure della guida libera (drivePictograms)

const std::map<QString, Painter> &pictograms()
{
    static const std::map<QString, Painter> table = {
        // --- I GESTI: mouse e dita ---
        { QStringLiteral("mouse-left"), [](QPainter &p) {
              mouse(p, QRectF(19, 5, 26, 40), MouseHot::Left);
              arrow(p, QPointF(12, 56), QPointF(52, 56), kAccent, true);
          } },
        { QStringLiteral("mouse-right"), [](QPainter &p) {
              mouse(p, QRectF(19, 5, 26, 40), MouseHot::Right);
              arrow(p, QPointF(12, 56), QPointF(52, 56), kAccent, true);
          } },
        { QStringLiteral("mouse-wheel"), [](QPainter &p) {
              mouse(p, QRectF(14, 12, 26, 40), MouseHot::Wheel);
              arrow(p, QPointF(52, 8), QPointF(52, 56), kAccent, true);
          } },
        { QStringLiteral("mouse-click"), [](QPainter &p) {
              mouse(p, QRectF(24, 14, 26, 40), MouseHot::Left);
              p.setPen(strokePen(kAccent, 2.6));
              p.drawLine(QPointF(18, 13), QPointF(11, 6));
              p.drawLine(QPointF(15, 23), QPointF(6, 23));
              p.drawLine(QPointF(27, 9), QPointF(27, 2));
          } },
        { QStringLiteral("finger-one"), [](QPainter &p) {
              touch(p, QPointF(32, 26));
              arrow(p, QPointF(12, 54), QPointF(52, 54), kAccent, true);
          } },
        { QStringLiteral("finger-two"), [](QPainter &p) {
              touch(p, QPointF(21, 26));
              touch(p, QPointF(43, 26));
              arrow(p, QPointF(12, 54), QPointF(52, 54), kAccent, true);
          } },
        { QStringLiteral("pinch"), [](QPainter &p) {
              touch(p, QPointF(25, 39));
              touch(p, QPointF(39, 25));
              arrow(p, QPointF(18, 46), QPointF(6, 58));
              arrow(p, QPointF(46, 18), QPointF(58, 6));
          } },
        { QStringLiteral("tap"), [](QPainter &p) {
              touch(p, QPointF(32, 32));
              QColor ring = kHot;
              ring.setAlpha(150);
              p.setPen(strokePen(ring, 1.8));
              p.setBrush(Qt::NoBrush);
              p.drawEllipse(QPointF(32, 32), 16.0, 16.0);
              ring.setAlpha(80);
              p.setPen(strokePen(ring, 1.8));
              p.drawEllipse(QPointF(32, 32), 22.0, 22.0);
          } },
        { QStringLiteral("shift"), [](QPainter &p) {
              p.setPen(strokePen(kLine, 2.2));
              p.setBrush(kBody);
              p.drawRoundedRect(QRectF(10, 10, 44, 44), 7, 7);
              QPolygonF up;
              up << QPointF(32, 18) << QPointF(45, 32) << QPointF(38, 32) << QPointF(38, 45) << QPointF(26, 45)
                 << QPointF(26, 32) << QPointF(19, 32);
              p.setPen(strokePen(kHot, 2.4));
              p.setBrush(Qt::NoBrush);
              p.drawPolygon(up);
          } },
        { QStringLiteral("none"), [](QPainter &p) {
              p.setPen(strokePen(kDim, 3.2));
              p.drawLine(QPointF(22, 32), QPointF(42, 32));
          } },

        // --- CHI SI MUOVE: la superficie, la scena, l'oggetto, il path ---
        { QStringLiteral("rotate"), [](QPainter &p) {
              torus(p, QPointF(32, 32), 15, 8.5);
              arcArrow(p, QRectF(5, 5, 54, 54), 35, 290);
          } },
        { QStringLiteral("slide"), [](QPainter &p) {
              torus(p, QPointF(32, 32), 12, 7);
              fourArrows(p, QPointF(32, 32), 15, 28, false);
          } },
        { QStringLiteral("zoom"), [](QPainter &p) {
              torus(p, QPointF(28, 28), 9, 5, true);
              magnifier(p, QPointF(28, 28), 16, QPointF(55, 55));
          } },
        { QStringLiteral("obj-rotate"), [](QPainter &p) {
              torus(p, QPointF(14, 13), 9, 5, true);
              sphere(p, QPointF(36, 38), 9, true);
              arcArrow(p, QRectF(15, 17, 42, 42), 60, 285);
          } },
        { QStringLiteral("obj-move"), [](QPainter &p) {
              torus(p, QPointF(13, 12), 9, 5, true);
              sphere(p, QPointF(34, 36), 8, true);
              fourArrows(p, QPointF(34, 36), 15, 26, false);
          } },
        { QStringLiteral("obj-size"), [](QPainter &p) {
              torus(p, QPointF(13, 12), 9, 5, true);
              sphere(p, QPointF(34, 36), 8, true);
              fourArrows(p, QPointF(34, 36), 15, 26, true);
          } },
        { QStringLiteral("path-turn"), [](QPainter &p) {
              torus(p, QPointF(32, 40), 7, 4, true);
              orbit(p, QPointF(32, 40), 25, 11);
              arcArrow(p, QRectF(8, 8, 48, 48), 165, -150);
          } },
        { QStringLiteral("path-move"), [](QPainter &p) {
              orbit(p, QPointF(32, 34), 26, 12);
              fourArrows(p, QPointF(32, 34), 3, 15, false);
          } },
        { QStringLiteral("path-size"), [](QPainter &p) {
              orbit(p, QPointF(32, 34), 12, 5.5, false, true);
              orbit(p, QPointF(32, 34), 28, 13);
              arrow(p, QPointF(45, 34), QPointF(57, 34));
              arrow(p, QPointF(19, 34), QPointF(7, 34));
          } },

        // --- LE SITUAZIONI (la prima colonna) ---
        { QStringLiteral("sit-surface"), [](QPainter &p) {
              torus(p, QPointF(32, 32), 25, 14);
          } },
        { QStringLiteral("sit-object"), [](QPainter &p) {
              torus(p, QPointF(22, 24), 16, 9, true);
              sphere(p, QPointF(44, 43), 10, true);
          } },
        { QStringLiteral("sit-path"), [](QPainter &p) {
              torus(p, QPointF(32, 36), 9, 5, true);
              orbit(p, QPointF(32, 36), 27, 12);
              playMark(p);
          } },
        { QStringLiteral("sit-path-object"), [](QPainter &p) {
              orbit(p, QPointF(32, 36), 27, 12);
              sphere(p, QPointF(32, 36), 6, true);
              playMark(p);
          } },

        // --- LA VISTA 2D (la texture) ---
        { QStringLiteral("tex-rotate"), [](QPainter &p) {
              picture(p, QRectF(18, 20, 28, 24));
              arcArrow(p, QRectF(5, 5, 54, 54), 35, 290);
          } },
        { QStringLiteral("tex-90"), [](QPainter &p) {
              picture(p, QRectF(12, 24, 28, 24));
              arcArrow(p, QRectF(8, 6, 44, 44), 150, -110);
              textMark(p, QRectF(36, 44, 28, 18), QStringLiteral("90°"), kAccent, 12);
          } },
        { QStringLiteral("tex-move"), [](QPainter &p) {
              picture(p, QRectF(20, 22, 24, 20));
              fourArrows(p, QPointF(32, 32), 15, 28, false);
          } },
        { QStringLiteral("tex-zoom"), [](QPainter &p) {
              picture(p, QRectF(6, 8, 34, 28));
              magnifier(p, QPointF(38, 38), 12, QPointF(57, 57));
          } },

        // --- IL CONTROLLER: i comandi ---
        { QStringLiteral("pad-ls"), [](QPainter &p) { stick(p, QPointF(32, 32), 26, QStringLiteral("L"), true); } },
        { QStringLiteral("pad-rs"), [](QPainter &p) { stick(p, QPointF(32, 32), 26, QStringLiteral("R"), true); } },
        { QStringLiteral("pad-triggers"), [](QPainter &p) {
              shoulder(p, QRectF(4, 14, 26, 36), 9, QStringLiteral("L2"), 12);
              shoulder(p, QRectF(34, 14, 26, 36), 9, QStringLiteral("R2"), 12);
          } },
        { QStringLiteral("pad-bumpers"), [](QPainter &p) {
              shoulder(p, QRectF(2, 22, 28, 20), 6, QStringLiteral("L1"), 12);
              shoulder(p, QRectF(34, 22, 28, 20), 6, QStringLiteral("R1"), 12);
          } },
        { QStringLiteral("pad-cross"), [](QPainter &p) { faceDiamond(p, QPointF(32, 32), 19, 10, int(Face::Cross)); } },
        { QStringLiteral("pad-circle"), [](QPainter &p) { faceDiamond(p, QPointF(32, 32), 19, 10, int(Face::Circle)); } },
        { QStringLiteral("pad-triangle"), [](QPainter &p) { faceDiamond(p, QPointF(32, 32), 19, 10, int(Face::Triangle)); } },
        { QStringLiteral("pad-square"), [](QPainter &p) { faceDiamond(p, QPointF(32, 32), 19, 10, int(Face::Square)); } },
        { QStringLiteral("pad-dpad-ud"), [](QPainter &p) { dpad(p, QPointF(32, 32), 26, 17, DpadHot::Vertical); } },
        { QStringLiteral("pad-dpad-lr"), [](QPainter &p) { dpad(p, QPointF(32, 32), 26, 17, DpadHot::Horizontal); } },

        // --- IL CONTROLLER: cio' che fanno i tasti ---
        { QStringLiteral("fx-startstop"), [](QPainter &p) {
              QPolygonF play;
              play << QPointF(10, 20) << QPointF(10, 44) << QPointF(30, 32);
              p.setPen(Qt::NoPen);
              p.setBrush(kAccent);
              p.drawPolygon(play);
              p.drawRoundedRect(QRectF(36, 21, 22, 22), 2, 2);
          } },
        { QStringLiteral("fx-reset"), [](QPainter &p) {
              torus(p, QPointF(32, 32), 11, 6, true);
              arcArrow(p, QRectF(8, 8, 48, 48), 60, 290);
          } },
        { QStringLiteral("fx-projection"), [](QPainter &p) {
              cube(p, QRectF(5, 26, 24, 24), QPointF(6, -9), 0.62);
              cube(p, QRectF(35, 28, 20, 20), QPointF(7, -9), 1.0);
          } },
        { QStringLiteral("fx-home"), [](QPainter &p) {
              torus(p, QPointF(32, 33), 15, 8.5);
              p.setPen(strokePen(kAccent, 2.6));
              const qreal l = 9, x0 = 6, y0 = 10, x1 = 58, y1 = 56;
              p.drawPolyline(QPolygonF({ QPointF(x0, y0 + l), QPointF(x0, y0), QPointF(x0 + l, y0) }));
              p.drawPolyline(QPolygonF({ QPointF(x1 - l, y0), QPointF(x1, y0), QPointF(x1, y0 + l) }));
              p.drawPolyline(QPolygonF({ QPointF(x1, y1 - l), QPointF(x1, y1), QPointF(x1 - l, y1) }));
              p.drawPolyline(QPolygonF({ QPointF(x0 + l, y1), QPointF(x0, y1), QPointF(x0, y1 - l) }));
          } },
        { QStringLiteral("fx-presets"), [](QPainter &p) {
              p.setPen(Qt::NoPen);
              for (int i = 0; i < 4; ++i) {
                  p.setBrush(i == 1 ? kHot : kDim);
                  p.drawRoundedRect(QRectF(8, 10 + i * 12, 34, 7), 3, 3);
              }
              arrow(p, QPointF(54, 26), QPointF(54, 6));
              arrow(p, QPointF(54, 38), QPointF(54, 58));
          } },
        { QStringLiteral("fx-w"), [](QPainter &p) {
              textMark(p, QRectF(2, 14, 34, 36), QStringLiteral("W"), kLine, 26);
              arrow(p, QPointF(46, 8), QPointF(46, 56), kAccent, true);
          } },
        { QStringLiteral("fx-objects"), [](QPainter &p) {
              torus(p, QPointF(16, 40), 11, 6, true);
              sphere(p, QPointF(46, 40), 9, true);
              arrow(p, QPointF(8, 14), QPointF(56, 14), kAccent, true);
          } },
    };
    return table;
}

// Il browser delle pagine: le immagini pict:<nome>@<lato> le disegna lui.
class PictogramBrowser : public QTextBrowser
{
public:
    using QTextBrowser::QTextBrowser;
    QStringList missing;

    QVariant loadResource(int type, const QUrl &name) override
    {
        if (type == QTextDocument::ImageResource && name.scheme() == QLatin1String("pict")) {
            const QString spec = name.path();
            const int at = spec.lastIndexOf(QLatin1Char('@'));
            const QString key = at > 0 ? spec.left(at) : spec;
            // @lato per le figure quadrate, @larghezzaxaltezza per i disegni larghi.
            const QStringList dims = at > 0 ? spec.mid(at + 1).split(QLatin1Char('x')) : QStringList();
            const int w = dims.isEmpty() ? 56 : dims[0].toInt();
            const int h = dims.size() > 1 ? dims[1].toInt() : w;
            const QImage image = paintControlsPictogram(key, QSize(w, h), devicePixelRatioF());
            if (image.isNull()) {
                if (!missing.contains(key)) missing << key;
                return QVariant();
            }
            return image;
        }
        return QTextBrowser::loadResource(type, name);
    }
};

QPointer<ControlsWindow> s_open[2];

} // namespace

// IL CONTROLLER INTERO, su una tela di 420 x 230: un pad generico a due
// impugnature (non il disegno di un modello vero), coi comandi che l'app usa.
static void padDiagram(QPainter &p)
{
    // I grilletti e i dorsali, dietro il corpo: le sigle nella parte che
    // sporge.
    shoulder(p, QRectF(104, 4, 52, 34), 11, QStringLiteral("L2"), 15);
    shoulder(p, QRectF(264, 4, 52, 34), 11, QStringLiteral("R2"), 15);
    shoulder(p, QRectF(92, 30, 72, 24), 9, QStringLiteral("L1"), 13);
    shoulder(p, QRectF(256, 30, 72, 24), 9, QStringLiteral("R1"), 13);
    // Il corpo: una fascia arrotondata e le due impugnature.
    QPainterPath body;
    body.addRoundedRect(QRectF(78, 56, 264, 122), 46, 46);
    QPainterPath grip;
    grip.addEllipse(QPointF(116, 150), 58, 72);
    body = body.united(grip);
    grip = QPainterPath();
    grip.addEllipse(QPointF(304, 150), 58, 72);
    body = body.united(grip);
    p.save();
    p.setPen(strokePen(kLine, 2.4));
    p.setBrush(QColor(50, 50, 58));
    p.drawPath(body);
    p.restore();
    dpad(p, QPointF(128, 102), 24, 17, DpadHot::None);
    faceDiamond(p, QPointF(292, 102), 23, 11, -1);
    stick(p, QPointF(172, 146), 24, QStringLiteral("L"), false);
    stick(p, QPointF(248, 146), 24, QStringLiteral("R"), false);
}

namespace {
struct Wide { QSizeF canvas; Painter paint; };

const std::map<QString, Wide> &wideDiagrams()
{
    static const std::map<QString, Wide> table = {
        { QStringLiteral("pad"), { QSizeF(420, 230), padDiagram } },
    };
    return table;
}
} // namespace

QImage paintControlsPictogram(const QString &name, const QSize &size, qreal dpr)
{
    Painter paint;
    QSizeF canvas(64, 64);
    if (const auto it = pictograms().find(name); it != pictograms().end()) {
        paint = it->second;
    } else if (const auto drive = drivePictograms().find(name); drive != drivePictograms().end()) {   // 2.1
        paint = drive->second;
    } else if (const auto wide = wideDiagrams().find(name); wide != wideDiagrams().end()) {
        paint = wide->second.paint;
        canvas = wide->second.canvas;
    }
    if (!paint || size.isEmpty()) return QImage();
    const qreal ratio = std::max<qreal>(1.0, dpr);
    QImage image(qCeil(size.width() * ratio), qCeil(size.height() * ratio), QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(ratio);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.scale(size.width() / canvas.width(), size.height() / canvas.height());
    paint(p);
    p.end();
    return image;
}

QImage paintControlsPictogram(const QString &name, int side, qreal dpr)
{
    return paintControlsPictogram(name, QSize(side, side), dpr);
}

// ==========================================================
// LA FINESTRA
// ==========================================================

QString ControlsWindow::title(Kind kind)
{
    return kind == GameController ? QStringLiteral("Game Controller") : QStringLiteral("Mouse & Touch");
}

QString ControlsWindow::pageUrl(Kind kind)
{
    return kind == GameController ? QStringLiteral("qrc:/docs/controls_controller.html")
                                  : QStringLiteral("qrc:/docs/controls_mouse.html");
}

ControlsWindow *ControlsWindow::current(Kind kind)
{
    return s_open[kind].data();
}

ControlsWindow *ControlsWindow::open(QWidget *parent, Kind kind)
{
    if (ControlsWindow *w = s_open[kind].data()) {
        w->show();
        w->raise();
        w->activateWindow();
        return w;
    }
    auto *w = new ControlsWindow(parent, kind);
    s_open[kind] = w;
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    // Su iPad, iPhone e Android la mostra gia' setupDocumentationDialog, a
    // tutto schermo.
    w->show();
#endif
    return w;
}

ControlsWindow::ControlsWindow(QWidget *parent, Kind kind)
    : QDialog(parent)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setModal(false);
    setWindowTitle(title(kind));

    auto *layout = new QVBoxLayout(this);
    auto *browser = new PictogramBrowser(this);
    m_browser = browser;
    browser->setOpenLinks(false);
    // Come la documentazione: il motore rich text ignora il grosso del CSS
    // della pagina, la grandezza del testo si impone qui prima di caricarla.
    browser->document()->setDefaultStyleSheet(
        "p, li, div, td, th { font-size: 15px; }"
        " h3 { font-size: 19px; }");
    // SCHERMI STRETTI (iPhone): le figure si riducono in proporzione, fino a
    // 0.6, perche' le quattro colonne stiano nella larghezza senza scorrere di
    // lato. Il testo va a capo da se'.
    QString html;
    QFile page(QStringLiteral(":") + QUrl(pageUrl(kind)).path());
    if (page.open(QIODevice::ReadOnly)) html = QString::fromUtf8(page.readAll());
    const QScreen *screen = parent && parent->screen() ? parent->screen() : QGuiApplication::primaryScreen();
    const int available = screen ? screen->availableGeometry().width() : 700;
    const qreal scale = std::clamp((available - 40) / 660.0, 0.6, 1.0);
    if (scale < 1.0) {
        static const QRegularExpression sized(
            QStringLiteral("@(\\d+)(?:x(\\d+))?\" width=\"(\\d+)\" height=\"(\\d+)\""));
        QString scaled;
        qsizetype from = 0;
        for (auto it = sized.globalMatch(html); it.hasNext();) {
            const QRegularExpressionMatch m = it.next();
            const int w = qRound(m.captured(3).toInt() * scale), h = qRound(m.captured(4).toInt() * scale);
            scaled += html.mid(from, m.capturedStart() - from);
            scaled += (m.captured(2).isEmpty() ? QStringLiteral("@%1").arg(w) : QStringLiteral("@%1x%2").arg(w).arg(h))
                      + QStringLiteral("\" width=\"%1\" height=\"%2\"").arg(w).arg(h);
            from = m.capturedEnd();
        }
        html = scaled + html.mid(from);
    }
    browser->setHtml(html);

    auto *closeBtn = new QPushButton(QStringLiteral("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    layout->addWidget(browser);
    layout->addWidget(closeBtn);
    UiStyleManager::setupDocumentationDialog(this, layout, browser, closeBtn, nullptr);
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    resize(700, 780);
#endif
}

QStringList ControlsWindow::missingPictograms() const
{
    const auto *b = static_cast<const PictogramBrowser *>(m_browser);
    return b ? b->missing : QStringList();
}
