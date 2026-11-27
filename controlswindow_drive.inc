// controlswindow_drive.inc - Le figure della guida libera (DRIVE, 2.1) per le
// finestre dei comandi (ControlsWindow). Solo nel ramo drive: la include
// controlswindow.cpp dentro il suo namespace anonimo, dopo i suoi disegni
// (mouse, dita, stick, frecce, camera), che qui si riusano; la cerca
// paintControlsPictogram dopo le figure comuni. Le pagine le chiamano dalle
// sezioni "Free flight (DRIVE)" di docs/controls_mouse.html e
// docs/controls_controller.html.

// Un tasto della tastiera con la sua scritta.
void keycap(QPainter &p, const QRectF &r, const QString &label, qreal textSize = 15.0)
{
    p.save();
    p.setPen(strokePen(kLine, 1.8));
    p.setBrush(kBody);
    p.drawRoundedRect(r, 5.0, 5.0);
    p.restore();
    textMark(p, r, label, kHot, textSize);
}

void keyPair(QPainter &p, const QString &a, const QString &b)
{
    keycap(p, QRectF(4, 18, 26, 28), a);
    keycap(p, QRectF(34, 18, 26, 28), b);
}

void wideKey(QPainter &p, const QString &label)
{
    keycap(p, QRectF(4, 20, 56, 24), label, 13.0);
}

// Il veicolo della guida libera: la camera, grande `scale` volte, che guarda
// nella direzione `deg` (0 = a destra, 90 = in alto).
void vehicle(QPainter &p, const QPointF &c, qreal scale, qreal deg)
{
    p.save();
    p.translate(c);
    p.scale(scale, scale);
    const qreal a = qDegreesToRadians(deg);
    cameraGlyph(p, QPointF(0, 0), QPointF(std::cos(a), -std::sin(a)));
    p.restore();
}

// Lo schermo di un telefono in orizzontale, coi due pollici della guida: il
// sinistro (velocita' e virata) o il destro (muso e rollio), con le frecce
// del gesto, in verticale o di lato.
void thumbZone(QPainter &p, bool right, bool vertical)
{
    p.save();
    p.setPen(strokePen(kDim, 1.8));
    p.setBrush(QColor(40, 40, 46));
    p.drawRoundedRect(QRectF(3, 12, 58, 40), 6, 6);
    p.setPen(strokePen(kDim, 1.0, Qt::DashLine));
    p.drawLine(QPointF(32, 15), QPointF(32, 49));
    p.restore();
    const QPointF c(right ? 46 : 18, 32);
    touch(p, c);
    const QPointF d = vertical ? QPointF(0, 1) : QPointF(1, 0);
    const qreal reach = vertical ? 19.0 : 15.0;
    arrow(p, c + d * 7.0, c + d * reach);
    arrow(p, c - d * 7.0, c - d * reach);
}

// I tastini rotondi sul bordo destro dello schermo (DriveTouchOverlay):
// salire e scendere, o W+ e W-.
void touchButtons(QPainter &p, const QString &upper, const QString &lower)
{
    p.save();
    p.setPen(strokePen(kDim, 1.8));
    p.setBrush(QColor(40, 40, 46));
    p.drawRoundedRect(QRectF(3, 6, 58, 52), 6, 6);
    p.setPen(strokePen(QColor(30, 30, 30), 1.2));
    p.setBrush(kHot);
    p.drawEllipse(QPointF(46, 20), 9.0, 9.0);
    p.drawEllipse(QPointF(46, 44), 9.0, 9.0);
    p.restore();
    textMark(p, QRectF(37, 11, 18, 18), upper, QColor(30, 30, 30), 11);
    textMark(p, QRectF(37, 35, 18, 18), lower, QColor(30, 30, 30), 11);
}

// Il tastino degli urti, in cima alla colonna a tocco (DriveTouchOverlay):
// una parete e la camera che la tocca, come sullo schermo; sotto, spenti, i
// tastini che lo seguono.
void touchCollideButton(QPainter &p)
{
    p.save();
    p.setPen(strokePen(kDim, 1.8));
    p.setBrush(QColor(40, 40, 46));
    p.drawRoundedRect(QRectF(3, 6, 58, 52), 6, 6);
    p.setPen(strokePen(kDim, 1.2));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(46, 46), 7.0, 7.0);
    p.setPen(strokePen(QColor(30, 30, 30), 1.2));
    p.setBrush(kHot);
    p.drawEllipse(QPointF(46, 22), 12.0, 12.0);
    p.setPen(strokePen(QColor(30, 30, 30), 2.2));
    p.drawLine(QPointF(50, 15), QPointF(50, 29));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(30, 30, 30));
    p.drawEllipse(QPointF(43.5, 22), 3.6, 3.6);
    p.restore();
}

// Uno stick del pad con le frecce del gesto, in verticale o di lato.
void padStick(QPainter &p, const QString &letter, bool vertical)
{
    const QPointF c(32, 32);
    stick(p, c, 19, letter, false);
    const QPointF d = vertical ? QPointF(0, 1) : QPointF(1, 0);
    arrow(p, c + d * 21, c + d * 31);
    arrow(p, c - d * 21, c - d * 31);
}

const std::map<QString, Painter> &drivePictograms()
{
    static const std::map<QString, Painter> table = {
        // --- COSA FA IL VEICOLO ---
        { QStringLiteral("drive-throttle"), [](QPainter &p) {
              vehicle(p, QPointF(30, 32), 1.6, 0);
              arrow(p, QPointF(46, 32), QPointF(61, 32));
              arrow(p, QPointF(14, 32), QPointF(3, 32));
          } },
        { QStringLiteral("drive-yaw"), [](QPainter &p) {
              vehicle(p, QPointF(32, 36), 1.6, 90);
              arcArrow(p, QRectF(8, 12, 48, 48), 90, 70);
              arcArrow(p, QRectF(8, 12, 48, 48), 90, -70);
          } },
        { QStringLiteral("drive-pitch"), [](QPainter &p) {
              vehicle(p, QPointF(24, 32), 1.6, 0);
              arcArrow(p, QRectF(4, 6, 52, 52), 0, 60);
              arcArrow(p, QRectF(4, 6, 52, 52), 0, -60);
          } },
        { QStringLiteral("drive-roll"), [](QPainter &p) {
              // La camera vista da dietro: il corpo, la lente non si vede.
              p.save();
              p.setPen(strokePen(QColor(30, 30, 30), 1.2));
              p.setBrush(kLine);
              p.drawRoundedRect(QRectF(21, 24, 22, 16), 3, 3);
              p.restore();
              arcArrow(p, QRectF(7, 7, 50, 50), 110, -320);
          } },
        { QStringLiteral("drive-lift"), [](QPainter &p) {
              vehicle(p, QPointF(32, 32), 1.6, 0);
              arrow(p, QPointF(32, 21), QPointF(32, 3));
              arrow(p, QPointF(32, 43), QPointF(32, 61));
          } },
        { QStringLiteral("drive-w"), [](QPainter &p) {
              vehicle(p, QPointF(26, 34), 1.3, 0);
              textMark(p, QRectF(0, 0, 26, 26), QStringLiteral("W"), kLine, 20);
              arrow(p, QPointF(52, 8), QPointF(52, 58), kAccent, true);
          } },
        { QStringLiteral("drive-brake"), [](QPainter &p) {
              vehicle(p, QPointF(20, 32), 1.5, 0);
              arrow(p, QPointF(34, 32), QPointF(48, 32));
              p.setPen(strokePen(kHot, 4.0));
              p.drawLine(QPointF(55, 16), QPointF(55, 48));
          } },
        { QStringLiteral("drive-boost"), [](QPainter &p) {
              vehicle(p, QPointF(20, 32), 1.5, 0);
              p.setPen(strokePen(kAccent, 3.2));
              p.setBrush(Qt::NoBrush);
              for (const qreal x : { 38.0, 49.0 })
                  p.drawPolyline(QPolygonF({ QPointF(x, 20), QPointF(x + 10, 32), QPointF(x, 44) }));
          } },
        { QStringLiteral("drive-scale"), [](QPainter &p) {
              textMark(p, QRectF(0, 4, 64, 28), QStringLiteral("×2"), kAccent, 22);
              textMark(p, QRectF(0, 32, 64, 28), QStringLiteral("÷2"), kAccent, 22);
          } },
        { QStringLiteral("drive-collide"), [](QPainter &p) {
              // Gli urti: contro la parete il veicolo non passa, ci scivola lungo.
              p.setPen(strokePen(kHot, 4.0));
              p.drawLine(QPointF(54, 6), QPointF(54, 58));
              vehicle(p, QPointF(22, 42), 1.4, 35);
              arrow(p, QPointF(42, 46), QPointF(42, 10));
          } },
        { QStringLiteral("drive-stop"), [](QPainter &p) {
              vehicle(p, QPointF(22, 32), 1.5, 0);
              p.setPen(Qt::NoPen);
              p.setBrush(kAccent);
              p.drawRoundedRect(QRectF(42, 22, 18, 18), 2, 2);
          } },
        { QStringLiteral("drive-onoff"), [](QPainter &p) {
              vehicle(p, QPointF(32, 24), 1.6, 0);
              textMark(p, QRectF(0, 40, 64, 20), QStringLiteral("DRIVE"), kAccent, 13);
          } },

        // --- I COMANDI: tastiera, mouse, dita, pad ---
        { QStringLiteral("key-ws"), [](QPainter &p) { keyPair(p, QStringLiteral("W"), QStringLiteral("S")); } },
        { QStringLiteral("key-ad"), [](QPainter &p) {
              keycap(p, QRectF(4, 4, 26, 26), QStringLiteral("A"));
              keycap(p, QRectF(34, 4, 26, 26), QStringLiteral("D"));
              keycap(p, QRectF(4, 34, 26, 26), QStringLiteral("←"));
              keycap(p, QRectF(34, 34, 26, 26), QStringLiteral("→"));
          } },
        { QStringLiteral("key-updown"), [](QPainter &p) { keyPair(p, QStringLiteral("↑"), QStringLiteral("↓")); } },
        { QStringLiteral("key-qe"), [](QPainter &p) { keyPair(p, QStringLiteral("Q"), QStringLiteral("E")); } },
        { QStringLiteral("key-rf"), [](QPainter &p) { keyPair(p, QStringLiteral("R"), QStringLiteral("F")); } },
        { QStringLiteral("key-zx"), [](QPainter &p) { keyPair(p, QStringLiteral("Z"), QStringLiteral("X")); } },
        { QStringLiteral("key-pm"), [](QPainter &p) { keyPair(p, QStringLiteral("−"), QStringLiteral("+")); } },
        { QStringLiteral("key-space"), [](QPainter &p) { wideKey(p, QStringLiteral("space")); } },
        { QStringLiteral("key-shift"), [](QPainter &p) { wideKey(p, QStringLiteral("shift")); } },
        { QStringLiteral("key-esc"), [](QPainter &p) { wideKey(p, QStringLiteral("esc")); } },
        { QStringLiteral("key-c"), [](QPainter &p) { keycap(p, QRectF(19, 18, 26, 28), QStringLiteral("C")); } },
        { QStringLiteral("mouse-left-v"), [](QPainter &p) {
              mouse(p, QRectF(14, 12, 26, 40), MouseHot::Left);
              arrow(p, QPointF(52, 8), QPointF(52, 56), kAccent, true);
          } },
        { QStringLiteral("thumb-left-v"), [](QPainter &p) { thumbZone(p, false, true); } },
        { QStringLiteral("thumb-left-h"), [](QPainter &p) { thumbZone(p, false, false); } },
        { QStringLiteral("thumb-right-v"), [](QPainter &p) { thumbZone(p, true, true); } },
        { QStringLiteral("thumb-right-h"), [](QPainter &p) { thumbZone(p, true, false); } },
        { QStringLiteral("touch-lift"), [](QPainter &p) { touchButtons(p, QStringLiteral("▲"), QStringLiteral("▼")); } },
        { QStringLiteral("touch-w"), [](QPainter &p) { touchButtons(p, QStringLiteral("W+"), QStringLiteral("W−")); } },
        { QStringLiteral("touch-collide"), [](QPainter &p) { touchCollideButton(p); } },
        { QStringLiteral("double-tap"), [](QPainter &p) {
              touch(p, QPointF(26, 34));
              QColor ring = kHot;
              ring.setAlpha(150);
              p.setPen(strokePen(ring, 1.8));
              p.setBrush(Qt::NoBrush);
              p.drawEllipse(QPointF(26, 34), 16.0, 16.0);
              textMark(p, QRectF(38, 2, 26, 22), QStringLiteral("2×"), kAccent, 15);
          } },
        { QStringLiteral("pad-ls-v"), [](QPainter &p) { padStick(p, QStringLiteral("L"), true); } },
        { QStringLiteral("pad-ls-h"), [](QPainter &p) { padStick(p, QStringLiteral("L"), false); } },
        { QStringLiteral("pad-rs-v"), [](QPainter &p) { padStick(p, QStringLiteral("R"), true); } },
        { QStringLiteral("pad-rs-h"), [](QPainter &p) { padStick(p, QStringLiteral("R"), false); } },
        { QStringLiteral("pad-l3"), [](QPainter &p) {
              QColor ring = kHot;
              ring.setAlpha(110);
              p.setPen(strokePen(ring, 2.0));
              p.setBrush(Qt::NoBrush);
              p.drawEllipse(QPointF(32, 32), 29.0, 29.0);
              stick(p, QPointF(32, 32), 22, QStringLiteral("L3"), false);
          } },
    };
    return table;
}
