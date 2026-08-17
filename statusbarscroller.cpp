#include "statusbarscroller.h"

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLayout>
#include <QMetaObject>
#include <QMouseEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QScroller>
#include <QScrollerProperties>
#include <QStatusBar>
#include <QSizeGrip>
#include <QWidget>

namespace {
// Stacco fisso fra comandi di scena e tasti dock. Tenuto stretto: su un telefono
// ogni pixel del nastro e' spazio di lettura, e basta poco perche' la fila si
// legga come due gruppi invece che uno.
constexpr int kGroupGap = 20;
}

StatusBarScroller::StatusBarScroller(QObject* parent)
    : QObject(parent)
{
}

void StatusBarScroller::install(QStatusBar* bar, QWidget* firstPermanent,
                                const QList<QWidget*>& indicators)
{
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    if (!bar) return;

    // I tasti nell'ordine in cui la barra li MOSTRA. Serve l'ordine visivo, non
    // quello di costruzione: la barra di avanzamento e' creata prima dei tasti
    // dock ma va mostrata dopo, perche' quelli sono addPermanentWidget e la
    // barra li tiene in un gruppo separato ancorato a destra. Prendendo l'ordine
    // da children() (= costruzione) la zona scorrevole finiva in mezzo ai tasti
    // dock invece che dopo REC.
    //
    // L'ordine NON si puo' leggere da bar->layout(): il layout privato di
    // QStatusBar non espone i suoi widget come QLayoutItem con widget(), quindi
    // la raccolta resta VUOTA, items.isEmpty() scatta e install() esce senza
    // fare nulla - la barra torna quella nativa e non scorre affatto. Misurato
    // con una sonda: zero widget utili raccolti (era il bug "non scorre piu'").
    // Si parte quindi da children() e si spezza su firstPermanent, che segna
    // dove iniziano i tasti dock: prima i comandi di scena, poi i dock. NON si
    // ordina per posizione x: install() gira nel costruttore, a finestra non
    // ancora mostrata, dove le x valgono zero e l'ordinamento lascerebbe intatto
    // proprio l'ordine di costruzione che si vuole correggere. Il marcatore
    // invece e' deterministico e non dipende dal layout.
    QList<QWidget*> normals, permanents;
    bool afterMarker = false;
    const QList<QObject*> children = bar->children();
    for (QObject* child : children) {
        QWidget* w = qobject_cast<QWidget*>(child);
        if (!w || qobject_cast<QSizeGrip*>(w)) continue;
        if (indicators.contains(w)) continue;   // riaggiunti sotto, in coda ai normali
        if (w == firstPermanent) afterMarker = true;
        (afterMarker ? permanents : normals).append(w);
    }
    // Gli indicatori chiudono il gruppo dei comandi di scena: entrano DOPO REC e
    // PRIMA di Equations. Nell'ordine di children() la barra di avanzamento
    // compare prima dei tasti dock ma il label puo' trovarsi altrove, e in ogni
    // caso il punto giusto e' quello logico (fine dei comandi scena), non quello
    // di costruzione: quindi si estraggono sopra e si riaccodano qui.
    for (QWidget* w : indicators) {
        if (w && w->parentWidget() == bar) normals.append(w);
    }
    QList<QWidget*> items = normals;
    items.append(permanents);
    if (items.isEmpty()) return;

    QWidget* strip = new QWidget(bar);
    QHBoxLayout* stripLayout = new QHBoxLayout(strip);
    stripLayout->setContentsMargins(0, 0, 0, 0);
    stripLayout->setSpacing(0);

    // removeWidget() stacca dal layout della barra ma NASCONDE il widget, quindi
    // serve un show() o il nastro resta vuoto. Va pero' rimessa la visibilita'
    // che il widget aveva PRIMA: m_renderProgress nasce nascosto e si mostra solo
    // durante il rendering, e uno show() indiscriminato lo inchioda a "0%" in
    // barra anche a riposo.
    for (QWidget* w : items) {
        const bool wasVisible = !w->isHidden();
        bar->removeWidget(w);
        // Lo stacco fra comandi scena e tasti dock, in due pezzi:
        // - uno spazio FISSO, che sopravvive anche sul telefono. Lo stretch da
        //   solo non basta: quando i tasti eccedono il viewport non c'e' spazio
        //   da distribuire, lo stretch vale zero e REC finisce appiccicato a
        //   Equations - i due gruppi si leggevano come una fila sola.
        // - lo stretch, che sugli schermi larghi (iPad, desktop) riapre lo stacco
        //   pieno di prima, ancorando i tasti dock a destra.
        if (w == firstPermanent) {
            stripLayout->addSpacing(kGroupGap);
            stripLayout->addStretch(1);
        }
        stripLayout->addWidget(w);
        w->setVisible(wasVisible);
    }

    strip->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    strip->adjustSize();

    QScrollArea* area = new QScrollArea(bar);
    area->setWidget(strip);
    // widgetResizable RESTA FALSE: con true il nastro viene stirato fino a
    // riempire il viewport e il layout distribuisce l'eccesso sui tasti, che
    // diventano piu' larghi di prima. La larghezza la gestiamo noi qui sotto,
    // che e' l'unico modo di avere insieme tasti di dimensione naturale e stacco
    // fra i due gruppi.
    area->setWidgetResizable(false);
    area->setFrameShape(QFrame::NoFrame);
    // Niente scrollbar: lo scorrimento e' solo a dito, come chiesto. La barra
    // orizzontale mangerebbe altezza a una status bar gia' bassa.
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    area->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    area->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // Il nastro e' l'unico widget della barra: si prende tutta la larghezza e
    // non ha piu' nulla con cui contenderla (la barra di avanzamento sta DENTRO
    // di lui). Il minimo resta come rete di sicurezza per barre strettissime.
    area->setMinimumWidth(120);
    // Il viewport deve essere alto quanto il nastro, altrimenti la status bar
    // taglia i tasti invece di scorrerli.
    area->setFixedHeight(strip->sizeHint().height());
    area->viewport()->setAutoFillBackground(false);
    strip->setAutoFillBackground(false);

    bar->addWidget(area, 1);

    // Il nastro cambia larghezza naturale quando la barra di avanzamento si
    // mostra o si nasconde (150px che entrano ed escono dal layout). Senza
    // risincronizzare, sugli schermi larghi il nastro resterebbe alla vecchia
    // larghezza: a fine registrazione i tasti dock non tornerebbero ancorati a
    // destra. Il Resize del viewport non scatta - la barra non cambia
    // dimensione - quindi serve seguire gli indicatori stessi.
    //
    // LARGHEZZA DEL NASTRO, i due regimi:
    // - i tasti NON ci stanno (telefono): il nastro tiene la sua larghezza
    //   naturale, eccede il viewport ed e' quell'eccedenza a permettere lo
    //   scorrimento. Lo stretch, non avendo spazio da distribuire, vale zero e i
    //   due gruppi restano attaccati - giusto cosi', qui lo spazio e' prezioso.
    // - i tasti CI STANNO (iPad, desktop in finestra larga): il nastro viene
    //   portato alla larghezza del viewport, cosi' lo stretch ha spazio da dare
    //   e riapre lo stacco fra REC ed Equations, com'era prima del nastro.
    //   Senza questo ramo i tasti si accalcano tutti a sinistra.
    // Va rifatto a ogni resize del viewport: rotazione schermo e split view su
    // iPad passano da un regime all'altro.
    StatusBarScroller* sizer = new StatusBarScroller(area);
    sizer->m_strip = strip;
    sizer->m_area  = area;
    area->viewport()->installEventFilter(sizer);
    for (QWidget* w : indicators) {
        if (w) w->installEventFilter(sizer);
    }
    sizer->syncStripWidth();

    StatusBarScroller* filter = new StatusBarScroller(area);
    // Il filtro va sui TASTI, non sul viewport: e' li' che arriva il release che
    // farebbe scattare il click a fine trascinamento.
    const QList<QWidget*> buttons = strip->findChildren<QWidget*>();
    for (QWidget* w : buttons) w->installEventFilter(filter);
    strip->installEventFilter(filter);

    QScroller::grabGesture(area->viewport(), QScroller::TouchGesture);
    QScroller* scroller = QScroller::scroller(area->viewport());
    QScrollerProperties props = scroller->scrollerProperties();
    // Overshoot spento: il rimbalzo elastico su una fila di tasti alta ~40px
    // sembra un difetto, non un'animazione.
    props.setScrollMetric(QScrollerProperties::VerticalOvershootPolicy,
                          QScrollerProperties::OvershootAlwaysOff);
    props.setScrollMetric(QScrollerProperties::HorizontalOvershootPolicy,
                          QScrollerProperties::OvershootAlwaysOff);
    // Soglia bassa: il nastro deve seguire il dito subito, senza il ritardo che
    // farebbe sembrare la barra bloccata.
    props.setScrollMetric(QScrollerProperties::DragStartDistance, 0.005);
    props.setScrollMetric(QScrollerProperties::MousePressEventDelay, 0.0);
    scroller->setScrollerProperties(props);
#else
    Q_UNUSED(bar);
#endif
}

void StatusBarScroller::syncStripWidth()
{
    if (!m_strip || !m_area) return;
    // Il layout va riattivato a mano: il nastro ha larghezza imposta da qui, non
    // dal padre, quindi Qt non lo ricalcola da solo e sizeHint() resterebbe
    // quello di prima che la barra di avanzamento comparisse.
    if (QLayout* l = m_strip->layout()) l->activate();
    const int natural  = m_strip->sizeHint().width();
    const int viewport = m_area->viewport()->width();
    m_strip->resize(qMax(natural, viewport), m_strip->height());
}

bool StatusBarScroller::eventFilter(QObject* watched, QEvent* event)
{
    Q_UNUSED(watched);

    // Istanza sizer: si occupa solo di riadattare il nastro al viewport. Ascolta
    // il Resize del viewport (rotazione schermo, split view) e lo Show/Hide degli
    // indicatori, che entrando e uscendo cambiano la larghezza naturale del
    // nastro senza toccare quella della barra.
    if (m_strip) {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::Hide:
            // Lo Show arriva PRIMA che il layout del nastro abbia ricalcolato lo
            // sizeHint, quindi una sync immediata leggerebbe la larghezza vecchia
            // e la barra di avanzamento resterebbe schiacciata a zero. Rimandata
            // al giro d'eventi successivo, quando il layout e' aggiornato.
            QMetaObject::invokeMethod(this, [this]{ syncStripWidth(); },
                                      Qt::QueuedConnection);
            break;
        default:
            break;
        }
        return false;
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        QMouseEvent* me = static_cast<QMouseEvent*>(event);
        m_pressPos   = me->globalPosition().toPoint();
        m_pressValid = true;
        m_dragging   = false;
        break;
    }
    case QEvent::MouseMove: {
        if (!m_pressValid) break;
        QMouseEvent* me = static_cast<QMouseEvent*>(event);
        const int moved = (me->globalPosition().toPoint() - m_pressPos).manhattanLength();
        if (moved > QApplication::startDragDistance()) m_dragging = true;
        break;
    }
    case QEvent::MouseButtonRelease: {
        const bool wasDragging = m_dragging;
        m_pressValid = false;
        m_dragging   = false;
        // Il dito ha trascinato: consumiamo il release, cosi' il tasto sotto non
        // emette clicked(). Senza questo, scorrere la barra avvia START o REC.
        if (wasDragging) return true;
        break;
    }
    default:
        break;
    }
    return false;
}
