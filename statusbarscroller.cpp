#include "statusbarscroller.h"

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QScroller>
#include <QScrollerProperties>
#include <QStatusBar>
#include <QSizeGrip>
#include <QWidget>

#include <algorithm>

StatusBarScroller::StatusBarScroller(QObject* parent)
    : QObject(parent)
{
}

void StatusBarScroller::install(QStatusBar* bar, QWidget* firstPermanent,
                                const QList<QWidget*>& keepOutside)
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
    // L'ordine NON si puo' leggere da bar->layout(): QStatusBar avvolge ogni
    // widget in un item privato che non e' un QWidgetItem, quindi itemAt(i)
    // ->widget() torna SEMPRE nullptr e la raccolta resta vuota (sintomo: il
    // nastro non viene creato affatto e la barra non scorre piu'). Verificato
    // con una sonda: layout()->count() vede gli item ma nessun widget.
    // Si parte quindi da children() e si ordina per posizione x reale, che
    // riflette gia' la divisione fra widget normali e permanent.
    QList<QWidget*> items;
    const QList<QObject*> children = bar->children();
    for (QObject* child : children) {
        QWidget* w = qobject_cast<QWidget*>(child);
        if (!w || qobject_cast<QSizeGrip*>(w)) continue;
        if (keepOutside.contains(w)) continue;   // resta figlio della barra
        items.append(w);
    }
    if (items.isEmpty()) return;

    // La barra deve aver gia' disposto i figli perche' le x siano significative.
    bar->ensurePolished();
    QCoreApplication::sendPostedEvents(bar, QEvent::LayoutRequest);
    if (QLayout* bl = bar->layout()) bl->activate();
    std::stable_sort(items.begin(), items.end(),
                     [](const QWidget* a, const QWidget* b) {
                         return a->x() < b->x();
                     });

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
        // Lo stacco fra comandi scena e tasti dock: uno stretch al posto della
        // separazione che prima davano addWidget vs addPermanentWidget.
        if (w == firstPermanent) stripLayout->addStretch(1);
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
    // Larghezza minima, altrimenti durante la registrazione il nastro viene
    // schiacciato: la barra di avanzamento (150px fissi) e l'avviso "Generating
    // MP4..." hanno dimensione fissa, il nastro e' Expanding e cede tutto lui.
    // Misurato senza minimo, su una barra da 420px: il nastro scendeva a 88px,
    // una finestrella da due tasti. Il minimo e' tenuto basso di proposito: piu'
    // in alto (240 provato) su un telefono la somma nastro + 150px di progress
    // bar + avviso eccede la barra e i widget si SOVRAPPONGONO. Cosi' invece
    // durante la registrazione il nastro si accorcia ma resta scorrevole - tutti
    // i tasti restano raggiungibili - e gli indicatori hanno il loro posto.
    area->setMinimumWidth(120);
    // Il viewport deve essere alto quanto il nastro, altrimenti la status bar
    // taglia i tasti invece di scorrerli.
    area->setFixedHeight(strip->sizeHint().height());
    area->viewport()->setAutoFillBackground(false);
    strip->setAutoFillBackground(false);

    bar->addWidget(area, 1);

    // I widget esclusi vanno RIAGGIUNTI dopo il nastro: erano gia' nella barra,
    // ma il nastro e' stato accodato dopo di loro e senza questo giro la barra
    // di avanzamento comparirebbe a sinistra dei tasti.
    //
    // addWidget, NON addPermanentWidget: il nastro ha stretch 1 e si prende lo
    // spazio residuo, quindi un widget accodato dopo di lui nello stesso gruppo
    // si posiziona SUBITO DOPO IL NASTRO - cioe' dopo REC, dove la barra di
    // avanzamento e l'avviso stavano prima del nastro. Come permanent invece
    // finivano all'estrema destra, dopo Library.
    for (QWidget* w : keepOutside) {
        if (!w || w->parentWidget() != bar) continue;
        const bool wasVisible = !w->isHidden();
        bar->removeWidget(w);
        bar->addWidget(w);
        w->setVisible(wasVisible);
    }

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
    const int natural  = m_strip->sizeHint().width();
    const int viewport = m_area->viewport()->width();
    m_strip->resize(qMax(natural, viewport), m_strip->height());
}

bool StatusBarScroller::eventFilter(QObject* watched, QEvent* event)
{
    Q_UNUSED(watched);

    // Istanza sizer: si occupa solo di riadattare il nastro al viewport.
    if (m_strip) {
        if (event->type() == QEvent::Resize) syncStripWidth();
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
