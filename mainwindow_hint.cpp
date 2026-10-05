// mainwindow_hint.cpp - MainWindow: i messaggi in sovrimpressione (hint) della
// scena. La vista e' SceneHintOverlay; qui i tre testi, che sono della scena.
// Parte della classe MainWindow divisa per argomento (mainwindow_p.h).
#include "mainwindow_p.h"
#include "scenehintoverlay.h"


 // --- UI State & Graphics ---

// I messaggi in sovrimpressione, uniti in quello che si vede a schermo.
//
// Sono TENUTI SEPARATI in memoria e nei file (m_currentHintText nel preset di
// superficie/record, m_currentTextureHintText in quello della texture,
// m_currentBgTextureHintText nel blocco "background" del record) perche'
// descrivono cose diverse e possono nominare COSTANTI diverse: la texture
// d'origine dice la sua ("Slider A: relief frequency"), e un record che la
// riusa puo' aver spostato quel rilievo su un'altra costante per non
// confliggere con la superficie, quindi deve poter dire "Slider F".
//
// A schermo pero' vanno mostrati ENTRAMBI: prima si sostituivano a vicenda --
// showSceneHint scriveva l'overlay col solo messaggio della scena -- e vinceva
// l'ultimo che parlava (l'ordine di caricamento decideva quale). Un record con
// costanti sia sulla superficie sia sulla texture ne mostrava percio' solo una,
// lasciando l'altro slider acceso senza spiegazione.
//
// Ciascuno porta il suo ruolo nell'intestazione ("Texture sliders"), anche
// quando e' il solo (vedi composedHintText).
// Ridisegna l'overlay dalla coppia corrente di messaggi, SENZA toccarli.
// Serve a chi cambia uno solo dei due (il caricamento di una texture): passare
// da showSceneHint travaserebbe il testo della texture in m_currentHintText,
// che e' il messaggio della SCENA e finisce nel preset di superficie/record.
void MainWindow::refreshSceneHint(float seconds)
{
    if (!ui->glWidget) return;

    if (composedHintText().isEmpty()) { hideSceneHint(); return; }

    // showSceneHint costruisce l'overlay e riavvia il timer: la si richiama con
    // il messaggio della SCENA invariato, cosi' nessuna variabile cambia valore
    // e la composizione la rifa' lei leggendo entrambe.
    showSceneHint(m_currentHintText, seconds);
}

QString MainWindow::composedHintText() const
{
    return SceneHintOverlay::compose(m_currentHintText, m_currentTextureHintText,
                                     m_currentBgTextureHintText);
}

void MainWindow::showTextureHintsOnly(float seconds)
{
    if (m_currentTextureHintText.trimmed().isEmpty()
        && m_currentBgTextureHintText.trimmed().isEmpty()) return;
    // NB: non si passa da showSceneHint col testo delle texture: SCRIVE
    // m_currentHintText, il messaggio della SCENA, che il preset riscrive -- il
    // testo della texture diventerebbe quello della superficie al primo Save.
    const QString savedScene = m_currentHintText;
    m_currentHintText.clear();               // solo per questa chiamata
    refreshSceneHint(seconds);
    m_currentHintText = savedScene;
}

void MainWindow::showSceneHint(const QString &text, float seconds)
{
    // Memorizzato anche se non c'e' scena da decorare: e' il messaggio del
    // record corrente e va riscritto tale e quale a un eventuale risalvataggio.
    m_currentHintText = text.trimmed();
    m_currentHintSeconds = seconds;

    if (!ui->glWidget) return;
    if (!m_hintOverlay) m_hintOverlay = new SceneHintOverlay(ui->glWidget, this);
    m_hintOverlay->show(composedHintText(), seconds);
}

void MainWindow::hideSceneHint()
{
    if (m_hintOverlay) m_hintOverlay->hide();
}
