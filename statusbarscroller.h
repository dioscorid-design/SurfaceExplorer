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
    // dove nulla eccede e quindi non c'e' scorrimento. Qui il layout riceve in
    // quel punto uno spazio fisso PIU' uno stretch: il fisso tiene i due gruppi
    // distinti anche sul telefono (dove lo stretch, senza spazio da distribuire,
    // vale zero), lo stretch riapre lo stacco pieno sugli schermi larghi.
    // children() da solo non basta a ricavare il punto: non dice quali widget
    // fossero permanent. Passare nullptr = nessuno stacco, fila unica compatta.
    //
    // `indicators` = widget che chiudono il gruppo dei comandi di scena: entrano
    // nel nastro DOPO l'ultimo comando (REC) e PRIMA di firstPermanent,
    // qualunque sia il loro posto nell'ordine di costruzione. Serve per la barra
    // di avanzamento del rendering, che appartiene a REC e deve comparire
    // accanto a lui. Tenuta fuori dal nastro finiva in coda alla barra, cioe'
    // all'estrema destra dopo Library: il nastro ha stretch 1 e si mangia lo
    // spazio residuo, quindi "subito dopo il nastro" e' visivamente in fondo,
    // non dopo REC. Dentro il nastro invece la posizione e' quella giusta, e
    // l'allargamento che causa (150px) non e' piu' un problema: il nastro deve
    // eccedere il viewport, e' proprio quello che lo rende scorrevole.
    // No-op su desktop: li' i tasti ci stanno e la barra resta quella nativa.
    static void install(QStatusBar* bar,
                        QWidget* firstPermanent = nullptr,
                        const QList<QWidget*>& indicators = {});

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
