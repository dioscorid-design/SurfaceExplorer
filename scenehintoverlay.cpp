// scenehintoverlay.cpp - Il messaggio in sovrimpressione sulla scena (hint).
// Vedi scenehintoverlay.h.
#include "scenehintoverlay.h"

#include "glwidget.h"

#include <QEvent>
#include <QLabel>
#include <QList>
#include <QMouseEvent>
#include <QPair>
#include <QRegularExpression>
#include <QStringList>
#include <QTimer>

SceneHintOverlay::SceneHintOverlay(GLWidget *view, QObject *parent)
    : QObject(parent), m_view(view)
{
}

QString SceneHintOverlay::compose(const QString &surface, const QString &texture,
                                  const QString &background)
{
    // Nell'ordine in cui compaiono a schermo.
    const QList<QPair<QString, QString>> all = {
        { QStringLiteral("Surface"),    surface.trimmed() },
        { QStringLiteral("Texture"),    texture.trimmed() },
        { QStringLiteral("Background"), background.trimmed() },
    };

    // Vuoti fuori, e identici una volta sola: succede quando un record ha
    // ereditato il testo della texture da cui e' nato, o quando superficie e
    // sfondo usano la stessa texture. Mostrarlo due volte sarebbe solo rumore.
    QList<QPair<QString, QString>> shown;
    for (const auto &p : all) {
        if (p.second.isEmpty()) continue;
        bool dup = false;
        for (const auto &s : shown) if (s.second == p.second) { dup = true; break; }
        if (!dup) shown << p;
    }
    if (shown.isEmpty()) return QString();

    // Ogni legenda dice di CHI sono i suoi slider, anche da sola: senza,
    // "Sliders / F: ..." non diceva se F agisce sulla forma, sulla texture o
    // sullo sfondo. Il ruolo si mette QUI, non nel preset: una texture della
    // Library puo' finire sulla superficie o sullo sfondo. "Sliders" diventa
    // "Texture sliders", "Slider A: ..." "Surface slider A: ...".
    static const QRegularExpression head(QStringLiteral("^Sliders?\\b"));
    QStringList lines;
    for (const auto &s : shown) {
        QString text = s.second;
        const QRegularExpressionMatch m = head.match(text);
        if (m.hasMatch()) lines << text.replace(0, m.capturedLength(), s.first + " " + m.captured(0).toLower());
        else              lines << s.first + " - " + text;
    }
    return lines.join("\n");
}

void SceneHintOverlay::ensureWidget()
{
    if (m_label) return;
    // Figlia del glWidget: sta SOPRA la scena senza toccare il render loop
    // (e quindi senza finire nei frame esportati, composti offscreen).
    m_label = new QLabel(m_view);
    m_label->setObjectName("sceneHintOverlay");
    m_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_label->setWordWrap(true);
    // Testo SEMPLICE: un "\n" nel messaggio va a capo dove vogliamo noi,
    // invece di lasciare la spezzatura alla larghezza della finestra (che
    // tagliava "Slider / E" a meta'). Esplicito anche perche' l'auto-detect
    // di Qt interpreterebbe come HTML un hintText che contenesse dei tag.
    m_label->setTextFormat(Qt::PlainText);
    m_label->setTextInteractionFlags(Qt::NoTextInteraction);
    // CHIUSURA AL CLICK / TOCCO. Il riquadro resta TRASPARENTE al mouse e la
    // chiusura si intercetta sulla vista (eventFilter): installare il filtro
    // sulla QLabel funziona su desktop ma NON su iOS, dove una widget figlia di
    // una superficie RHI/OpenGL non riceve il mouse sintetizzato dal tocco. La
    // vista invece i click li riceve sempre -- e' lei a gestire rotazioni e
    // pan -- quindi si guarda li' se il punto premuto cade nel messaggio.
    m_label->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_label->setCursor(Qt::PointingHandCursor);
    m_label->setStyleSheet(
        "QLabel#sceneHintOverlay {"
        "  color: #ffffff;"
        "  background-color: rgba(0, 0, 0, 170);"
        "  border: 1px solid rgba(255, 255, 255, 90);"
        "  border-radius: 8px;"
        // Padding verticale contenuto: l'altezza la calcola heightForWidth()
        // sul testo riflussato (vedi reposition), quindi ogni pixel qui sopra
        // e sotto si aggiunge a quello -- con 10px il riquadro mostrava una
        // fascia vuota sopra e sotto le righe.
        "  padding: 6px 16px;"
        "  font-size: 15px;"
        "  font-weight: bold;"
        "}");
    // Ridimensionamento della scena e click: filtro sulla vista (non sulla
    // finestra principale, per non intrecciarsi con i filtri gia' installati su
    // tastiera/scroll mobile).
    m_view->installEventFilter(this);

    // CHIUSURA COL TOCCO (iOS/Android). Il filtro copre il mouse, ma su mobile
    // il gesto viene consumato da InputHandler::handleTouch e nessun evento di
    // mouse viene mai sintetizzato: il tocco arriva solo da questo segnale,
    // emesso da GLWidget::event sul TouchBegin.
    connect(m_view, &GLWidget::scenePressedAt, this, [this](const QPoint &pos) {
        if (m_label && m_label->isVisible() && m_label->geometry().contains(pos)) hide();
    });

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &SceneHintOverlay::hide);
}

bool SceneHintOverlay::eventFilter(QObject *obj, QEvent *ev)
{
    if (ev->type() == QEvent::Resize) reposition();

    // CLICK/TOCCO SUL MESSAGGIO -> lo chiude. Si consuma solo il premere DENTRO
    // il riquadro: fuori l'evento prosegue e la scena ruota come sempre.
    if (ev->type() == QEvent::MouseButtonPress && m_label && m_label->isVisible()) {
        auto *me = static_cast<QMouseEvent *>(ev);
        if (m_label->geometry().contains(me->position().toPoint())) {
            hide();
            m_swallowRelease = true;
            return true;   // non arriva a rotazioni/pan
        }
    }

    // ...e si mangia anche il RELEASE di quel click: appartiene al messaggio,
    // non alla vista. (In origine era l'unica difesa: il release orfano veniva
    // preso per un trascinamento, emetteva userMovedView e la scena passava per
    // MODIFICATA. Oggi InputHandler ignora da se' i release senza press -- vedi
    // takeMouseMovedView -- e questo resta come seconda cintura.)
    if (ev->type() == QEvent::MouseButtonRelease && m_swallowRelease) {
        m_swallowRelease = false;
        return true;
    }
    return QObject::eventFilter(obj, ev);
}

void SceneHintOverlay::show(const QString &text, float seconds)
{
    if (!m_view) return;
    if (text.isEmpty()) { hide(); return; }

    ensureWidget();
    m_label->setText(text);
    reposition();
    m_label->show();
    m_label->raise();

    if (seconds > 0.0f) m_timer->start(int(seconds * 1000.0f));
    else                m_timer->stop();   // 0 o negativo = resta finche' non lo nasconde qualcuno
}

void SceneHintOverlay::hide()
{
    if (m_timer) m_timer->stop();
    if (m_label) m_label->hide();
}

void SceneHintOverlay::reposition()
{
    if (!m_label || !m_view) return;

    const int margin = 24;
    const int maxW = qMax(160, int(m_view->width() * 0.8));

    // PRIMA DI MISURARE: togliere i vincoli della chiamata PRECEDENTE. Le
    // setFixedWidth/setFixedHeight qui sotto fissano minimum == maximum, e
    // restano appiccicati al widget: al messaggio successivo sizeHint() e
    // heightForWidth() venivano calcolati su una label ancora bloccata alle
    // dimensioni del messaggio VECCHIO, quindi un hint corto ereditava il
    // riquadro di uno lungo (enorme rispetto al suo testo).
    m_label->setMinimumSize(0, 0);
    m_label->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

    // LARGHEZZA. sizeHint() di una QLabel con wordWrap misura il testo su UNA
    // riga: e' il massimo utile, e va limitato allo spazio disponibile.
    // (Un tentativo di dimensionare sulla RIGA PIU' LUNGA con QFontMetrics e'
    // stato scartato: sui desktop e su iPad produceva riquadri enormi.)
    const int w = qMin(maxW, m_label->sizeHint().width());
    m_label->setFixedWidth(w);

    // ALTEZZA. adjustSize() da solo NON basta con il wordWrap: a larghezza
    // fissata continua a usare l'altezza del sizeHint (una riga o poco piu'),
    // quindi un messaggio lungo veniva TAGLIATO -- si vedeva su iPhone, dove
    // maxW e' stretto e gli hint multi-riga ("Sliders\nE: ...\nF: ...")
    // occupano parecchie righe. heightForWidth() e' il meccanismo previsto da
    // Qt proprio per questo: chiede alla label quanto e' alta ALLA LARGHEZZA
    // che le abbiamo imposto, contenuti riflussati.
    // heightForWidth() vuole la larghezza TOTALE del widget e restituisce
    // un'altezza che COMPRENDE GIA' i margini del contenuto (il padding del
    // foglio di stile). Sottrarli in ingresso e riaggiungerli in uscita --
    // come faceva la prima versione di questo fix -- li contava DUE volte, e
    // il riquadro mostrava una fascia vuota sopra e sotto il testo.
    int h = m_label->heightForWidth(w);
    if (h <= 0) h = m_label->sizeHint().height();   // wordWrap disattivo

    // Non superare l'altezza della scena: meglio un messaggio fitto in alto che
    // uno che esce dal fondo (e sul telefono in orizzontale ci sta poco).
    const int maxH = qMax(48, m_view->height() - 2 * margin);
    m_label->setFixedHeight(qMin(h, maxH));

    // In alto a sinistra: non copre l'oggetto, che sta al centro della scena.
    m_label->move(margin, margin);
}
