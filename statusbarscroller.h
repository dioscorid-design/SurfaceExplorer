#ifndef STATUSBARSCROLLER_H
#define STATUSBARSCROLLER_H

#include <QObject>
#include <QPoint>
#include <QList>

class QStatusBar;
class QWidget;
class QScrollArea;

// Status bar scorrevole col dito, come le barre di navigazione dei sistemi
// mobile: nessuna scrollbar, si trascina il nastro dei tasti.
//
// PERCHE' SERVE: QStatusBar non e' scrollabile (ha un layout proprio, nessun
// viewport). Con undici tasti in fila gli ultimi - i dock, Library per ultimo -
// finivano fuori schermo sui telefoni, e non erano piu' RAGGIUNGIBILI: non era
// un problema estetico. L'unica via e' mettere i tasti dentro una QScrollArea
// orizzontale e infilare QUELLA nella status bar come widget unico.
//
// LA TRAPPOLA (gia' costata un revert sul dock Library): QScroller in modalita'
// TouchGesture consegna comunque il press/release ai figli mentre si trascina,
// quindi scorrendo la barra si fa partire START o REC. Su una lista si traduce
// in una selezione sbagliata; qui avvierebbe una registrazione. Per questo il
// filtro consuma il release quando il dito si e' mosso oltre startDragDistance:
// trascinare scorre e basta, il click scatta solo se il dito e' rimasto fermo.
class StatusBarScroller : public QObject
{
    Q_OBJECT
public:
    // Sposta i widget gia' aggiunti a `bar` dentro un nastro scorrevole.
    // Da chiamare DOPO l'ultimo addWidget/addPermanentWidget: i widget vengono
    // riparentati nell'ordine in cui la barra li espone (prima i normali, poi i
    // permanent), quindi l'ordine visivo di oggi e' preservato.
    //
    // `firstPermanent` = primo widget del gruppo ancorato a destra (il tasto
    // Equations). Prima del nastro la spaziatura fra comandi scena e tasti dock
    // veniva dal fatto che gli uni erano addWidget e gli altri
    // addPermanentWidget; dentro un QHBoxLayout quella distinzione non esiste
    // piu' e i tasti si accalcano tutti a sinistra sugli schermi larghi (iPad),
    // dove nulla eccede e quindi non c'e' scorrimento. Qui il layout riceve uno
    // stretch in quel punto, che riproduce lo stacco di prima. children() da
    // solo non basta a ricavarlo: non dice quali widget fossero permanent.
    // Passare nullptr = nessuno stacco, fila unica compatta.
    //
    // `keepOutside` = widget che NON entrano nel nastro e restano figli diretti
    // della barra. Serve per la barra di avanzamento del rendering: e' un
    // indicatore transitorio, non un comando da scorrere, e dentro al nastro i
    // suoi 150px fissi comparivano all'avvio della registrazione allargando il
    // nastro e spingendo i tasti fuori dal viewport - le scritte risultavano
    // tagliate. Fuori dal nastro si prende il suo spazio dalla barra, che e'
    // quello che faceva prima.
    // No-op su desktop: li' i tasti ci stanno e la barra resta quella nativa.
    static void install(QStatusBar* bar,
                        QWidget* firstPermanent = nullptr,
                        const QList<QWidget*>& keepOutside = {});

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    explicit StatusBarScroller(QObject* parent);

    // Allarga il nastro fino al viewport quando i tasti ci stanno, lo lascia
    // alla larghezza naturale quando eccedono (ed e' allora che si scorre).
    void syncStripWidth();

    QPoint m_pressPos;
    bool   m_pressValid  = false;
    bool   m_dragging    = false;

    // Valorizzati solo sull'istanza che fa da sizer, nulli su quella che filtra
    // i click.
    QWidget*     m_strip = nullptr;
    QScrollArea* m_area  = nullptr;
};

#endif // STATUSBARSCROLLER_H
