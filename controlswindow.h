// controlswindow.h - Le finestre dei comandi, dal menu Help (richiesta
// dell'utente, 2026-11-26): "Mouse & Touch" e "Game Controller".
//
// Piu' disegni che testo: le pagine (docs/controls_*.html, nel qrc) sono
// tabelle di figure, e le figure le disegna il codice (paintControlsPictogram),
// nitide a ogni densita' di schermo e senza file d'immagine da tenere allineati.
// Una pagina le chiama per nome e lato in punti: <img src="pict:rotate@56"
// width="56" height="56">.
//
// Sostituiscono la descrizione dei comandi nella guida, che ora rimanda qui:
// il testo dei comandi vive in un posto solo.
#pragma once

#include <QDialog>
#include <QImage>

class QTextBrowser;

class ControlsWindow : public QDialog
{
public:
    enum Kind { MouseTouch = 0, GameController = 1 };

    // Apre la finestra di quel tipo, o riporta in primo piano quella gia'
    // aperta. Sul desktop non blocca l'app: si tiene accanto alla vista mentre
    // si provano i comandi. Su iPad, iPhone e Android e' a tutto schermo, come
    // la documentazione.
    static ControlsWindow *open(QWidget *parent, Kind kind);
    // La finestra aperta di quel tipo, o nullptr (per i test).
    static ControlsWindow *current(Kind kind);
    static QString title(Kind kind);
    static QString pageUrl(Kind kind);

    QTextBrowser *browser() const { return m_browser; }
    // I nomi delle figure che la pagina ha chiesto e che non esistono (per i test).
    QStringList missingPictograms() const;

private:
    ControlsWindow(QWidget *parent, Kind kind);
    QTextBrowser *m_browser = nullptr;
};

// Una figura per nome, quadrata di `side` punti (o di `size`, per i disegni
// larghi come il controller), a densita' `dpr`. Immagine nulla se il nome non
// esiste.
QImage paintControlsPictogram(const QString &name, int side, qreal dpr);
QImage paintControlsPictogram(const QString &name, const QSize &size, qreal dpr);
