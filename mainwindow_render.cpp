// mainwindow_render.cpp - MainWindow: resa -- Base/Phong/Wireframe, trasparenza, Shell/Solid
// e marcher, scena vuota, aspetto per-mesh e ambito All/Mesh.
// Parte della classe MainWindow divisa per argomento (mainwindow_p.h).
#include "mainwindow_p.h"


// UNICO punto da cui si mostra un errore di compilazione all'utente (~18
// chiamanti). L'esito del Run NON si marca qui: lo deduce RunOutcomeGuard dal
// contatore di InputValidator, che intercetta anche gli errori dei validatori
// -- i quali fermano l'equazione prima di compilare e non passerebbero mai da
// questa funzione.
void MainWindow::showShaderError(const QString &title, const QString &errorLog)
{
    InputValidator::showShaderCompilationError(this, title, errorLog);
}

// Allinea etichette e range degli slider che cambiano significato col modo:
// - S: "s=" (parametrico, range [-1000,1000]) vs "Step Relax" (Ray Marching, min 0)
// - Steps: "Steps=" (parametrico) vs "Ray Steps=" (Ray Marching)
// Il gestore di tabModeSelector::currentChanged fa già questo, ma durante il load di
// un preset il tab viene cambiato a SEGNALI BLOCCATI (per non innescare il reset
// distruttivo), quindi va richiamato esplicitamente: senza, caricando una superficie
// Ray Marching all'avvio (tab Parametric mai cambiato a mano) restano "s="/"Steps=".
// Tocca SOLO etichette e min dello slider, mai i VALORI (li imposta il preset).
void MainWindow::applyModeDependentStepUI(bool isImplicit)
{
    bool sB = ui->sSlider->blockSignals(true);
    if (isImplicit) {
        ui->lblS->setText("Step Relax");
        ui->lblSteps->setText("Ray Steps=");
        if (ui->sSlider->minimum() < 0) ui->sSlider->setMinimum(0);
    } else {
        ui->lblS->setText("s=");
        ui->lblSteps->setText("Steps=");
        if (ui->sSlider->minimum() == 0) ui->sSlider->setMinimum(-1000);
    }
    ui->sSlider->blockSignals(sB);
}

// Sincronizza lo stato dello slider di trasparenza con il condizionamento del campo
// implicito corrente. I campi a PRODOTTO (es. preset "Chain") fanno sparire la
// superficie con alpha<1 (crossing fantasma nel ramo trasparente): il motore forza
// gia' opaco (m_uboData.alpha=1).
//
// LOGICA (scelta utente): al caricamento lo slider resta ABILITATO e NON compare il
// popup. Solo quando l'utente TOCCA lo slider su un campo a prodotto scatta il popup e
// lo slider si blocca (handler valueChanged -> onAlphaSliderMovedIllCheck).
//
// newSurface=true  -> chiamata dopo un COMMIT di equazione implicita (nuova superficie):
//   riparte "fresco", slider abilitato e popup riarmato, cosi' l'utente puo' toccare di
//   nuovo lo slider su questa superficie.
// newSurface=false -> chiamata da updateRenderState (eventi UI vari, NON cambi superficie):
//   aggiorna SOLO il tooltip, senza toccare enabled/guardia (altrimenti un toggle
//   qualsiasi riabiliterebbe uno slider appena bloccato dall'utente sulla stessa superficie).
void MainWindow::syncImplicitAlphaSlider(bool isImplicitMode, bool newSurface)
{
    bool illImplicit = isImplicitMode && ui->glWidget && ui->glWidget->isImplicitIllConditioned();
    // SOLO ANDROID (implicitTransparencyMayDegrade e' sempre false altrove): superficie
    // la cui trasparenza potrebbe degradare (Gyroid, script RM). NON blocca: lo slider
    // resta usabile, mostriamo solo un avviso.
    bool warnImplicit = isImplicitMode && ui->glWidget && ui->glWidget->implicitTransparencyMayDegrade();

    if (newSurface) {
        if (!ui->alphaSlider->isEnabled()) ui->alphaSlider->setEnabled(true);
        m_implicitAlphaDisabled = false;
        m_implicitWarnShown = false;    // riarma il popup di avviso per la nuova superficie
        m_alphaHeavyWarnShown = false;  // riarma anche la conferma "scena pesante"
        m_alphaHeavyDeclined = false;   // il "no" valeva per la superficie precedente
        // Anche la guardia displacement-su-trasparenza riparte pulita: il suo
        // "gia' chiesto" era legato alla superficie precedente, e lo stesso
        // displacement su una superficie nuova e' un caso nuovo da valutare.
        // NON mentre la guardia e' in corso: nel ramo libreria questa funzione e'
        // chiamata (newSurface=true) POCHE RIGHE DOPO la guardia, sullo stesso
        // commit. Azzerare li' avrebbe buttato via il "gia' chiesto" appena
        // scritto, e il commit seguente avrebbe riaperto il box: e' la raffica
        // senza fine da cui veniamo.
        if (!m_heavyTexGuardActive) m_heavyTexGuardAskedFor.clear();
    }

    if (illImplicit) {
        ui->alphaSlider->setToolTip(
            tr("Transparency is unavailable for this surface: it is defined by a "
               "product of several factors, so true transparency cannot be computed "
               "reliably. Moving the slider will keep the surface opaque."));
    } else if (warnImplicit) {
        ui->alphaSlider->setToolTip(
            tr("Transparency may not render correctly on this surface on this device: "
               "it has many layers along each ray. The slider still works, but the "
               "surface may look clipped."));
    } else {
        ui->alphaSlider->setToolTip(QString());
    }
}

// L'utente ha abbassato lo slider trasparenza su un campo implicito a PRODOTTO:
// ripristina l'opacita' piena, mostra il popup UNA VOLTA e blocca lo slider. Chiamata
// dall'handler valueChanged solo per interazioni utente (non set programmatici) e con
// il campo gia' verificato come a prodotto.
void MainWindow::onAlphaSliderMovedIllCheck(int /*value*/)
{
    // Ripristina opacita' piena. La rientranza in valueChanged porta value==100, che
    // non rientra nel ramo dell'intercetto (gated da value < 100).
    ui->alphaSlider->setValue(100);

    if (!m_implicitAlphaDisabled) {
        m_implicitAlphaDisabled = true;   // guardia PRIMA del popup modale (anti-doppio)
        ui->alphaSlider->setEnabled(false);
        // Testo breve in setText, dettaglio in informativeText (che va a capo):
        // il testo principale non viene mandato a capo e allargherebbe il box
        // oltre la larghezza dello schermo.
        QMessageBox box(this);
        box.setIcon(QMessageBox::Information);
        box.setWindowTitle(tr("Transparency unavailable"));
        box.setText(tr("This surface will stay opaque."));
        box.setInformativeText(
            tr("It is defined by a product of several factors "
               "(e.g. linked tori). True transparency cannot be computed "
               "reliably on such fields — with transparency the surface would "
               "disappear instead of turning translucent.\n\n"
               "The transparency slider is disabled."));
        box.exec();
    }
}

// SOLO ANDROID. L'utente ha abbassato lo slider su una superficie implicita la cui
// trasparenza puo' degradare (Gyroid, script RM): NON blocchiamo e NON ripristiniamo
// l'opacita' (lo slider agisce davvero, l'utente vede l'effetto reale). Mostriamo solo
// un popup di avviso UNA VOLTA per superficie. Chiamata dall'handler valueChanged solo
// per interazioni utente (non set programmatici), con warn gia' verificato.
void MainWindow::onAlphaSliderMovedWarnCheck()
{
    if (m_implicitWarnShown) return;
    m_implicitWarnShown = true;   // guardia PRIMA del popup modale (anti-doppio)
    // Vedi sopra: il testo principale non va a capo, il corpo sta
    // nell'informativeText o il box invade tutto lo schermo.
    QMessageBox box(this);
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(tr("Transparency may not render correctly"));
    box.setText(tr("Transparency may not render correctly here."));
    box.setInformativeText(
        tr("On this device, this surface is made of many layers stacked along each "
           "viewing ray, more than the renderer can blend here, so with "
           "transparency it may look clipped.\n\n"
           "The slider still works — this is just a heads-up."));
    box.exec();
}

// SOLO MOBILE (no-op su desktop, dove trasparenza+displacement regge). Chiamata
// DOPO un validateAndApplyImplicitShader riuscito, col displacement che era
// applicato PRIMA: se l'apply lo ha INTRODOTTO o CAMBIATO mentre la trasparenza
// e' attiva, l'alpha va a 1 in modo deterministico. Il displacement gira dentro
// map() e il ramo trasparente lo moltiplica per MAX_FACES x 3 x passi: il
// collasso arriva CON la texture, quindi la conferma misurata (EMA della scena
// PRECEDENTE, ancora leggera) non puo' prevederlo. L'animazione/record CONTINUA,
// opaca; riabbassando lo slider decide la conferma misurata coi dati veri.
// Corpo condiviso: porta la scena a opaca e zittisce il watchdog, poi avvisa.
// Il watchdog va zittito perche' opaco+texture-con-displacement su iPhone e'
// comunque al limite -> senza, certi frame sforavano e faceva scattare un SECONDO
// popup (sopra questo) o l'auto-stop, in modo intermittente. Il flag si riarma
// quando l'utente riabbassa lo slider (sliderPressed -> rearmPerformanceWarning):
// esattamente il "a meno che non riporti alpha<1".
void MainWindow::forceOpaqueForHeavyRM(const QString &message)
{
    // La trasparenza la toglie l'app: non e' lavoro dell'utente da salvare.
    AbsorbChangesGuard absorbOpaque(this);
    m_settingAlphaProgrammatic = true;
    ui->alphaSlider->setValue(100);
    m_settingAlphaProgrammatic = false;

    if (ui->glWidget) ui->glWidget->acknowledgePerformanceWarning();

    // Il QMessageBox modale fa girare l'event loop: un performanceWarning gia' in
    // coda (emesso sui primi frame trasparenti prima dell'ack) verrebbe
    // consegnato proprio ORA, aprendo il popup del watchdog SOPRA questo. Il flag
    // fa scartare quel segnale stantio nel gestore di performanceWarning per
    // tutta la durata del nostro box. Ripristinato dopo (nested guard-safe: il
    // valore precedente non e' mai true perche' questa funzione non e' rientrante).
    m_transparencyGuardActive = true;
    QMessageBox::information(this, tr("Transparency turned off"), message);
    m_transparencyGuardActive = false;
}

// SOLO MOBILE (no-op su desktop, dove trasparenza+displacement regge). Chiamata
// DOPO un validateAndApplyImplicitShader riuscito, col displacement che era
// applicato PRIMA: se l'apply lo ha INTRODOTTO o CAMBIATO mentre la trasparenza
// e' attiva, l'alpha va a 1 in modo deterministico. Il displacement gira dentro
// map() e il ramo trasparente lo moltiplica per MAX_FACES x 3 x passi: il
// collasso arriva CON la texture, quindi la conferma misurata (EMA della scena
// PRECEDENTE, ancora leggera) non puo' prevederlo. L'animazione/record CONTINUA,
// opaca; riabbassando lo slider decide la conferma misurata coi dati veri.
void MainWindow::guardTransparencyOnDisplacementApply(const QString &prevDisp)
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    if (!ui->glWidget) return;
    const QString newDisp = ui->glWidget->currentDisplacementCode().trimmed();
    if (newDisp.isEmpty() || newDisp == prevDisp.trimmed()) return; // nessun displacement nuovo
    if (ui->alphaSlider->value() >= 100) return;                    // trasparenza non attiva

    forceOpaqueForHeavyRM(
        tr("This texture carves relief into the surface (displacement). Combined "
           "with transparency it would overload the GPU on this device, so the "
           "surface has been set to fully opaque — the animation keeps "
           "running.\n\n"
           "Lower the transparency slider again to retry it: the app will check "
           "the measured load first."));
#else
    Q_UNUSED(prevDisp);
#endif
}

// SOLO MOBILE (no-op su desktop). Guardia POST-LOAD: chiamata a fine
// applyCommonData. Chiude la falla del "record/preset con alpha<1 nel JSON +
// displacement": al load alpha e displacement sono impostati programmaticamente,
// quindi ne' la conferma misurata ne' la guardia interattiva scattano, e la
// scena partirebbe trasparente+pesante lasciando come unica rete il watchdog
// tardivo. Se lo STATO FINALE e' RM + alpha<1 + displacement presente -> opaco +
// avviso, coerente con la guardia interattiva. NB: legge lo stato GIA' finale
// (slider + widget), quindi va invocata DOPO che il load ha applicato tutto.
void MainWindow::guardTransparencyOnImplicitLoad()
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    if (!ui->glWidget) return;
    const bool isImplicit = (implicitMode());
    if (!isImplicit) return;
    if (ui->alphaSlider->value() >= 100) return;                          // trasparenza non attiva
    if (ui->glWidget->currentDisplacementCode().trimmed().isEmpty()) return; // niente displacement

    forceOpaqueForHeavyRM(
        tr("This surface was saved with transparency and a relief texture "
           "(displacement). Together they would overload the GPU on this device, "
           "so it has been loaded fully opaque — the animation keeps "
           "running.\n\n"
           "Lower the transparency slider to try transparency: the app will "
           "check the measured load first."));
#endif
}

// TUTTE LE PIATTAFORME, desktop COMPRESO (a differenza delle due guardie qui
// sopra). Chiamata DOPO un validateAndApplyImplicitShader riuscito, quando la
// texture RM appena committata e' finita su una scena gia' trasparente. Dopo e
// non prima: cosi' agisce sullo stato davvero applicato, e un apply fallito (che
// lascia la scena precedente) non fa comparire nessun popup.
//
// PERCHE' SERVE ANCHE SU DESKTOP. Il caso che la motiva e' il Cross Section di
// default: loadCrossSectionDefaultSurface si porta da solo ad alpha 0.75 e Ray
// Steps 600 su un'equazione T^3 (quartica di quartiche in x,y,z,p). Quell'alpha
// e' scritto con m_settingAlphaProgrammatic, quindi NON passa dalla conferma
// misurata dell'handler valueChanged: quando arriva la texture la scena e' gia'
// nel ramo trasparente senza che nessuna guardia l'abbia mai vista. Il costo per
// pixel del ramo trasparente e' MAX_FACES(8 desktop) x 3 marchNextLayer x fino a
// MAX_LAYER_STEPS passi x 4 map(): una texture RM non banale dentro map() lo
// moltiplica ancora, e il salto da "fluido" a "GPU fault" non passa per una zona
// grigia misurabile. Il watchdog lavora su una EMA e richiede animazione in
// corso: arriva sempre DOPO il magenta, quando c'e' ancora un'app a cui parlare.
// Per questo la guardia e' PREVENTIVA (prima del commit) e non misurata.
//
// Fa le tre cose insieme, come da richiesta: ferma il moto, ripristina l'opaco e
// chiede se ripristinarli. Ritorna true se l'utente sceglie di proseguire
// trasparente (chi chiama ha gia' applicato la texture: qui si decide solo il
// destino di alpha e moto).
bool MainWindow::guardTransparencyOnHeavyTextureApply(const QString &newDispCode)
{
    if (!ui->glWidget || !ui->alphaSlider) return true;
    if (m_heavyTexGuardActive) return true;                       // box gia' aperto: non impilare
    if (!implicitMode()) return true;    // solo Ray Marching
    if (ui->alphaSlider->value() >= 100) return true;             // scena gia' opaca: nulla da fare

    // IL CRITERIO E' IL DISPLACEMENT, non la texture. %DISPLACEMENT_CODE% e'
    // iniettato in rawField() e quindi valutato dentro ogni map() del marcher
    // (MAX_FACES x 3 x MAX_LAYER_STEPS x 4 per pixel sul ramo trasparente);
    // %TEXTURE_CODE% vive nello shading finale e costa una valutazione per pixel.
    // Senza displacement non c'e' nessun moltiplicatore da temere, per quanto
    // elaborata sia la texture di colore: era il bug del "popup anche con una
    // texture leggera". Commenti tolti col filtro delle validazioni del commit.
    const QString disp = stripCodeComments(newDispCode).trimmed();
    if (disp.isEmpty()) return true;

    // GIA' CHIESTO per questo displacement: non si richiede, qualunque sia stata
    // la risposta. E' la fine del popup "a raffica": con "Restore" l'alpha torna
    // <1 e il commit successivo -- che il riavvio stesso puo' innescare --
    // ritrovava condizioni identiche e riapriva il box all'infinito. Il confronto
    // sul CODICE riarma da solo quando cambia davvero il displacement.
    if (disp == m_heavyTexGuardAskedFor) return true;
    m_heavyTexGuardAskedFor = disp;

    // Stato da ripristinare se l'utente sceglie di proseguire.
    const int alphaPrev = ui->alphaSlider->value();
    const bool wasMoving = !m_masterStopped;

    // STOP PRIMA del box: il moto va tolto mentre la finestra e' aperta,
    // altrimenti la scena continua a marciare trasparente+texturizzata proprio
    // nei secondi in cui l'utente legge (ed e' li' che si blocca).
    if (wasMoving) performMasterStop();

    // Opaco PRIMA del box, per lo stesso motivo: lo stop toglie il moto, non il
    // costo per pixel: con alpha<1 ogni singolo ridisegno (il box stesso, un
    // resize) resta da secondi. Set programmatico: i check dell'handler sono
    // fuori luogo qui, la decisione la prende questo box.
    {
        // La trasparenza la toglie l'app: non e' lavoro dell'utente da salvare.
        AbsorbChangesGuard absorbOpaque(this);
        m_settingAlphaProgrammatic = true;
        ui->alphaSlider->setValue(100);
        m_settingAlphaProgrammatic = false;
    }

    // Il watchdog va zittito e il suo eventuale segnale gia' in coda scartato:
    // stesso motivo documentato in forceOpaqueForHeavyRM (il box modale fa girare
    // l'event loop e aprirebbe il popup del watchdog SOPRA questo).
    ui->glWidget->acknowledgePerformanceWarning();
    m_transparencyGuardActive = true;
    m_heavyTexGuardActive = true;

    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Relief texture on a transparent surface"));
    box.setText(tr("This texture carves relief into the surface "
                   "(displacement), and the surface is currently transparent."));
    QString info = tr("The relief is computed at every step of every ray, and "
                      "transparency multiplies the number of those steps: "
                      "together they can make rendering collapse and freeze the "
                      "application, on desktop too.\n\n"
                      "The surface has been set to fully opaque");
    if (wasMoving)
        info += tr(" and the animation has been stopped");
    info += tr(" so you can see the result safely.\n\n");
    info += wasMoving
        ? tr("You can restore transparency and restart the animation at your "
             "own risk, or keep the surface opaque and stopped.")
        : tr("You can restore transparency at your own risk, or keep the "
             "surface opaque.");
    box.setInformativeText(info);
    QPushButton *restoreBtn = box.addButton(wasMoving ? tr("Restore and restart")
                                                      : tr("Restore transparency"),
                                            QMessageBox::AcceptRole);
    QPushButton *keepBtn    = box.addButton(wasMoving ? tr("Keep it opaque and stopped")
                                                      : tr("Keep it opaque"),
                                            QMessageBox::RejectRole);
    box.setDefaultButton(keepBtn);
    box.exec();
    const bool restore = (box.clickedButton() == restoreBtn);

    m_transparencyGuardActive = false;

    if (restore) {
        // Trasparenza PRIMA del riavvio, cosi' la scena riparte come l'utente
        // l'aveva lasciata. Programmatico: i check sono gia' stati assolti qui.
        m_settingAlphaProgrammatic = true;
        ui->alphaSlider->setValue(alphaPrev);
        m_settingAlphaProgrammatic = false;
        // Riavvio = vero master Start, come fa il gestore di performanceWarning:
        // onStartClicked riconosce sender()==m_btnStart e riarma i flag user-stop.
        if (wasMoving && m_btnStart) m_btnStart->click();
        // L'utente e' avvisato e prosegue: il watchdog resta zittito per QUESTA
        // scena (niente popup a raffica sullo stesso rallentamento). Si riarma
        // alla prossima presa dello slider o al prossimo rebuildShader.
        ui->glWidget->acknowledgePerformanceWarning();
    }

    // ULTIMA RIGA, non subito dopo exec(): il flag deve coprire anche il ramo
    // "restore" qui sopra. m_btnStart->click() rientra in onStartClicked, che
    // puo' ricommittare lo shader e ripassare di qui, e nel percorso libreria
    // syncImplicitAlphaSlider(newSurface=true) gira poche righe dopo il ritorno e
    // azzererebbe il "gia' chiesto" appena scritto (e' gated proprio da questo
    // flag). Abbassarlo dopo exec() lo rendeva cieco esattamente dove serviva.
    m_heavyTexGuardActive = false;
    return restore;
}

void MainWindow::updateRenderState()
{
    // Per primo, prima di qualunque ramo: vedi updateBackgroundControlsGate.
    updateBackgroundControlsGate();

    // 1. Identifichiamo se siamo in modalità Ray Marching
    bool isImplicitMode = (implicitMode());

        // --- DISATTIVAZIONE CONTROLLI NON SUPPORTATI ---
        ui->radioWF->setEnabled(!isImplicitMode);

        // Controlli densità Wireframe (uDensity/vDensity, contenitori dei tasti +/-):
        // attivi SOLO quando il Wireframe è la modalità di rendering attiva (e non in Ray
        // Marching, dove il wireframe non esiste). Disabilitare il widget contenitore
        // disabilita anche i suoi figli. Senza Wireframe la densità non ha effetto, quindi
        // i controlli vanno in grigio.
        // I radio MOSTRANO la mesh selezionata, quindi radioWF acceso significa
        // "la mesh che sto guardando e' in wireframe": e' esattamente la
        // condizione in cui i tasti densita' servono (agiscono su quella parte).
        bool wireframeDensityUsable = !isImplicitMode && shownRenderMode() == 2;
        ui->uDensity->setEnabled(wireframeDensityUsable);
        ui->vDensity->setEnabled(wireframeDensityUsable);

        // --- 1. DISATTIVAZIONE ROTAZIONI 4D ---
        // Omega/Phi/Psi vivono nel dock 3D insieme a Spin/Precessione/Nutazione.
        // Erano spenti in TUTTA la modalita' implicita perche' li' non avevano
        // effetto: lo shader ray marching non leggeva omega/phi/psi.
        //
        // Ora il sotto-tab CROSS SECTION li usa: l'equazione ha 4 variabili
        // (x,y,z,p) e la rotazione 4D decide quale sezione dell'ipersuperficie
        // si vede (vedi %CROSS_SECTION_P% in createImplicitFragmentShader).
        // Quindi restano spenti solo dove continuano a non avere effetto: il
        // sotto-tab "3D", la cui equazione e' a 3 variabili e non ha un p da
        // ruotare, e la modalita' parametrica non e' implicita affatto.
        const bool crossSectionActive = isImplicitMode && crossSectionTab();
        const bool rot4DUsable = !isImplicitMode || crossSectionActive;

        // Omega (W-X)
        ui->btnOmegaMinus->setEnabled(rot4DUsable);
        ui->btnOmegaPlus->setEnabled(rot4DUsable);

        // Phi (W-Y)
        ui->btnPhiMinus->setEnabled(rot4DUsable);
        ui->btnPhiPlus->setEnabled(rot4DUsable);

        // Psi (W-Z)
        ui->btnPsiMinus->setEnabled(rot4DUsable);
        ui->btnPsiPlus->setEnabled(rot4DUsable);

        // --- 3. DISATTIVAZIONE PANNELLO 4D ---
        // Il pannello contiene DUE famiglie di controlli con destini diversi in
        // Ray Marching:
        //  - path 4D e SPOSTAMENTI dell'osservatore (btnXPlus, btnPPlus, ...):
        //    scrivono m_cameraPos4D / m_observerPos, che il template ray
        //    marching NON legge (u_observerPos e u_cameraPos4D sono dichiarati
        //    nell'UBO ma non usati da nessuna riga dello shader). Restano spenti:
        //    accenderli darebbe tasti che non fanno nulla;
        //  - ROTAZIONI 4D della camera (btnOmegaAhead/Rear e i gemelli Phi/Psi):
        //    scrivono omega/phi/psi, cioe' ESATTAMENTE lo stato che il sotto-tab
        //    Cross Section usa per decidere quale sezione dell'ipersuperficie si
        //    vede. Sono i gemelli "a scatto" dei tasti Omega/Phi/Psi del dock 3D,
        //    gia' abilitati sopra con rot4DUsable.
        // NB: in Qt un figlio NON si riattiva dentro un padre disabilitato --
        // isEnabled() resta false comunque (verificato). Quindi NON si puo'
        // spegnere dockWidgetContents_3 a blocco e riaccendere le rotazioni
        // dopo: il contenitore va lasciato abilitato e si spengono i singoli
        // controlli che non hanno effetto.
        if (ui->dockWidgetContents_3) {
            ui->dockWidgetContents_3->setEnabled(true);
        }
        // Path 4D: ATTIVO nel sotto-tab Cross Section, spento nel "3D".
        //
        // Nel sotto-tab 3D le equazioni del path non hanno effetto, ed e' la
        // ragione per cui erano spente ovunque in ray marching: quel path
        // muoverebbe l'osservatore 4D (u_observerPos/u_cameraPos4D), che il
        // template non legge.
        //
        // Nel Cross Section invece funzionano gia', senza una riga di logica
        // nuova, perche' applyPath4DCameraAt NON passa da quegli uniform. Scrive
        // due cose che il ray marcher usa entrambe:
        //  - setRotation4D(omega, phi, psi): e' lo stato che %CROSS_SECTION_P%
        //    legge per decidere quale sezione dell'ipersuperficie si vede, quindi
        //    lungo il path la sezione cambia;
        //  - setCameraFrom4DVectors(...): proietta la posa 4D in una camera 3D
        //    (projectPoint4Dto3D), cioe' proprio la matrice di vista da cui il
        //    marcher ricava il raggio (inverse(u_mvMatrix)).
        // In pratica il path muove la camera nello spazio 4D e la sezione segue:
        // la stessa semantica del ramo parametrico, dalla stessa funzione
        // condivisa fra tick live e loop di registrazione (CLAUDE.md).
        //
        // Lo slider di VELOCITA' del pannello resta abilitato in entrambi i
        // sotto-tab anche a path spento: e' la manopola con cui si dosano i tasti
        // P+/P- della sezione (obsSpeed = speed4D * kObs4DSpeedMul).
        if (ui->panelPath) {
            ui->panelPath->setEnabled(true);
        }
        const bool path4DUsable = !isImplicitMode || crossSectionActive;
        for (QWidget *w : { (QWidget*)ui->lineX_P,  (QWidget*)ui->lineY_P,
                            (QWidget*)ui->lineZ_P,  (QWidget*)ui->lineP_P,
                            (QWidget*)ui->lineAlpha_P, (QWidget*)ui->lineBeta_P,
                            (QWidget*)ui->lineGamma_P }) {
            if (w) w->setEnabled(path4DUsable);
        }

        // DEPARTURE e VIEW hanno DUE condizioni, non una: il modo deve
        // permettere il path 4D (path4DUsable) E i campi devono definirlo.
        // Accendendoli qui insieme ai campi si scriveva la sola prima: passando
        // da parametrico a Cross Section il tasto si accendeva anche a campi
        // vuoti, sovrascrivendo l'ultimo checkPathFields.
        // Non si duplica la seconda condizione: la decidono checkPathFields e
        // updateViewButtonsEnabled, che sanno anche del path IN CORSA (a timer
        // attivo il tasto resta acceso per poterlo fermare).
        if (!path4DUsable) {
            if (ui->btnDeparture) ui->btnDeparture->setEnabled(false);
            if (ui->pushView)     ui->pushView->setEnabled(false);
        } else {
            checkPathFields();   // accende Departure e, di seguito, i View
        }
        // Navigazione 4D: contenitore ACCESO, si spengono i singoli tasti di
        // SPOSTAMENTO dell'osservatore, che scrivono m_cameraPos4D/m_observerPos
        // -- stato che lo shader ray marching non usa.
        // SPOSTAMENTI X/Y/Z dell'osservatore: restano spenti in Ray Marching.
        // Non perche' siano concettualmente fuori luogo, ma perche' sarebbero un
        // DOPPIO spostamento: la posizione 3D della telecamera e' gia' quella
        // della matrice di vista (rayPos = inverse(u_mvMatrix) * origine, nel
        // main() del template), e sommare anche u_observerPos.xyz muoverebbe la
        // scena due volte. Per muoversi in 3D ci sono i controlli del dock 3D.
        // btnLightMode idem: il suo modo si applica solo quando is4DActive() e'
        // vero (glwidget ~600), cosa che in Ray Marching non accade.
        for (QPushButton *b : { ui->btnXPlus, ui->btnXMinus,
                                ui->btnYPlus, ui->btnYMinus,
                                ui->btnZPlus, ui->btnZMinus,
                                ui->btnLightMode }) {
            if (b) b->setEnabled(!isImplicitMode);
        }
        // TRASLAZIONE LUNGO P: il caso opposto, ed e' il controllo piu'
        // importante del Cross Section dopo le rotazioni. Muove la QUOTA del
        // piano di sezione (u_observerPos.w, letta da %CROSS_SECTION_P%), cioe'
        // fa scorrere la sezione lungo la quarta dimensione: le rotazioni
        // cambiano l'INCLINAZIONE del taglio, questa cambia DOVE taglia. La
        // quarta coordinata e' anche l'unica che la matrice di vista non
        // rappresenta, quindi qui non c'e' il doppio spostamento degli altri assi.
        for (QPushButton *b : { ui->btnPPlus, ui->btnPMinus }) {
            if (b) b->setEnabled(rot4DUsable);
        }
        // ROTAZIONI 4D della camera: scrivono omega/phi/psi, cioe' lo stato che
        // il sotto-tab Cross Section usa per scegliere la sezione. Sono i
        // gemelli "a scatto" dei tasti Omega/Phi/Psi del dock 3D, e seguono lo
        // stesso gate (rot4DUsable).
        for (QPushButton *b : { ui->btnOmegaAhead, ui->btnOmegaRear,
                                ui->btnPhiAhead,   ui->btnPhiRear,
                                ui->btnPsiAhead,   ui->btnPsiRear }) {
            if (b) b->setEnabled(rot4DUsable);
        }

        if (isImplicitMode) {
            // Ripristino forzato se l'utente era in modalità non compatibili
            if (m_savedRenderMode == 2) {
                m_savedRenderMode = 1;
                refreshRenderRadios();
            }
        }

    // 2. RECUPERA LO STATO AGGIORNATO
    // 'mode' governa il GATING dell'interfaccia (texture, trasparenza, densita'
    // wireframe): deve seguire cio' che l'utente sta guardando, cioe' la
    // modalita' EFFICACE della mesh selezionata. Su "All" coincide con lo stato
    // globale, quindi il comportamento storico non cambia.
    // Restano invece su m_savedRenderMode le decisioni sullo stato GLOBALE
    // (vedi 'isPhong' e il blocco che scrive nel motore piu' sotto).
    int mode = m_savedRenderMode;
    if (ui->glWidget && ui->glWidget->activeMeshPart() >= 0
        && ui->glWidget->meshPartCount() > 1 && !isImplicitMode) {
        mode = ui->glWidget->activeMeshEffectiveRenderMode();
    }
    // L'accensione della texture di superficie nel motore NON si decide piu' qui
    // dal checkbox: il checkbox e' una vista (dello sfondo, della fascia
    // selezionata, spento in wireframe) e leggerlo come comando accendeva la
    // texture globale su una multi-mesh (superficie bianca o nera, vedi la
    // storia in applySurfaceTextureToEngine). Il motore segue l'intenzione
    // m_surfaceTextureState, piu' sotto.

    // 3. LOGICA TEXTURE (Mantenendo il fix per il Background)
    // In Wireframe superficie (mode==2, non in editing sfondo) la texture della
    // superficie non e' visibile: oltre al checkbox Texture, disabilitiamo anche
    // il ramo Texture del dock Library (l'albero treeTextures), cosi' non si puo'
    // applicare una texture che non avrebbe effetto. In editing sfondo la texture
    // di background e' indipendente dal wireframe, quindi resta tutto attivo.
    bool wireframeSurface = (mode == 2 && !editingBackground());
    // Il checkbox Texture e' la vista della texture del bersaglio: etichetta,
    // abilitazione (spento in wireframe sulla superficie e a scena vuota) e
    // spunta si decidono in un posto solo.
    refreshTextureCheckbox();
    if (ui->treeTextures) ui->treeTextures->setEnabled(!wireframeSurface);

    // Collasso + grigio del ramo Texture solo alla TRANSIZIONE di stato, non a
    // ogni updateRenderState (chiamata di frequente). Entrando in wireframe le
    // cartelle si chiudono e restano chiuse; uscendo si ripristina il colore.
    if (wireframeSurface != m_textureLibraryGrayed) {
        setTextureLibraryGrayed(wireframeSurface);
        m_textureLibraryGrayed = wireframeSurface;
    }

    // Transparence e Light non hanno effetto in Wireframe: la superficie è disegnata
    // a colore piatto senza illuminazione (surface.frag, ramo u_renderMode == 2),
    // quindi i due slider vanno in grigio. Qui NON c'è l'esclusione radioBackground
    // usata sopra per la texture: questi slider agiscono SEMPRE sulla superficie
    // (l'alpha dello sfondo è forzato a 1), anche mentre si edita lo sfondo.
    // Disabilitiamo il PANNELLO contenitore (etichette comprese) e non i singoli
    // slider: agire sul parent preserva il flag enabled proprio di alphaSlider,
    // così i suoi blocchi indipendenti (campo a prodotto, vista 2D) sopravvivono
    // al passaggio per il wireframe.
    //
    // In wireframe i due slider tornano anche al DEFAULT (opaco, luce 100%): lo
    // shader usa comunque ubuf.alpha sulle linee, e un alpha stantio (< 1) le
    // renderebbe sbiadite con lo slider ormai bloccato. Reset incondizionato
    // finché mode == 2, non solo alla transizione: copre anche il load di un
    // preset wireframe con alpha salvato < 1 (setValue del load, poi questa
    // chiamata lo riporta a 1). A slider disabilitati nessun input utente da
    // preservare; uscendo dal wireframe restano ai default.
    // Il reset riguarda lo stato GLOBALE, quindi si fa solo quando e' il
    // globale a essere in wireframe (m_savedRenderMode), NON quando e'
    // semplicemente la mesh selezionata a esserlo: li' le altre mesh sono
    // ancora solide e azzerarne trasparenza e luce cancellerebbe l'aspetto
    // per-mesh appena impostato.
    if (mode == 2 && m_savedRenderMode == 2) {
        // Questi due sono reset AUTOMATICI del motore, non scelte dell'utente su
        // una singola mesh: vanno sullo stato globale. Senza il bypass, con una
        // mesh selezionata nello spinbox finivano scritti su QUELLA parte (che
        // smetteva di ereditare) e il giro di segnali che ne seguiva poteva
        // inchiodare il selettore. Si manifestava solo TORNANDO in wireframe,
        // perche' solo qui esiste questo ramo.
        // Si RIPRISTINA il valore precedente, non si spegne a forza: durante il
        // load di un preset il bypass e' gia' acceso (MeshBypassGuard di
        // applySurfaceExample), e updateRenderState passa di qui dentro quel
        // periodo. Spegnendolo, il resto del load proseguiva SENZA protezione e
        // il setColor del colore globale finiva scritto sulla parte attiva.
        const bool oldBypass = ui->glWidget && ui->glWidget->meshAppearanceBypass();
        if (ui->glWidget) ui->glWidget->setMeshAppearanceBypass(true);
        resetTransparency();
        ui->lightSlider->setValue(100);   // valueChanged aggiorna intensità e label
        if (ui->glWidget) ui->glWidget->setMeshAppearanceBypass(oldBypass);
    }
    // Il FOV vive ORA dentro panelTransp (scelta di layout), ma NON deve
    // seguirne l'abilitazione: disabilitare un contenitore disabilita tutti i
    // figli, e il FOV deve restare sempre attivo (agisce sulla camera, non
    // sull'aspetto della superficie; vedi la nota storica sullo slider unico).
    // Percio' qui si disabilitano i figli che riguardano davvero la
    // trasparenza/luce, non il groupbox.
    const bool transpUsable = (mode != 2);
    if (ui->lblTrans)         ui->lblTrans->setEnabled(transpUsable);
    if (ui->panelSliderTrans) ui->panelSliderTrans->setEnabled(transpUsable);
    if (ui->lblLight)         ui->lblLight->setEnabled(transpUsable);
    if (ui->widget_2)         ui->widget_2->setEnabled(transpUsable);
    // Il groupbox resta abilitato: lo spegnerebbe insieme al FOV.
    ui->panelTransp->setEnabled(true);

    // Slider trasparenza su campo implicito mal condizionato (vedi syncImplicitAlphaSlider).
    syncImplicitAlphaSlider(isImplicitMode);

    bool isPhong = (m_savedRenderMode == 1);

    // 4. APPLICAZIONE AL MOTORE GRAFICO
    if (ui->glWidget) {
        ui->glWidget->setSpecularEnabled(isPhong);

        // Texture di superficie: accesa = intenzione e non wireframe (il
        // wireframe di UNA fascia non la spegne). Regola in un punto solo.
        applySurfaceTextureToEngine();

        if (isImplicitMode) {
            // Modalità Ray Marching: il render mode viene dai radio Shell/Solid
            // (panelRenderControls, comune ai due sotto-tab). Si passa
            // dall'helper e non da ui->radioShell diretto perche' quello e' il
            // punto unico di lettura -- quando i radio erano duplicati per
            // sotto-tab, leggerne uno fisso qui cancellava a ogni cambio
            // tab/proiezione/load la scelta fatta nell'altro.
            ui->glWidget->setGlobalRenderMode(implicitShellSelected() ? 1 : 0);
        } else {
            // Modalità Parametrica: Ascolta i radio button classici.
            // (il gating dello slider Thickness e' in fondo a questa funzione)
            // NB: setGlobalRenderMode scrive SOLO lo stato GLOBALE, mai su una parte,
            // anche se lo spinbox ha una mesh selezionata. Questa funzione gira
            // a ogni cambio tab / proiezione / load: se scrivesse sulla parte
            // attiva, il valore mostrato dai radio (che e' quello della mesh
            // selezionata) verrebbe riapplicato a un destinatario diverso ogni
            // volta, propagando il wireframe alle mesh che ereditano. La sola
            // via che scrive una modalita' per-parte e' onUserRenderModeChosen,
            // cioe' un click esplicito dell'utente.
            //
            // Quando una mesh e' selezionata i radio mostrano LEI, quindi non
            // sono una fonte valida per il globale: lo lasciamo com'e'.
            const bool showingPart =
                (ui->glWidget->activeMeshPart() >= 0 && ui->glWidget->meshPartCount() > 1);
            if (!showingPart) ui->glWidget->setGlobalRenderMode(m_savedRenderMode);
        }

        ui->glWidget->update();
    }

    // ==========================================================
    // SCENA VUOTA (tasto NEW): controlli di RESA in grigio
    // ==========================================================
    // I comandi legati al CONTENUTO si spengono gia' da soli a campi vuoti --
    // i Run del dock Equations via i flag "applied", Run/Save script via
    // hasGLSLCode, Run/Save texture via i controlli campi-vuoti, gli slider
    // A..F/S via updateConstantsUIState. Restano accesi solo quelli di RESA,
    // che non hanno mai avuto un gate sull'esistenza di una superficie perche'
    // fino a ora una superficie c'era sempre: agirebbero sul nulla.
    // Le eccezioni sono volute: COLORE e TEXTURE dello SFONDO restano usabili,
    // perche' lo sfondo esiste anche senza superficie ed e' l'unica cosa che
    // si puo' ancora comporre a scena vuota.
    // SPESSORE DEL GUSCIO: ha senso solo in Ray Marching e solo con Shell
    // selezionato -- in Solid non c'e' nessun guscio di cui regolare la parete, e
    // nel parametrico la modalita' non esiste affatto. Il contenitore intero
    // (etichetta, valore, slider) cosi' il numero non resta leggibile accanto a
    // uno slider spento.
    // Etichetta E slider: il pannello contenitore non esiste piu' (i tre
    // controlli vivono nella griglia gridRenderControls, che li allinea in
    // colonne condivise), quindi si disabilitano i due widget direttamente --
    // cosi' il testo non resta leggibile accanto a uno slider spento.
    {
        const bool shellUsable = isImplicitMode && implicitShellSelected();
        if (ui->lblShellThickness)    ui->lblShellThickness->setEnabled(shellUsable);
        if (ui->shellThicknessSlider) ui->shellThicknessSlider->setEnabled(shellUsable);
    }

    // FILL LIGHT: solo in Ray Marching. Lo shader parametrico DICHIARA
    // u_fillLight (il blocco UBO deve combaciare campo per campo col fragment,
    // regola Adreno) ma non lo usa, e non per una dimenticanza: il ramo
    // parametrico illumina con abs(dot(N,L)) -- two-sided, quindi la faccia
    // interna di un tubo e' gia' illuminata quanto l'esterna -- e alza il
    // risultato con un fondo fisso (0.3 + 0.7*diff), per cui nessuna faccia
    // scende sotto il 30%. Non ha le zone nere che questa luce va a recuperare:
    // nel ray marching una faccia radente misurava 0.06 su 1.
    // Sommarci un riempimento schiarirebbe tutto invece di recuperare le ombre,
    // appiattendo il modellato. Quindi il controllo si SPEGNE qui, invece di
    // restare acceso senza fare nulla.
    if (ui->lblFill)    ui->lblFill->setEnabled(isImplicitMode);
    if (ui->widgetFill) ui->widgetFill->setEnabled(isImplicitMode);

    // RAY STEPS e STEP RELAX restano ACCESI anche col marcher "Precise". Erano
    // stati spenti come "senza effetto", ed era sbagliato: in Precise governano
    // l'AVVICINAMENTO (marchField), e il suo esito non e' solo un'ottimizzazione.
    // Decide quali raggi sono sfondo -- un raggio che esce dalla scena senza aver
    // visto un cambio di segno viene scartato senza seconda marcia, anche se un
    // passo lungo ha scavalcato per intero una parete sottile -- e da dove riparte
    // la ricerca a cambio di segno, che guarda solo IN AVANTI. Verificato a
    // schermo: difetti presenti in Precise sparivano dopo aver mosso i due slider.
    // La misura sul T^3 che aveva motivato il gate riguardava il tetto di
    // marchNextLayer, non questi due valori.

    // Ultimi blocchi della funzione: sovrascrivono di proposito le decisioni
    // prese qui sopra, che presuppongono tutte una superficie a schermo e
    // l'editing della superficie.
    applyEmptySceneGating();
    updateSurfaceControlsGate();
}

// COMANDI DELLA SUPERFICIE SPENTI MENTRE SI EDITA LO SFONDO (radio Background in
// cima al dock RENDERER), fino al ritorno su Surface. Speculare a
// updateBackgroundControlsGate. Si spengono modo di resa e densita' wireframe,
// trasparenza, luce e Headlight: nessuno agisce sullo sfondo, e restando accesi
// sembravano comandi dello sfondo. Restano accesi texture e Color 1/2 (in
// Background sono quelli dello sfondo), gli slider RGB (colore dello sfondo) e
// il FOV (camera: inquadra anche il cielo). L'ambito All/Mesh lo spegne gia'
// updateMeshScopeEnabled.
// panelSurface non ha altre regole e si commuta direttamente: i figli spenti di
// proposito (Wireframe in Ray Marching, densita' fuori dal wireframe, radio a
// scena vuota) restano spenti, perche' Qt ricorda chi e' stato disabilitato
// esplicitamente. Trasparenza, luce e Headlight hanno invece regole loro su
// QUESTI contenitori (updateRenderState qui sopra, applyEmptySceneGating): qui
// si spengono soltanto, per ultimi; tornando su Surface li rimette a posto
// updateRenderState, che l'handler di radioBackground chiama.
void MainWindow::updateSurfaceControlsGate()
{
    const bool onBackground = editingBackground();
    if (ui->panelSurface) ui->panelSurface->setEnabled(!onBackground);
    if (!onBackground) return;
    for (QWidget *w : { static_cast<QWidget*>(ui->lblTrans), static_cast<QWidget*>(ui->panelSliderTrans),
                        static_cast<QWidget*>(ui->lblLight), static_cast<QWidget*>(ui->widget_2),
                        static_cast<QWidget*>(ui->lblFill),  static_cast<QWidget*>(ui->widgetFill) })
        if (w) w->setEnabled(false);
}

// Gate dei controlli di RESA sulla scena vuota. Sede unica, chiamata sia da
// updateRenderState (che decide tutto il resto dell'aspetto) sia da
// updateMasterButtonState: il Run NON passa da updateRenderState -- onStartClicked
// ha 31 uscite e nessuna la richiama -- quindi senza il secondo chiamante i
// controlli restavano grigi anche dopo aver ricostruito una superficie, e
// l'unico modo di riaverli era cambiare tab.
void MainWindow::applyEmptySceneGating()
{
    if (isSceneEmpty()) {
        // Trasparenza e luce: gli STESSI figli che il ramo wireframe disabilita
        // qui sopra. Si agisce sui figli e non sul groupbox panelTransp perche'
        // quello si porterebbe dietro anche il FOV, che ha una regola sua (piu'
        // sotto: a scena vuota si spegne anche lui, ma va detto esplicitamente).
        if (ui->lblTrans)         ui->lblTrans->setEnabled(false);
        if (ui->panelSliderTrans) ui->panelSliderTrans->setEnabled(false);
        if (ui->lblLight)         ui->lblLight->setEnabled(false);
        if (ui->widget_2)         ui->widget_2->setEnabled(false);

        ui->uDensity->setEnabled(false);
        ui->vDensity->setEnabled(false);

        // Texture: chkBoxTexture e' UN SOLO widget per due destinatari -- vale
        // per la superficie o per lo SFONDO a seconda di radioSurface/
        // radioBackground (cambia solo l'etichetta, ~2085). Spegnerlo sempre
        // bloccava anche la texture di sfondo, che a scena vuota deve restare
        // usabile: lo sfondo esiste anche senza superficie ed e' l'unica cosa
        // che si puo' ancora comporre. Stessa ragione per il ramo Library.
        // La regola sta nella vista (refreshTextureCheckbox: a scena vuota
        // resta acceso solo col bersaglio Background).
        refreshTextureCheckbox();
        // Il ramo Texture della Library resta USABILE anche senza superficie e
        // fuori da Background: cliccare una texture a scena vuota ricostruisce
        // la superficie di DEFAULT del tab corrente e ce la applica sopra (vedi
        // ensureSurfaceForTexture, chiamata dal click). Il checkbox qui sopra
        // invece resta legato allo sfondo: e' il display di uno stato, non un
        // comando che possa creare una superficie.
        if (ui->treeTextures) ui->treeTextures->setEnabled(true);

        // Risoluzione/passi: nulla da campionare.
        ui->stepSlider->setEnabled(false);
        ui->lineSteps->setEnabled(false);

        // Modo di resa della SUPERFICIE: non c'e' superficie da rendere.
        // (radioShell/radioSolid sono gli equivalenti del ramo Ray Marching, e
        // vivono in panelRenderControls insieme a Thickness e ai radio marcher.)
        if (ui->radioBasic)  ui->radioBasic->setEnabled(false);
        if (ui->radioPhong)  ui->radioPhong->setEnabled(false);
        if (ui->radioWF)     ui->radioWF->setEnabled(false);
        if (ui->radioShell)  ui->radioShell->setEnabled(false);
        if (ui->radioSolid)  ui->radioSolid->setEnabled(false);
        // Scelta del marcher: come Shell/Solid, riguarda solo il ray marching.
        if (ui->radioMarcherFast)    ui->radioMarcherFast->setEnabled(false);
        if (ui->radioMarcherPrecise) ui->radioMarcherPrecise->setEnabled(false);

        // Ambito multi-mesh: le parti non esistono piu'. Senza questo, restava
        // selezionabile la mesh di una superficie che non c'e' -- e i comandi
        // di aspetto sarebbero finiti dentro un MeshPart fantasma.
        if (ui->radioMeshAll) ui->radioMeshAll->setEnabled(false);
        if (ui->radioMeshOne) ui->radioMeshOne->setEnabled(false);
        if (ui->spinMeshSel)  ui->spinMeshSel->setEnabled(false);
        if (ui->panelMultiMesh) ui->panelMultiMesh->setEnabled(false);

        // Picker Color1/Color2: dipendono dalla texture ATTIVA (quali token
        // u_col1/u_col2 referenzia). A scena vuota la texture di superficie e'
        // spenta, quindi updateTextureUIState li spegne da se': va solo
        // CHIAMATA, perche' il reset non la invocava mai e i due radio
        // restavano accesi com'erano sotto il preset precedente.
        // In editing sfondo decide lo stato della texture di background, che
        // resta legittimamente componibile.
        updateTextureUIState(editingBackground() && targetTextureOn());

        // Slider RGB: NON si toccano da qui. La loro sede unica e'
        // onColorTargetChanged, che gira anche dopo questo gate e li
        // riaccenderebbe: il caso "scena vuota" e' dentro di lei, insieme alle
        // altre ragioni per cui possono essere inerti (texture senza colori).
        onColorTargetChanged();

        // FOV: agisce sulla camera, non sulla superficie, ed e' per questo che
        // il ramo wireframe lo lascia sempre attivo. Ma a scena VUOTA non c'e'
        // nulla da inquadrare, e il criterio qui e' "si spegne tutto tranne lo
        // sfondo": inquadrare il nulla non e' un'eccezione che vale la pena.
        if (ui->fovSliderMain) ui->fovSliderMain->setEnabled(false);
        if (ui->lblFov)        ui->lblFov->setEnabled(false);
        if (ui->lblValFov)     ui->lblValFov->setEnabled(false);

        m_emptySceneGated = true;
    }
    else if (m_emptySceneGated) {
        // La scena e' tornata piena (primo Run dopo un NEW): si riaccende SOLO
        // cio' che questo gate aveva spento, e solo la prima volta -- da qui in
        // poi comandano di nuovo le regole normali (wireframe, ray marching,
        // ambito mesh), che questo blocco non deve piu' scavalcare.
        // Trasparenza, luce e densita' non si toccano: dipendono dal render mode
        // e li rimette a posto updateRenderState, che gira a ogni cambio di
        // modalita'. Qui basta togliere il blocco su cio' che nessun altro
        // riaccenderebbe.
        m_emptySceneGated = false;

        ui->stepSlider->setEnabled(true);
        ui->lineSteps->setEnabled(true);

        // Modo di resa: si riaccende solo cio' che la modalita' corrente
        // ammette. radioWF non esiste in Ray Marching e radioShell/radioSolid
        // non esistono in parametrico: la scelta la rifa' updateRenderState
        // (che gestisce anche il ripristino forzato se si veniva da wireframe),
        // qui basta togliere il blocco.
        const bool isRM = (implicitMode());
        if (ui->radioBasic)  ui->radioBasic->setEnabled(true);
        if (ui->radioPhong)  ui->radioPhong->setEnabled(true);
        if (ui->radioWF)     ui->radioWF->setEnabled(!isRM);
        if (ui->radioShell)  ui->radioShell->setEnabled(true);
        if (ui->radioSolid)  ui->radioSolid->setEnabled(true);
        // Scelta del marcher: SOLO in Ray Marching (nel ramo parametrico non c'e'
        // marcher). radioWF qui sopra usa la stessa condizione, al contrario.
        if (ui->radioMarcherFast)    ui->radioMarcherFast->setEnabled(isRM);
        if (ui->radioMarcherPrecise) ui->radioMarcherPrecise->setEnabled(isRM);
        // NB: il gate di Ray Steps col marcher Precise NON va qui. Questo ramo
        // gira una volta sola, all'uscita dalla scena vuota (m_emptySceneGated
        // viene azzerato sopra): con una superficie gia' carica non passa mai.
        // Vive in updateRenderState, che gira a ogni cambio di stato.

        // Ambito multi-mesh: lo stato giusto lo conosce updateMeshScopeEnabled
        // (dipende da quante parti ha la superficie appena costruita), che ha
        // gia' la sua logica di "usable". Qui si toglie solo il blocco del
        // pannello e si lascia decidere a lei.
        if (ui->panelMultiMesh) ui->panelMultiMesh->setEnabled(true);
        updateMeshScopeEnabled();

        // Slider RGB: li rimette onColorTargetChanged, che sa se il target e'
        // superficie, sfondo o una texture senza colori (in quel caso restano
        // spenti di proposito).
        onColorTargetChanged();

        // FOV di nuovo utile: c'e' qualcosa da inquadrare.
        if (ui->fovSliderMain) ui->fovSliderMain->setEnabled(true);
        if (ui->lblFov)        ui->lblFov->setEnabled(true);
        if (ui->lblValFov)     ui->lblValFov->setEnabled(true);

        // Trasparenza, luce, densita' e texture le rimette a posto
        // updateRenderState secondo le regole normali. NON la si chiama da qui:
        // e' lei a chiamare questa funzione, e si avviterebbe. Quando il gate si
        // apre dal Run (via updateMasterButtonState) la si invoca dal chiamante.
    }
}

// Scena vuota = nessuna geometria a schermo. Non guarda i campi della UI (che
// l'utente puo' aver gia' ricominciato a riempire) ma cio' che il motore ha
// davvero da disegnare: in parametrico la mesh, in ray marching l'equazione
// implicita. E' la stessa condizione che il tasto NEW produce via
// resetScene(..., false).
// Scena vuota + click su una texture: si ricostruisce la superficie di DEFAULT
// del tab corrente (sfera in Ray Marching, superficie parametrica di avvio in
// Parametric) cosi' la texture ha su cosa apparire. Senza, il click non
// produceva nulla di visibile e il ramo Texture della Library restava
// disabilitato apposta per non offrire un gesto inerte -- ma cosi' da una scena
// vuota non c'era modo di ripartire da una texture.
//
// Si passa da resetScene(..., loadDefaultSurface=true), cioe' lo STESSO percorso
// del cambio tab e del reset: nessuna seconda copia della costruzione di default
// da tenere allineata. Non e' un ramo nuovo, e' quello gia' in produzione.
//
// Ritorna true se ha ricostruito qualcosa: il chiamante lo usa per sapere che la
// scena e' cambiata sotto i piedi e deve rileggerla.
bool MainWindow::ensureSurfaceForTexture()
{
    if (!isSceneEmpty()) return false;
    // In editing SFONDO non serve: lo sfondo esiste anche senza superficie ed e'
    // proprio il caso che a scena vuota resta componibile.
    if (editingBackground()) return false;

    resetScene(implicitMode() ? 1 : 0, /*loadDefaultSurface=*/true);
    return true;
}

bool MainWindow::isSceneEmpty() const
{
    if (!ui->glWidget) return false;

    if (implicitMode()) {
        // Il ramo ATTIVO nel marcher, non sempre quello del sotto-tab 3D: nel
        // Cross Section il campo compilato e' m_eqCrossSectionF, e leggere
        // implicitEquation() giudicava la scena su un'equazione che a schermo
        // non c'e'. Dopo un New nel Cross Section la scena restava percio'
        // "vuota" anche a superficie ripristinata, e il gate non si sganciava
        // piu': i controlli del tab Render (Basic/Phong, wireframe, texture)
        // rimanevano disabilitati finche' non si cambiava modalita'.
        const QString eq = ui->glWidget->activeImplicitEquation().trimmed();
        return eq.isEmpty() || eq == QLatin1String(kEmptyImplicitField);
    }

    const SurfaceEngine *e = ui->glWidget->getEngine();
    return !e || e->getIndices().empty();
}


// ==========================================================
// RENDERING & VISUALS
// ==========================================================

void MainWindow::resetTransparency()
{
    // setValue(100) scatena il valueChanged dello slider, che e' la fonte unica
    // di verita': aggiorna alphaValue, l'etichetta e la GPU (setAlpha) in un
    // colpo. Se eravamo gia' a 100 il segnale non scatta, quindi forziamo a mano
    // membro/label/GPU per coprire anche quel caso.
    if (ui->alphaSlider->value() != 100) {
        ui->alphaSlider->setValue(100);
    }
    alphaValue = 1.0f;
    ui->lblAlphaVal->setText("1.00");
    if (ui->glWidget) ui->glWidget->setAlpha(1.0f);
}

// Shell/Solid del Ray Marching. UNICA implementazione: la chiamano entrambi i
// rami implicit di applyCommonData (equazione E script) e il reset alla sfera di
// default in resetScene. Prima esisteva solo dentro il ramo EQUAZIONE: siccome i
// record RM sono quasi tutti da SCRIPT, caricare un record (o la sfera di
// default) lasciava i radio e il motore sullo stato del preset precedente.
// I radio sono esclusivi e il loro toggled riscrive comunque il globalRenderMode:
// li muoviamo a segnali bloccati e scriviamo noi il motore, per non far girare
// l'handler durante un load.
void MainWindow::applyImplicitShellMode(bool shell)
{
    m_implicitShell = shell;
    // UNA SOLA coppia di radio, in panelRenderControls (widget comune ai due
    // sotto-tab). Lo stato Shell/Solid e' sempre stato UNO SOLO — il render mode
    // del motore e' globale e il preset lo salva in un solo campo (renderMode
    // composito, >= 10 = Shell) — e da quando i radio non sono piu' duplicati per
    // sotto-tab non c'e' nulla da riallineare: la selezione non puo' contraddire
    // cio' che si vede.
    if (ui->radioShell && ui->radioSolid) {
        const bool oldShell = ui->radioShell->blockSignals(true);
        const bool oldSolid = ui->radioSolid->blockSignals(true);
        if (shell) ui->radioShell->setChecked(true);
        else       ui->radioSolid->setChecked(true);
        ui->radioShell->blockSignals(oldShell);
        ui->radioSolid->blockSignals(oldSolid);
    }

    if (ui->glWidget) ui->glWidget->setGlobalRenderMode(shell ? 1 : 0);
}

// Spessore del guscio: motore + posizione dello slider in un colpo. La curva dello
// slider e' quadratica (0..100 -> 0.005..0.30), quindi la posizione si ricava
// invertendola: f = sqrt((t - min) / (max - min)). Tenerla qui evita che ogni
// chiamante se la riscriva -- ed e' la stessa curva definita nel connect dello
// slider, che resta l'unico punto in cui i due estremi sono scritti.
void MainWindow::setShellThicknessUI(float thickness)
{
    // Chi scrive qui (load, reset, clic su Solid) decide lo spessore da capo:
    // lo spessore ricordato per il ritorno da Solid non vale piu'. Il clic su
    // Solid lo reimposta DOPO questa chiamata (updateImplicitRenderMode).
    m_shellThicknessBeforeSolid = -1.0f;

    const double kMin = 0.005, kMax = 0.30;
    const double t = qBound(kMin, (double)thickness, kMax);

    if (ui->glWidget) ui->glWidget->setShellThickness((float)t);

    if (ui->shellThicknessSlider) {
        const double f = std::sqrt((t - kMin) / (kMax - kMin));
        const bool old = ui->shellThicknessSlider->blockSignals(true);
        ui->shellThicknessSlider->setValue(qRound(f * 100.0));
        ui->shellThicknessSlider->blockSignals(old);
    }
}

// LUCE DI RIEMPIMENTO (dock Renderer): motore + slider + etichetta in un colpo.
// La conversione dello slider non e' l'identita' (0..100 -> 0.00..1.20, fattore
// 0.012), quindi ogni chiamante che scrivesse i widget a mano dovrebbe
// ripeterla -- ed e' la ragione per cui questa funzione esiste, come la gemella
// setShellThicknessUI.
//
// A SEGNALI BLOCCATI: serve dal caricamento di un preset e dai reset, dove la
// scrittura NON deve passare per l'handler dello slider (che la scambierebbe per
// una modifica dell'utente). Il motore lo scriviamo qui, esplicitamente, perche'
// setValue non emette valueChanged quando il valore coincide con quello corrente
// -- due preset di fila con la stessa luce lascerebbero la GPU non aggiornata.
void MainWindow::setFillLightUI(float v)
{
    const float val = qBound(0.0f, v, 1.20f);
    if (ui->glWidget) ui->glWidget->setFillLight(val);
    if (ui->fillLightSlider) {
        const bool old = ui->fillLightSlider->blockSignals(true);
        ui->fillLightSlider->setValue(qRound(val / 0.012f));
        ui->fillLightSlider->blockSignals(old);
    }
    if (ui->lblValFill) ui->lblValFill->setText(QString::number(val, 'f', 2));
}

// MARCHER (radio Fast/Precise): scrive il motore E i radio, a segnali bloccati.
// Gemella di setShellThicknessUI: serve dal caricamento di un preset e dai reset,
// dove la scrittura NON deve passare per l'handler dei radio (che la scambierebbe
// per una modifica dell'utente e accenderebbe l'avviso "lavoro non salvato").
void MainWindow::setMarcherUI(bool precise)
{
    if (ui->glWidget) ui->glWidget->setHybridMarcher(precise);

    // ENTRAMBI bloccati, come fa applyImplicitShellMode: l'esclusivita' spegne
    // l'altro radio, e se i suoi segnali non sono bloccati il suo toggled(false)
    // parte comunque (l'handler lo scarta con "solo chi si accende", ma il
    // pattern del progetto e' bloccare la coppia e scrivere il motore qui).
    QRadioButton *fast = ui->radioMarcherFast;
    QRadioButton *prec = ui->radioMarcherPrecise;
    if (fast && prec) {
        const bool oldFast = fast->blockSignals(true);
        const bool oldPrec = prec->blockSignals(true);
        if (precise) prec->setChecked(true);
        else         fast->setChecked(true);
        fast->blockSignals(oldFast);
        prec->blockSignals(oldPrec);
    }
}

// FOV UNICO. Unico punto che imposta il campo visivo: aggiorna slider + etichetta
// del dock renderer e applica SEMPRE il valore alla proiezione, senza condizioni.
//
// Prima c'erano applyPathFov3D/applyPathFov4D, una per dock, che applicavano il
// valore SOLO se la rispettiva path era in corsa. Con entrambi i path fermi il
// FOV non era piu' modificabile (gli slider erano anche disabilitati), quindi
// dopo un path a FOV largo l'inquadratura restava tale fino a Reset View; e coi
// due path attivi in sequenza i due slider si contendevano lo stesso
// m_cameraFov. Un solo controllo, sempre attivo, elimina entrambi i problemi.
//
// m_fov3D/m_fov4D restano allineati al valore unico perche' PresetSerializer li
// scrive ancora nel JSON (chiavi fov3D/fov4D): i file salvati da questa build
// restano leggibili dalle build precedenti, che li usavano per i path.
// ==========================================================
// ASPETTO PER-MESH: spinbox di selezione
// ==========================================================
// Valuta la direttiva "MESH_VISIBLE := <espressione>;" con le costanti A..S
// correnti. 0 = non dichiarata (o non valutabile): il chiamante usa allora tutte
// le mesh dichiarate, cioe' il comportamento di sempre.
// Si rivaluta ogni volta invece di memorizzare un numero perche' l'espressione
// dipende dalle costanti: nei tori di Hopf e' "E", e deve seguire lo slider.
int MainWindow::meshVisibleCount() const
{
    if (m_meshVisibleExpr.isEmpty()) return 0;

    // const_cast: resolveCascadeConstants non e' const (aggiorna la cache
    // m_lastValidConst dei valori buoni). Qui pero' e' l'unico effetto, e scrive
    // lo stesso valore che i campi hanno gia': con restoreTextOnNegative = false
    // non tocca la UI.
    MainWindow *self = const_cast<MainWindow*>(this);
    const CascadeConstants k = self->resolveCascadeConstants(false);

    // ATTENZIONE, NON passare l'espressione a parseUIConstant cosi' com'e':
    // ExprTk e' CASE-INSENSITIVE e add_constants() registra 'e' = numero di
    // Nepero, che vince su add_constant("E", ...). "MESH_VISIBLE := E" veniva
    // quindi valutato 2.71828 -> floor(+0.5) = 3 mesh invece di 5 (bug visto sui
    // tori di Hopf con E = 5). Stessa famiglia della nota su PI/e/tau riservati
    // nel traduttore.
    // Percio' le lettere delle costanti si sostituiscono QUI col loro valore
    // numerico, prima di dare il testo al parser: cosi' 'E' non arriva mai a
    // ExprTk come simbolo. Le parentesi proteggono le espressioni composte
    // (es. "E-1" con E negativo non diventa "--1").
    QString expr = m_meshVisibleExpr;
    const struct { const char* name; float val; } consts[] = {
        {"A", k.a}, {"B", k.b}, {"C", k.c}, {"D", k.d},
        {"E", k.e}, {"F", k.f}, {"S", k.s},
    };
    for (const auto& c : consts) {
        const QRegularExpression re(QString("\\b%1\\b").arg(c.name),
                                    QRegularExpression::CaseInsensitiveOption);
        expr.replace(re, QString("(%1)").arg(c.val, 0, 'g', 9));
    }

    // E NEMMENO passarla a parseUIConstant per la valutazione: quella funzione
    // comincia con replace(",", "."), una comodita' per chi scrive i decimali
    // all'italiana in un CAMPO (dove c'e' un numero solo e non ci sono virgole
    // separatrici). Qui invece le virgole separano gli ARGOMENTI, e quel replace
    // trasforma min(max(E,1),6) in min(max(E.1).6): non compila, ok = false, e
    // il chiamante leggeva "nessuna direttiva" tornando al conteggio dichiarato.
    // E' il motivo per cui Clifford Labyrinth arrivava a 6 con E = 3, mentre i
    // tori di Hopf funzionavano: "E" da sola non ha virgole.
    // Stessa trappola della memoria sui campi dei path (mod/atan2/min/max rotte
    // dal replace virgola->punto). Percio' qui ExprTk lo usiamo direttamente.
    exprtk::symbol_table<double> st;
    st.add_constants();          // pi, epsilon, inf: nessuna lettera A..F/S,
                                 // che a questo punto sono gia' numeri.
    exprtk::expression<double> compiled;
    compiled.register_symbol_table(st);
    exprtk::parser<double> parser;
    if (!parser.compile(expr.toStdString(), compiled)) return 0;

    // Arrotondamento allo stesso modo dello shader dei tori di Hopf
    // (floor(x + 0.5)), cosi' UI e superficie contano le mesh nello stesso modo:
    // e' la stessa cautela della memoria sulle costanti discrete, dove lo script
    // e lo slider dovevano arrotondare uguale.
    const int nVis = int(std::floor(float(compiled.value()) + 0.5f));
    return nVis > 0 ? nVis : 0;
}

// Riallinea il range dello spinbox al numero di parti della superficie corrente.
// Va chiamata dopo ogni rigenerazione della mesh: con una superficie a mesh
// singola il massimo resta 1, quindi lo spinbox e' inerte e la funzionalita' e'
// invisibile: nulla cambia per i preset che non usano il multi-mesh.
// Il selettore All/Mesh ha senso solo dove esistono davvero piu' fasce E i
// comandi agiscono sulla superficie. Punto UNICO, perche' i due casi che lo
// disabilitano si erano gia' dimostrati facili da dimenticare:
//  - SUPERFICIE A MESH SINGOLA. I preset non salvano "meshScopeAll", quindi
//    applyPendingMeshScope apriva in "Mesh" anche una superficie normale: con
//    la parte 0 attiva ogni comando veniva dirottato su di essa e sembrava che
//    "non funzionasse niente" finche' non si sceglieva All a mano.
//  - BACKGROUND. Li' si edita lo sfondo, che di fasce non ne ha: i controlli si
//    spengono, ma la selezione NON si tocca -- uscendo si deve ritrovare
//    l'ambito con cui si stava lavorando.
bool MainWindow::meshScopeUsable() const
{
    if (!ui->glWidget) return false;
    if (editingBackground()) return false;
    // Scena vuota (tasto NEW): non ci sono parti da selezionare. Il controllo
    // sta QUI e non solo in applyEmptySceneGating perche' updateMeshSelectorRange
    // riabilita i radio All/Mesh per conto suo (~13813) sull'onda di
    // meshPartsChanged: gaterebbe il gate.
    if (isSceneEmpty()) return false;
    return ui->glWidget->meshPartCount() > 1;
}

// Abilita/disabilita il gruppo All/Mesh.
// L'ambito viene riportato ad "All" SOLO quando le fasce non esistono (mesh
// singola): li' "Mesh" non ha significato e lasciare la parte 0 attiva
// dirottava ogni comando su di essa.
// In BACKGROUND invece si spengono solo i controlli: la selezione resta com'e'
// e si ritrova uscendo. Lo sfondo ha i propri stati (bg_*) e non passa dai
// setter per-mesh, quindi non c'e' nulla da proteggere azzerandola.
void MainWindow::updateMeshScopeEnabled()
{
    if (!ui->radioMeshAll || !ui->radioMeshOne || !ui->spinMeshSel || !ui->glWidget) return;

    const bool usable = meshScopeUsable();

    ui->radioMeshAll->setEnabled(usable);
    ui->radioMeshOne->setEnabled(usable);
    ui->spinMeshSel->setEnabled(usable && !meshScopeAll());

    // LIMITI PER-MESH: attivi in ENTRAMBI gli ambiti su una superficie
    // multi-mesh. In "Mesh" tagliano la parte scelta, in "All" tagliano tutte
    // le mesh insieme sospendendo -- senza cancellarli -- i tagli per-parte,
    // esattamente come colore, trasparenza e texture si comportano nei due
    // ambiti. Il dominio di "All" e' un livello suo (SurfaceEngine::setAllDomain),
    // indipendente dai limiti del dock Equations: quelli valgono per le
    // superfici a mesh singola e vengono riscritti dalle direttive "u_min :="
    // a ogni Run, quindi non potevano reggere un taglio dell'utente.
    const bool meshLimitsUsable = usable;
    for (QLineEdit* e : { ui->meshUMinEdit, ui->meshUMaxEdit,
                          ui->meshVMinEdit, ui->meshVMaxEdit }) {
        if (!e) continue;
        e->setEnabled(meshLimitsUsable);
        if (!meshLimitsUsable) {
            QSignalBlocker b(e);
            e->clear();
        }
    }

    if (usable) return;

    // BACKGROUND: i controlli sono gia' spenti qui sopra e basta cosi'. La
    // selezione va LASCIATA com'era, per ritrovarla uscendo.
    if (editingBackground()) return;

    // MESH SINGOLA: qui "Mesh" non ha significato, quindi si torna ad "All".
    // I valori per-parte NON si toccano (restano in MeshPart e ricompaiono se
    // la superficie torna multi-mesh): cambia solo il destinatario dei comandi.
    if (!meshScopeAll()) setMeshScopeAll(true);
    ui->glWidget->setMeshAppearanceUniform(true);
    ui->glWidget->setActiveMeshPart(-1);
}

void MainWindow::updateMeshSelectorRange()
{
    if (!ui->spinMeshSel || !ui->glWidget) return;

    int n = ui->glWidget->meshPartCount();
    // MESH VISIBILI: un //CUTOUT puo' spegnere le mesh oltre un certo indice
    // (tori di Hopf: lo slider E ne accende da 1 a 9 delle 9 dichiarate). Quelle
    // spente esistono come geometria ma non si vedono, quindi selezionarle
    // significava muovere slider che non cambiavano nulla a schermo.
    // Il numero vivo lo DICHIARA lo script con "MESH_VISIBLE := E;": non e'
    // deducibile qui, perche' il cutout e' GLSL eseguito sulla GPU e rifarne il
    // conto in C++ sarebbe la solita logica duplicata che diverge.
    // Senza direttiva nVis = 0 e vale il conteggio dichiarato, come da sempre.
    const int nVis = meshVisibleCount();
    if (nVis > 0) n = std::min(n, nVis);

    // Lo spinbox parte da 1 e sceglie SOLO quale mesh: "All" e' ora un radio a
    // parte, non piu' il valore 0 (specialValueText). Con una parte sola resta
    // 1..1: inerte ma coerente, e non serve piu' il caso speciale maxSel = 0.
    const int maxSel = std::max(1, n);

    // Il valore da RIPRISTINARE non e' quello dello spinbox ma quello del
    // widget: setMaximum() clampa il valore da solo, e lo fa a segnali
    // bloccati, quindi una rigenerazione transitoria con una parte sola
    // azzerava lo spinbox SENZA che il gestore girasse mai.
    // m_activeMeshPart restava all'indice vecchio: da li' in poi il numero
    // mostrato e la mesh realmente selezionata erano due cose diverse, e
    // tornare sul numero visualizzato non emetteva valueChanged perche' lo
    // spinbox ci era gia'. E' il "dopo qualche giro il campo si blocca".
    // In "All" la parte attiva e' -1: li' il numero mostrato non va cambiato,
    // resta quello a cui si tornera' passando a Mesh.
    const int active = ui->glWidget->activeMeshPart();
    int wanted = (active >= 0) ? active + 1 : ui->spinMeshSel->value();

    bool old = ui->spinMeshSel->blockSignals(true);
    ui->spinMeshSel->setMaximum(maxSel);
    ui->spinMeshSel->setValue(qBound(1, wanted, maxSel));
    ui->spinMeshSel->blockSignals(old);

    // AMBITO "ALL": e' il radio a decidere, non il numero. La parte attiva resta
    // -1 (i comandi vanno sul globale) e lo spinbox e' disabilitato ma conserva
    // il suo valore, cosi' tornando su Mesh si ritrova la stessa selezione.
    if (meshScopeAll()) {
        ui->glWidget->setActiveMeshPart(-1);
        ui->spinMeshSel->setEnabled(false);
        ui->radioMeshAll->setEnabled(true);
        if (ui->radioMeshOne) ui->radioMeshOne->setEnabled(true);
        // In "All" non c'e' nessuna mesh da mostrare negli slider, ma il flag va
        // consumato lo stesso: restando alzato forzerebbe un sync spurio al
        // primo giro utile (una rigenerazione qualunque), riallineando gli
        // slider sotto le dita dell'utente.
        m_meshScopeJustApplied = false;
        updateMeshScopeEnabled();
        return;
    }
    ui->spinMeshSel->setEnabled(true);

    // Riallinea SEMPRE il widget al valore che lo spinbox ha davvero adesso
    // (che puo' essere stato clampato qui sopra). Cosi' i due non possono
    // divergere, qualunque cosa abbia fatto setMaximum.
    const int now = ui->spinMeshSel->value() - 1;
    const bool moved = (now != ui->glWidget->activeMeshPart());
    ui->glWidget->setActiveMeshPart(now);

    // Gli slider si riallineano solo se la selezione e' davvero cambiata: qui
    // si passa a ogni rigenerazione della griglia, e risincronizzarli sempre
    // li farebbe saltare sotto le dita mentre si edita una mesh.
    // updateRenderState va chiamata DOPO il sync (che muove i radio a segnali
    // bloccati, quindi non la fa girare da solo): e' lei ad abilitare i tasti
    // densita' U/V in base ai radio appena aggiornati.
    //
    // ECCEZIONE: subito dopo un LOAD. applyPendingMeshScope gira sullo stesso
    // meshPartsChanged, appena prima di questa funzione, e porta gia' la parte
    // attiva all'indice del preset; "moved" risulta quindi falso (spinbox 1 ->
    // indice 0, dove la parte attiva gia' e') e il sync veniva saltato. Gli
    // slider restavano sul colore GLOBALE mentre la mesh 1 disegnava il proprio:
    // il disallineamento visibile GIA' al primo caricamento, senza cambio di
    // preset. Il flag vale un giro solo, quindi non reintroduce il salto degli
    // slider durante l'editing.
    const bool justLoaded = m_meshScopeJustApplied;
    m_meshScopeJustApplied = false;
    if (moved || justLoaded) {
        syncAppearanceControlsToActiveMesh();
        updateRenderState();
    }

    // GATING del gruppo All/Mesh (mesh singola / Background -> disabilitato).
    // Storicamente qui i radio si riabilitavano SEMPRE, perche' disabilitarli
    // "in base al numero di parti" li aveva resi irrecuperabili: bastava una
    // rigenerazione transitoria con una parte sola (cambio tab, un Run fallito
    // prima di ri-estrarre le sezioni) e restavano grigi per sempre.
    // Ora il gating e' recuperabile perche' NON e' one-shot: questa funzione
    // gira a ogni meshPartsChanged, quindi appena le parti tornano il gruppo si
    // riaccende da solo. La condizione vive in meshScopeUsable(), punto unico.
    updateMeshScopeEnabled();
}

// Porta gli slider (colore, trasparenza, Light) sui valori della parte
// selezionata, cosi' mostrano cio' che stanno per modificare. Una parte che
// eredita (valori negativi) mostra lo stato globale.
// Riversa sulle parti l'aspetto letto dal preset. Va chiamata DOPO che la
// griglia e' stata generata: prima le parti non esistono. Se il preset non
// portava nulla la lista e' vuota e non tocca niente, quindi le parti restano
// a "eredita dal globale" come da sempre.
// AMBITO All/Mesh: si ripristina quello con cui il preset e' stato SALVATO.
// Forzare sempre "Mesh" era sbagliato: una superficie messa tutta in wireframe
// da "All" salva renderMode = 2 (globale) e NESSUNA modalita' propria nelle
// parti; riaprendola in "Mesh" quelle parti EREDITAVANO il wireframe globale e
// si vedevano tutte wireframe, ma in un ambito che l'utente non aveva scelto.
// Preset vecchi (nessuna chiave "meshScopeAll"): si apre in "Mesh", com'e'
// sempre stato.
void MainWindow::applyPendingMeshScope()
{
    if (!ui->glWidget || !ui->radioMeshAll || !ui->radioMeshOne) return;

    // "PENDING" davvero: si applica UNA volta sola, al primo giro dopo il load.
    // Il chiamante e' agganciato a meshPartsChanged, che scatta a OGNI
    // rigenerazione della griglia (ogni cambio di costante): senza questo
    // consumo, ogni movimento di uno slider riportava l'ambito allo stato del
    // preset e riscriveva la parte attiva, annullando la scelta dell'utente.
    if (!m_meshScopePending) return;

    // NON si consuma su una griglia che non e' ancora quella del preset.
    // meshPartsChanged scatta anche PRIMA che le parti nuove esistano: durante
    // il load arriva un giro con la superficie precedente ancora nel motore
    // (l'unica parte sintetizzata da resolveMeshParts, perche' clearMeshParts ha
    // gia' svuotato le dichiarate). Consumando li', il flag sarebbe gia' false
    // al giro BUONO e setActiveMeshPart non verrebbe mai chiamato con l'indice
    // del preset. Se il preset non porta aspetto per-mesh (m_pendingMeshParts
    // vuoto) non c'e' nulla da attendere e vale il primo giro, come da sempre.
    if (!m_pendingMeshParts.empty()
        && ui->glWidget->getEngine()
        && ui->glWidget->getEngine()->getMeshPartCount() < (int)m_pendingMeshParts.size())
        return;

    m_meshScopePending = false;

    // Superficie a mesh SINGOLA (o Background attivo): l'ambito "Mesh" non ha
    // senso. I preset non salvano "meshScopeAll", quindi senza questa riga una
    // superficie normale si apriva in "Mesh" con la parte 0 attiva, e ogni
    // comando finiva dirottato su di essa: sembrava che "non funzionasse
    // niente" finche' non si sceglieva All a mano.
    const bool wantAll = m_pendingMeshScopeAll || !meshScopeUsable();
    setMeshScopeAll(wantAll);
    ui->glWidget->setMeshAppearanceUniform(wantAll);
    ui->glWidget->setActiveMeshPart(wantAll ? -1 : ui->spinMeshSel->value() - 1);
    ui->spinMeshSel->setEnabled(!wantAll);

    // Il sync degli slider tocca a updateMeshSelectorRange, che gira subito dopo
    // sullo stesso meshPartsChanged. Li' pero' il sync e' condizionato a "moved"
    // (la selezione e' cambiata), e la riga qui sopra ha GIA' portato la parte
    // attiva al valore che quella funzione calcolera': moved risulta falso e gli
    // slider non vengono mai riallineati alla mesh del preset.
    m_meshScopeJustApplied = true;
}

void MainWindow::applyPendingMeshAppearance()
{
    if (!ui->glWidget || !ui->glWidget->getEngine()) return;

    // L'AMBITO va ripristinato anche quando il preset NON porta aspetto
    // per-mesh: una superficie salvata in "All" (tutta wireframe dal globale,
    // nessun valore proprio nelle parti) non scrive la chiave "meshParts", e
    // con l'early-return sotto sarebbe riaperta in "Mesh". Percio' sta qui,
    // prima del return.
    applyPendingMeshScope();

    if (m_pendingMeshParts.empty()) return;

    SurfaceEngine *eng = ui->glWidget->getEngine();
    const int n = std::min((int)m_pendingMeshParts.size(), eng->getMeshPartCount());
    bool anyTexture = false;
    for (int k = 0; k < n; ++k) {
        MeshPart *dst = eng->mutableMeshPart(k);
        if (!dst) continue;
        const MeshPart &src = m_pendingMeshParts[k];
        dst->colorR = src.colorR;
        dst->colorG = src.colorG;
        dst->colorB = src.colorB;
        dst->alpha = src.alpha;
        dst->lightIntensity = src.lightIntensity;
        dst->renderMode = src.renderMode;
        dst->hasCustomRenderMode = src.hasCustomRenderMode;
        dst->wfStepU = src.wfStepU;
        dst->wfStepV = src.wfStepV;
        dst->textureCode = src.textureCode;
        dst->textureLibName = src.textureLibName;
        dst->textureEnabled = src.textureEnabled;
        dst->hasCustomTexture = src.hasCustomTexture;
        dst->texCol1R = src.texCol1R;
        dst->texCol1G = src.texCol1G;
        dst->texCol1B = src.texCol1B;
        dst->texCol2R = src.texCol2R;
        dst->texCol2G = src.texCol2G;
        dst->texCol2B = src.texCol2B;
        dst->texZoom = src.texZoom;
        dst->texPanX = src.texPanX;
        dst->texPanY = src.texPanY;
        dst->texRotation = src.texRotation;
        // OROLOGIO DELLA PARTE: **derivato dal codice**, non letto dal file.
        // texAnimating e' stato di RUNTIME e non viene serializzato (come il
        // clock globale, che il load ricalcola da hasTimeVariable): restando al
        // default false, ogni fascia texturizzata nasceva FERMA al caricamento
        // di un record. Derivarlo invece di salvarlo fa funzionare anche i
        // record salvati prima di questa feature.
        dst->texAnimating = dst->hasCustomTexture && dst->textureEnabled
                            && !dst->textureCode.isEmpty()
                            && hasTimeVariable(dst->textureCode);
        dst->timeTex = 0.0f;   // il record riparte dall'inizio
        if (src.hasCustomTexture) anyTexture = true;
    }
    eng->syncPartAppearance();
    m_pendingMeshParts.clear();

    // Le texture per-mesh vivono DENTRO il fragment shader (getCustomColor_<k>):
    // caricarle e' un cambio di CODICE, non di uniform, quindi senza ricompilare
    // resterebbero quelle della superficie precedente. Si ricompila solo se il
    // preset ne porta davvero almeno una: le superfici che non usano la feature
    // non pagano nulla.
    if (anyTexture) ui->glWidget->rebuildShader();

    // Le densita' wireframe per-parte appena caricate cambiano la GEOMETRIA
    // delle linee, non solo un uniform: senza ricostruzione si vedrebbero
    // ancora quelle della superficie precedente.
    ui->glWidget->rebuildWireframeGeometry();
    ui->glWidget->update();
}

// BASE / PHONG / WIREFRAME: lo stato (m_savedRenderMode, o la MeshPart della
// fascia selezionata) e la sua vista. Prima i radio facevano anche da stato: li
// leggevano colori, texture e motore, e tre punti li accendevano a segnali vivi
// (il reset e il ritorno da Wireframe in Ray Marching), facendo girare il
// gestore del clic.
int MainWindow::shownRenderMode() const
{
    if (ui->glWidget && !implicitMode()
        && ui->glWidget->activeMeshPart() >= 0 && ui->glWidget->meshPartCount() > 1)
        return ui->glWidget->activeMeshEffectiveRenderMode();
    return m_savedRenderMode;
}

// DISPLAY: in un QButtonGroup esclusivo setChecked(true) ne deseleziona un
// altro, che emette toggled(false): vanno bloccati i segnali di TUTTI i
// bottoni del gruppo, non solo di quello che si accende. Anche in Ray
// Marching, dove mostrano Base o Phong (m_savedRenderMode) e il Wireframe e'
// spento.
void MainWindow::refreshRenderRadios()
{
    if (!ui->radioBasic || !ui->radioPhong || !ui->radioWF) return;
    const int mode = shownRenderMode();

    QRadioButton *target = (mode == 2) ? ui->radioWF
                         : (mode == 1) ? ui->radioPhong
                                       : ui->radioBasic;
    if (!target || target->isChecked()) return;

    const bool b0 = ui->radioBasic->blockSignals(true);
    const bool b1 = ui->radioPhong->blockSignals(true);
    const bool b2 = ui->radioWF->blockSignals(true);
    target->setChecked(true);
    ui->radioBasic->blockSignals(b0);
    ui->radioPhong->blockSignals(b1);
    ui->radioWF->blockSignals(b2);
}


void MainWindow::syncAppearanceControlsToActiveMesh()
{
    if (!ui->glWidget || !ui->glWidget->getEngine()) return;

    // Guardia di rientranza: questa funzione muove slider e radio, e quei
    // widget possono a loro volta far ripartire il giro (updateRenderState ->
    // setGlobalRenderMode -> ...). Senza la guardia un rientro riscriverebbe lo stato
    // mentre lo stiamo leggendo, e il selettore smetterebbe di rispondere.
    if (m_syncingMeshControls) return;
    m_syncingMeshControls = true;
    struct Guard { bool &f; ~Guard(){ f = false; } } guard{m_syncingMeshControls};

    auto setNoSignal = [](QSlider *s, int v) {
        if (!s) return;
        bool old = s->blockSignals(true);
        s->setValue(v);
        s->blockSignals(old);
    };

    // DISPLAY di colore / trasparenza / luce. Punto UNICO per i due ambiti: i
    // valori da mostrare cambiano (globali in "All", della parte in "Mesh"), il
    // modo di mostrarli no. Tenerlo unico e' cio' che impedisce ai due rami di
    // divergere, che e' esattamente com'era nato questo bug.
    // In tutti i casi gli slider si muovono a SEGNALI BLOCCATI, quindi le
    // etichette numeriche vanno scritte a mano: l'unico che le aggiorna e'
    // handleColorChange / i gestori valueChanged, che qui non scattano.
    auto showAppearance = [&](float fr, float fg, float fb, float fa, float fl) {
        const int r = qRound(fr * 255.0f);
        const int g = qRound(fg * 255.0f);
        const int b = qRound(fb * 255.0f);
        setNoSignal(ui->sliderR, r);
        setNoSignal(ui->sliderG, g);
        setNoSignal(ui->sliderB, b);
        ui->valR->setNum(r);
        ui->valG->setNum(g);
        ui->valB->setNum(b);

        if (fa >= 0.0f) {
            setNoSignal(ui->alphaSlider, qRound(fa * 100.0f));
            alphaValue = fa;
            ui->lblAlphaVal->setText(QString::number(fa, 'f', 2));
        }
        if (fl >= 0.0f) {
            setNoSignal(ui->lightSlider, qRound(fl * 100.0f));
            ui->lblValLight->setText(QString::number(qRound(fl * 100.0f)) + " %");
        }
        // NB: NON si tocca m_currentSurfaceColor. Quel membro e' il colore
        // GLOBALE ed e' SALVATO nel preset (presetserializer, chiavi r/g/b e
        // surfColor): scriverci il colore della mesh selezionata significherebbe
        // che basta guardare la mesh 3 e salvare per portarsi via il suo rosso
        // come colore globale della superficie. alphaValue invece si aggiorna
        // perche' non finisce nel preset: e' solo lo stato corrente dello slider.
    };

    const int idx = ui->glWidget->activeMeshPart();
    const auto &parts = ui->glWidget->getEngine()->getMeshParts();
    if (idx < 0 || idx >= (int)parts.size()) {
        // AMBITO "ALL": i comandi agiscono sullo stato GLOBALE, quindi i controlli
        // devono mostrare QUELLO.
        // Prima qui gli slider "restavano dove sono" e si riallineavano solo i
        // radio: tornando da "Mesh" ad "All", colore, trasparenza e luce
        // continuavano a mostrare i valori dell'ultima mesh guardata, mentre
        // muoverli agiva sul globale. Il primo tocco faceva quindi saltare il
        // globale al valore della mesh precedente.
        // I RADIO restano indispensabili per un motivo in piu' (vedi 685100e):
        // uscendo da "Mesh" la guardia showingPart di updateRenderState smette di
        // valere, e quella funzione rilegge i radio come fossero il globale; se
        // restassero sul display della mesh precedente, il globale verrebbe
        // sovrascritto con la modalita' di quella mesh.
        float gr, gg, gb;
        ui->glWidget->globalColor(gr, gg, gb);
        showAppearance(gr, gg, gb,
                       ui->glWidget->globalAlpha(),
                       ui->glWidget->globalLightIntensity());
        refreshRenderRadios();

        // EDITOR: in "All" si comanda la texture GLOBALE, quindi va mostrato il
        // suo script. Senza questo l'editor restava sul codice dell'ultima mesh
        // guardata mentre l'ambito diceva "All": premere Run avrebbe applicato
        // alla superficie intera uno script che apparteneva a una fascia.
        // (shownScriptSlot: in "All" lo slot mostrato e' quello della
        // superficie, e solo se il dock e' sul modulo Texture.)
        refreshScriptEditor();

        // FOCUS DEL DOCK LIBRARY, come nel ramo "Mesh": tornando su "All"
        // l'albero deve evidenziare la texture di SUPERFICIE. L'aggancio stava
        // solo nel ramo per-mesh, quindi passando ad "All" il focus restava
        // sulla texture dell'ultima fascia guardata -- o si perdeva del tutto,
        // perche' syncTextureTreeSelection deseleziona quando non trova
        // corrispondenza.
        if (!editingBackground())
            syncTextureTreeSelection();

        // I colori della texture nei picker non vanno riallineati: li legge
        // surfaceTexColor dal motore, secondo l'ambito corrente.

        // TASTI Run/Stop DEL DOCK SCRIPT, come in fondo al ramo "Mesh": il tasto
        // texture descrive l'ambito corrente, quindi tornando su "All" va
        // ricalcolato sulla texture di SUPERFICIE. Questo ramo esce con return e
        // la chiamata mancava: i tasti restavano fermi all'ultimo stato
        // calcolato per la MESH -- il tasto di "All" diceva "Stop" (stato della
        // fascia in movimento) anche con la texture di superficie ferma.
        updateScriptButtonText();

        // SLIDER R/G/B SUL TARGET GIUSTO, come in fondo al ramo "Mesh": anche qui
        // showAppearance() ha appena scritto negli slider il colore SOLIDO, e con
        // una texture colorata attiva devono invece mostrare u_col1/u_col2.
        // Questo ramo esce con return, quindi la chiamata va ripetuta: senza,
        // bastava passare da "All" per perdere l'allineamento.
        onColorTargetChanged();
        return;
    }

    const MeshPart &p = parts[idx];

    // EDITOR DELLA TEXTURE PER-MESH. Selezionando una parte, l'editor mostra il
    // SUO script: e' lo stesso principio dei radio e degli slider, cioe' i
    // controlli mostrano cio' che stanno per modificare.
    // Solo se il dock e' sul modulo Texture col bersaglio Surface
    // (shownScriptSlot): sul ramo Background o sugli altri moduli l'editor
    // mostra un altro slot.
    // Una parte SENZA texture propria SVUOTA l'editor. Prima lo si lasciava
    // fermo, per non "cancellare la texture della superficie" con un Run: ma
    // quel ragionamento valeva quando una fascia non configurata EREDITAVA la
    // texture globale. Ora non la eredita piu' (vedi effectiveTextureEnabledMulti),
    // quindi lasciare in vista lo script di un'altra fascia e' solo un
    // disallineamento: l'editor mostrava una texture che quella fascia non ha,
    // e un Run la applicava alla fascia sbagliata.
    // Si guarda la texture EFFICACE (propria E accesa), non il solo
    // hasCustomTexture: in wireframe la texture viene SPENTA conservando lo
    // script, e mostrarlo lo stesso rimetterebbe in vista una texture che quella
    // mesh non sta disegnando -- col Run che la riapplicherebbe di soppiatto.
    // Lo script non e' perso: riaccendendo il checkbox torna, editor compreso.
    // Lo slot della fascia si ricarica da solo quando cambia la fascia o la sua
    // texture (syncMeshTextureSlot); qui si riallinea la vista.
    refreshScriptEditor();

    // FOCUS DEL DOCK LIBRARY (ramo Texture) SULLA FASCIA SELEZIONATA. Stessa
    // ragione dell'editor qui sopra: i controlli devono indicare cio' su cui si
    // sta lavorando. syncTextureTreeSelection aveva come soli chiamanti i cambi
    // di modalita' (Base/Phong/WF e Surface/Background), quindi al cambio di
    // mesh l'albero restava fermo sulla texture della fascia precedente -- o su
    // quella globale. Non e' condizionata a ScriptModeTexture: l'albero e' un
    // pannello a se', visibile qualunque cosa mostri l'editor.
    if (!editingBackground())
        syncTextureTreeSelection();

    // COLORI u_col1/u_col2 DELLA PARTE nei picker: nessun riallineamento, li
    // legge surfaceTexColor (i propri della parte, o i globali che eredita).
    // Prima erano copie aggiornate qui solo se la parte ne aveva di propri: su
    // una fascia che li eredita restavano quelli della fascia guardata prima.

    // DISPLAY del checkbox Texture: mostra lo stato EFFICACE della parte (il
    // proprio se dichiarato, altrimenti il globale che sta ereditando), come i
    // radio Base/Phong/Wireframe. A segnali bloccati, o il toggled riscriverebbe
    // sulla parte cio' che stiamo solo mostrando.
    if (!editingBackground() && !implicitMode()) {
        // In MULTI-mesh una parte mai configurata NON eredita la texture
        // globale (resta in tinta unita): il display deve dire la stessa cosa
        // del render, o il checkbox risulterebbe acceso su una fascia che si
        // disegna senza texture. Con una mesh sola vale la regola di sempre.
        // La regola sta in refreshTextureCheckbox (unico punto della vista).
        const bool multi = ui->glWidget->meshPartCount() > 1;
        const bool eff = multi ? p.effectiveTextureEnabledMulti()
                               : p.effectiveTextureEnabled(ui->glWidget->isTextureEnabled());
        refreshTextureCheckbox();

        // PICKER Color1/Color2 RIALLINEATI ALLA PARTE. Il checkbox qui sopra
        // mostra gia' lo stato giusto, ma i due picker non venivano rivalutati
        // da nessuno al cambio di mesh (questa funzione non chiamava
        // updateTextureUIState): restavano accesi con i permessi della mesh
        // PRECEDENTE, cioe' offrivano di editare i colori di una texture che
        // sulla mesh selezionata non esiste.
        // Il flag "reset a Color 1" e' false: qui si sta solo CAMBIANDO
        // selezione, non applicando una texture nuova, quindi la scelta
        // Color1/Color2 dell'utente non va spostata sotto le dita.
        // Passa da updateTextureUIState (punto unico) invece di accendere i
        // due radio a mano: e' la stessa funzione che gia' decide il gating in
        // tutti gli altri contesti, e duplicarne la logica qui la farebbe
        // divergere -- che e' esattamente come sono nati gli altri bug di
        // questa famiglia.
        updateTextureUIState(eff, false);
    }

    // AMBITO "MESH": si mostra il valore PROPRIO della parte se c'e', altrimenti
    // quello globale, che e' cio' che la parte sta ereditando (stessa regola dei
    // radio con effectiveRenderMode). Senza il ramo "eredita", selezionando una
    // mesh senza valori propri i controlli restavano su quelli della mesh
    // precedente, mostrando un aspetto che quella mesh non ha.
    float fr = p.colorR, fg = p.colorG, fb = p.colorB;
    if (!p.hasCustomColor()) ui->glWidget->globalColor(fr, fg, fb);
    const float fa = (p.alpha >= 0.0f) ? p.alpha : ui->glWidget->globalAlpha();
    // LUCE: sempre il valore GLOBALE, anche in ambito "Mesh". Lo slider agisce
    // sull'illuminazione della scena (come la speculare), non sulla fascia:
    // il display deve dire cio' che lo slider scrivera'. Mostrare qui il
    // lightIntensity della parte -- che i preset vecchi possono ancora avere --
    // farebbe saltare lo slider al cambio di mesh su un valore che poi non e'
    // quello che si va a modificare.
    const float fl = ui->glWidget->globalLightIntensity();
    showAppearance(fr, fg, fb, fa, fl);

    // LIMITI u/v DELLA PARTE nei quattro campi del pannello Multi Mesh. Stessa
    // regola di editor, slider e radio qui sopra: i controlli mostrano cio' che
    // stanno per modificare. Il ramo "All" non ha bisogno del gemello -- li'
    // updateMeshScopeEnabled li svuota e li spegne, perche' non esiste una
    // parte a cui riferirli.
    syncMeshLimitFields();

    // DISPLAY della modalita' di rendering: i radio mostrano quella EFFICACE
    // della parte (la propria se dichiarata, altrimenti la globale). Siamo
    // dentro la guardia m_syncingMeshControls, quindi il gestore dei radio
    // riconosce questi setChecked come sincronizzazione e NON li riscrive sulla
    // parte. In un QButtonGroup esclusivo setChecked(true) ne deseleziona un
    // altro, che emette toggled(false): vanno bloccati i segnali di TUTTI i
    // bottoni del gruppo, non solo di quello che si accende.
    // In Ray Marching i radio classici non governano nulla: si lascia stare.
    refreshRenderRadios();

    // La densita' wireframe non ha widget di stato da riallineare: i tasti +/-
    // sono incrementali e leggono il valore corrente della parte selezionata
    // (vedi i loro gestori, che passano da GLWidget::*WireframeUDensity).

    // TASTI Run/Stop DEL DOCK SCRIPT. Stessa ragione di editor, slider, radio e
    // picker qui sopra: in ambito "Mesh" il tasto texture parla della PARTE
    // selezionata (vedi updateScriptButtonText), quindi cambiare mesh ne cambia
    // lo stato. Nessuno lo ricalcolava al cambio di selezione: passando su una
    // fascia in wireframe i tasti si spegnevano -- corretto per QUELLA fascia --
    // e tornando su quella texturizzata restavano spenti, perche' erano fermi
    // all'ultimo stato calcolato. Si "riparavano" solo al primo evento che
    // ricalcola i tasti (applicare un'altra texture, o digitare un carattere:
    // isModified). E' lo stesso difetto gia' corretto poco sopra per i picker
    // Color1/Color2, che al cambio di mesh restavano coi permessi della mesh
    // precedente.
    updateScriptButtonText();

    // SLIDER R/G/B SUL TARGET GIUSTO. **ULTIMA COSA**, e l'ordine e' il punto:
    // showAppearance() qui sopra scrive negli slider il colore della SUPERFICIE
    // (o della parte), sovrascrivendo l'allineamento ai colori della TEXTURE che
    // updateTextureUIState aveva gia' fatto poco prima. Risultato: cambiando mesh
    // (o passando per "All") e tornando indietro, gli slider mostravano il colore
    // solido invece dei col1/col2 della texture.
    // onColorTargetChanged e' il punto unico che sceglie COSA gli slider devono
    // mostrare (colore superficie, u_col1/u_col2, o nulla in wireframe): chiamarla
    // per ultima fa vincere quella decisione su tutto il resto.
    onColorTargetChanged();
}

// L'utente ha cliccato un radio Base/Phong/Wireframe (non e' una
// sincronizzazione). Se una mesh e' selezionata la scelta riguarda SOLO quella;
// con "All" e' la modalita' globale, come da sempre.
void MainWindow::onUserRenderModeChosen()
{
    if (!ui->glWidget) return;
    if (implicitMode()) return;   // Ray Marching: non si applica

    const int mode = ui->radioWF->isChecked()    ? 2
                   : ui->radioPhong->isChecked() ? 1
                                                 : 0;

    const bool editingSingleMesh =
        (ui->glWidget->activeMeshPart() >= 0 && ui->glWidget->meshPartCount() > 1);

    if (editingSingleMesh) {
        // Scrive la modalita' PROPRIA della parte. m_savedRenderMode (lo stato
        // globale, quello che il preset salva e che le altre mesh ereditano)
        // resta invariato: e' la ragione per cui ricaricare il preset non
        // propaga piu' il wireframe a tutte le parti.
        ui->glWidget->setActiveMeshRenderMode(mode);

        // WIREFRAME => TEXTURE SPENTA SU QUESTA MESH. In wireframe la texture
        // non si disegna (il fragment esce prima con colore piatto), quindi
        // lasciarla "accesa" descriveva uno stato che a schermo non esisteva:
        // togliendo il wireframe ricompariva da sola, e per giunta col checkbox
        // deselezionato -- display e realta' che dicevano il contrario.
        // Si SPEGNE conservando lo script (setActiveMeshTextureEnabled):
        // riaccendendo il checkbox si ritrova la propria texture invece di
        // doverla ricaricare. Non si riaccende da se' uscendo dal wireframe:
        // e' l'utente a decidere quando rimetterla.
        if (mode == 2) ui->glWidget->setActiveMeshTextureEnabled(false);

        // DISPLAY RIALLINEATO A OGNI CAMBIO DI MODALITA', non solo entrando in
        // wireframe: uscendone (mode 0/1) la texture resta spenta, e senza
        // questo giro l'editor continuava a mostrare lo script di prima mentre
        // la mesh tornava in tinta unita.
        // syncAppearanceControlsToActiveMesh e' il punto unico del display
        // per-mesh: svuota/riempie l'editor, allinea checkbox, picker e tasti.
        syncAppearanceControlsToActiveMesh();
        // Il clock della parte puo' essersi fermato: il master va rivalutato, o
        // resterebbe su "STOP" per un'animazione che non c'e' piu'.
        updateMasterButtonState();

        // BASE/PHONG VALGONO PER TUTTA LA FIGURA, anche in ambito "Mesh".
        // Non e' un'incoerenza con la riga sopra: sono due cose diverse che
        // condividono gli stessi tre radio.
        //  - WIREFRAME e' una proprieta' della singola griglia: lo shader lo
        //    decide per parte (u_renderMode == 2 sul valore per-parte), quindi
        //    puo' convivere con mesh solide accanto.
        //  - BASE vs PHONG e' solo la SPECULARE, che nell'UBO e' un unico flag
        //    globale (ubuf.useSpecular, da setSpecularEnabled): non esiste "una
        //    mesh in Phong e una in Base", il modello di illuminazione e' uno
        //    per tutta la figura.
        // Prima m_savedRenderMode non veniva toccato qui, e siccome
        // updateRenderState calcola isPhong proprio da lui, cliccare Phong con
        // una mesh selezionata non accendeva nulla: il tasto sembrava morto.
        // Percio' la scelta fra Base e Phong si scrive anche nel globale. Il
        // Wireframe no: quello resta della sola parte, o si perderebbe l'aspetto
        // misto (e cambierebbe il valore salvato nel preset).
        if (mode != 2 && mode != m_savedRenderMode) {
            // Stessa cautela del ramo "All" qui sotto: le parti che EREDITANO
            // seguono il globale, quindi spostarlo le trascina. Il caso vero:
            // globale in wireframe (mesh ereditanti disegnate a fil di ferro),
            // scelgo Phong su UNA mesh -> il globale passa a 1 e tutte le
            // ereditanti uscirebbero dal wireframe, che l'utente non ha chiesto.
            // Congelando prima l'eredita', restano come sono e cambia solo
            // l'illuminazione.
            ui->glWidget->pinInheritedRenderModes();
            m_savedRenderMode = mode;
        }
    } else {
        // AMBITO "ALL": la scelta e' globale, come da sempre. Ma le parti che
        // NON hanno una modalita' propria ereditano dal globale, quindi
        // cambiarlo qui le trascina tutte, e l'aspetto misto impostato per-mesh
        // sparisce appena si torna su "Mesh".
        // Caso segnalato ("Hopf Tori Mesh Colors"): globale = Phong, due mesh
        // con wireframe proprio. Wireframe da "All" -> le altre ereditano il
        // wireframe -> tornando su "Mesh" e' tutto wireframe. I dati per-mesh
        // non erano andati persi: si era spostata la BASE sotto di loro.
        // Percio' PRIMA di muovere il globale si congela nelle parti la
        // modalita' che stavano ereditando: chi non aveva nulla di suo se la
        // prende com'e' (niente cambia a schermo), e resta li' quando il
        // globale si sposta. Cosi' vale anche per il render mode il contratto
        // gia' valido per colore e trasparenza, quello scritto nel tooltip di
        // "All": le impostazioni per-mesh si ritrovano tornando su "Mesh".
        // NB: va fatto solo se il globale cambia davvero. updateRenderState
        // chiama i gestori dei radio anche per ragioni di sola UI, e congelare
        // a vuoto renderebbe "propria" una modalita' che l'utente non ha mai
        // scelto per quelle mesh (smetterebbero di seguire il globale).
        if (mode != m_savedRenderMode)
            ui->glWidget->pinInheritedRenderModes();
        m_savedRenderMode = mode;
    }
}
