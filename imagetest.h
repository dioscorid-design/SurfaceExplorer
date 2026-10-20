#ifndef IMAGETEST_H
#define IMAGETEST_H

#include <QObject>
#include <QStringList>

class MainWindow;

// TEST DELLE IMMAGINI: fotografa ogni superficie e ogni record di una
// libreria a tempo FERMO e, se gli si da' una serie di riferimento, confronta
// pixel per pixel. E' la rete delle tappe che toccano il renderer (2.0:
// GLWidget diviso in vista + oggetti): la modalita' Superficie deve restare
// identica, e scenari e andata e ritorno guardano lo stato, non i pixel.
//
// Per ogni preset: load con le stesse funzioni del click nell'albero, poi
// tutto fermo con le chiamate del recorder all'avvio del REC (path,
// rotazioni, flusso geodetico, suono; gli orologi li avanza solo chi li
// guida da fuori, cioe' nessuno), assestamento, foto a risoluzione fissa
// (beginHiResCapture: non dipende dalla finestra) e seconda foto dopo una
// pausa. Se le due foto differiscono il preset non e' fermo e il confronto
// non vale (riga "instabile"); se un moto parte DOPO il fermo (avvio
// differito del load) la posa dipende dal tempo reale (riga "differito").
//
//   SurfaceExplorer --image-test <radice preset> <cartella uscita>
//                   [--compare <cartella di un giro precedente>]
//                   [--filter <testo>[|<testo>...]] [--settle <ms>]
//                   [--size <L>x<A>]
//
// Scrive images/<preset>.png, image-report.txt e, col confronto, diff/<preset>.png
// (i pixel diversi in rosso sull'immagine attenuata). Col confronto esce 0 se
// ogni immagine stabile coincide col riferimento, 1 altrimenti.
class ImageTest : public QObject
{
    Q_OBJECT
public:
    static bool requested(const QStringList &args);
    static void start(MainWindow *mw, const QStringList &args);

private:
    struct Entry {
        QString path;       // assoluto
        QString rel;        // relativo alla radice
        bool isRecord = false;
    };

    ImageTest(MainWindow *mw, const QStringList &args);
    void run();
    void collect();
    void shoot(const Entry &e);
    void freeze();
    bool anyMotionRunning() const;
    void wait(int ms);
    void finish();

    MainWindow *m_mw;
    QString m_root;
    QString m_outDir;
    QString m_compareDir;
    QString m_filter;
    int m_settleMs = 1200;
    int m_width = 640;
    int m_height = 400;
    bool m_valid = false;

    QList<Entry> m_entries;
    QStringList m_lines;
    QStringList m_popups;   // popup chiusi durante il preset corrente
    int m_same = 0, m_different = 0, m_unstable = 0, m_deferred = 0, m_missingRef = 0;
};

#endif // IMAGETEST_H
