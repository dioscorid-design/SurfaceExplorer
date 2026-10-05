// scenehintoverlay.h - Il messaggio in sovrimpressione sulla scena (hint).
//
// La VISTA dei tre messaggi della scena (superficie, texture, sfondo): un
// riquadro figlio della vista 3D, in alto a sinistra, che si nasconde a timer o
// al tocco. I testi NON stanno qui: sono dati della scena (finiscono nei preset,
// li tiene MainWindow); questa classe li riceve gia' composti (compose) e li
// mostra. Figlio del GLWidget, NON entra nei video esportati, che sono composti
// dal render offscreen.
#pragma once

#include <QObject>
#include <QString>

class GLWidget;
class QLabel;
class QTimer;

class SceneHintOverlay : public QObject
{
    Q_OBJECT
public:
    explicit SceneHintOverlay(GLWidget *view, QObject *parent = nullptr);

    // I tre messaggi in quello che si vede: vuoti fuori, identici una volta
    // sola, ciascuno col suo ruolo nell'intestazione ("Texture sliders").
    static QString compose(const QString &surface, const QString &texture,
                           const QString &background);

    // Mostra `text` per `seconds` secondi (0 o meno: finche' qualcuno non lo
    // nasconde). Testo vuoto = nasconde.
    void show(const QString &text, float seconds);
    void hide();

private:
    void ensureWidget();
    void reposition();
    bool eventFilter(QObject *obj, QEvent *ev) override;

    GLWidget *m_view = nullptr;
    QLabel *m_label = nullptr;
    QTimer *m_timer = nullptr;
    bool m_swallowRelease = false;
};
