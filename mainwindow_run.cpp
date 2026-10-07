// mainwindow_run.cpp - MainWindow: Run, Start e Stop (master e tasti dei dock) e stato
// dell'animazione.
// Parte della classe MainWindow divisa per argomento (mainwindow_p.h).
#include "mainwindow_p.h"


void MainWindow::performMasterStop()
{
    m_masterStopped = true;

    // Ferma le animazioni delle variabili t
    if (ui->glWidget) {
        ui->glWidget->setSurfaceAnimating(false);
        ui->glWidget->setSurfaceTextureAnimating(false);
        ui->glWidget->setBackgroundTextureAnimating(false);
        // OROLOGI PER-MESH: hanno un flag PROPRIO in MeshPart, quindi spegnere i
        // tre clock globali non li tocca. Senza questa riga il master fermava
        // tutto tranne le texture delle fasce, che continuavano a girare: e
        // siccome updateMasterButtonState le considera attivita' in moto, il
        // tasto restava su "STOP" senza avere piu' nulla da fermare.
        ui->glWidget->setAllMeshTexturesAnimating(false);
        // Stop esplicito: alza il gate come gli altri stop, o il primo ricalcolo
        // (commit di equazione, toggle sfondo) le riaccenderebbe. Lo riarma il
        // master Start, che deve poter rimettere in moto qualunque cosa.
        m_userStoppedMeshTexClock = true;
    }

    // Ferma il flusso geodetico
    if (m_geoAnimTimer && m_geoAnimTimer->isActive()) {
        m_geoAnimTimer->stop();
    }

    // Ferma le rotazioni 3D/4D delegando al suo tasto dedicato
    if (ui->glWidget && ui->glWidget->isAnimating()) onStopClicked();

    // Ferma i path delegando ai loro tasti dedicati (che spengono gia' il flag
    // di animazione nel GLWidget). Lo forziamo comunque a false come rete di
    // sicurezza per il watchdog. NB: NON tocchiamo m_isPathFollowing (modalita'
    // camera tangent): deve persistere dopo lo stop, altrimenti la vista
    // tornerebbe a center view al primo render (es. resize/fullscreen).
    if (pathRunning(CameraPaths::Path4D)) onDepartureClicked();
    if (pathRunning(CameraPaths::Path3D)) onDeparture3DClicked();
    if (ui->glWidget) ui->glWidget->setPathAnimating(false);
    updateViewButtonsEnabled();

    // Ferma l'audio
    if (m_audioController && m_audioController->isPlaying()) {
        m_audioController->stopAll();
        updateScriptButtonText();
    }

    // Sincronizza il bottone principale
    updateMasterButtonState();
}

void MainWindow::performEquationsStop()
{
    // Stop del dock Equations: ferma SOLO l'orologio della geometria
    // principale e il flusso geodetico. Texture, sfondo, rotazioni, path e
    // audio restano sotto il controllo del master.
    // Lo stop e' una scelta ESPLICITA dell'utente: il flag impedisce ai
    // ricalcoli globali (applyAnimationState) di riaccendere la geometria
    // finche' un Run/Start non lo riarma.
    m_userStoppedGeomClock = true;
    if (ui->glWidget) {
        ui->glWidget->setSurfaceAnimating(false);
    }

    if (m_geoAnimTimer && m_geoAnimTimer->isActive()) {
        m_geoAnimTimer->stop();
    }

    // Riallinea i pulsanti (il dock torna a "Run", il master a STOP/START a
    // seconda di cosa resta in movimento).
    updateMasterButtonState();
}

bool MainWindow::hasAnyRotationSpeed() const
{
    return std::abs(ui->glWidget->getNutationSpeed()) > 0.001f ||
           std::abs(ui->glWidget->getPrecessionSpeed()) > 0.001f ||
           std::abs(ui->glWidget->getSpinSpeed()) > 0.001f ||
           std::abs(ui->glWidget->getOmegaSpeed()) > 0.001f ||
           std::abs(ui->glWidget->getPhiSpeed()) > 0.001f ||
           std::abs(ui->glWidget->getPsiSpeed()) > 0.001f;
}

void MainWindow::applyStartSideEffects()
{
    if (!ui->glWidget) return;

    // Stessa soglia del tasto Departure (basta un campo, hasPath*Input): col
    // vecchio controllo sul solo campo X il master Start non avviava un path
    // scritto con la sola Y o la sola Z, che il suo tasto invece avvia.
    const bool hasPath4D = hasPath4DInput();
    const bool hasPath3D = hasPath3DInput();

    // Con piu' moti camera disponibili (rotazioni, path 4D, path 3D) riparte
    // SOLO la modalita' corrente (ultimo moto avviato in sessione, o quello
    // salvato nel record via "activeMotion"): la vecchia cascata li accendeva
    // in sequenza e la mutua esclusivita' faceva vincere sempre il path 3D.
    // Se l'indicazione non e' piu' onorabile (campi svuotati) si torna alla
    // cascata storica.
    // (validazione con la stessa soglia del tasto Departure, >=1 campo: il
    // vecchio check sul solo campo X negherebbe un path 4D con X vuota)
    // Come per il suono qui sotto: se l'utente ha fermato ESPLICITAMENTE il
    // moto camera (m_userStoppedCameraMotion), un commit di equazione che
    // arriva qui via onStartClicked NON deve farlo ripartire.
    if (!m_userStoppedCameraMotion) {
        QString pick = m_scene.lastCameraMotion;
        if (pick == "rotation" && !hasAnyRotationSpeed()) pick.clear();
        if (pick == "path4D" && !hasPath4D) pick.clear();
        if (pick == "path3D" && !hasPath3D) pick.clear();

        if (pick == "rotation") {
            if (!ui->glWidget->isAnimating()) onStopClicked();
        } else if (pick == "path4D") {
            if (!pathRunning(CameraPaths::Path4D)) onDepartureClicked();
        } else if (pick == "path3D") {
            if (!pathRunning(CameraPaths::Path3D)) onDeparture3DClicked();
        } else {
            if (hasAnyRotationSpeed() && !ui->glWidget->isAnimating()) {
                onStopClicked();
            }
            if (hasPath4D && !pathRunning(CameraPaths::Path4D)) {
                onDepartureClicked();
            }
            if (hasPath3D && !pathRunning(CameraPaths::Path3D)) {
                onDeparture3DClicked();
            }
        }
    }

    // Non riaccendere il suono se l'utente l'ha fermato esplicitamente: un commit
    // di equazione/texture (Enter ad animazione attiva) arriva qui via onStartClicked
    // e altrimenti lo farebbe ripartire come un master Start.
    if (m_audioController && !m_audioController->isPlaying() && !m_userStoppedSound) {
        const QString codeToAnalyze = sceneAudioSource();
        if (!codeToAnalyze.trimmed().isEmpty()) {
            m_audioController->playFromScript(codeToAnalyze);
            updateScriptButtonText();
        }
    }
}


// ==========================================================
// ANIMATION, MOTION & TIMERS
// ==========================================================

void MainWindow::onStartClicked()
{
    QObject *from = sender();
    if (from == ui->btnRunParametric || from == ui->btnImplicit)
        runScene(RunOrigin::DockRun, static_cast<QPushButton *>(from));
    else if (from && from == m_btnStart)
        runScene(RunOrigin::MasterButton);
    else
        runScene(RunOrigin::Program);
}

void MainWindow::runScene(RunOrigin origin, QPushButton *dockBtn)
{
    // Il master button (START/STOP globale, mai disabilitato) e i Run dei
    // dock restano INERTI durante il REC: rilanciano equazioni e clock di
    // TUTTI i moduli, uno stravolgimento che il loop non deve subire. I
    // singoli moti restano invece comandabili al volo dai loro tasti
    // (GO/Departure/Reset), che il loop legge a ogni frame.
    if (m_isRecording) return;

    // ESITO DEL RUN. Non basta guardare gli errori di COMPILAZIONE: un'equazione
    // mal posta viene quasi sempre fermata prima, dai validatori (parentesi,
    // costanti, limiti, variabili), che mostrano il loro popup e fanno return
    // senza mai arrivare a compilare. Invece di marcare a mano 31 uscite e una
    // dozzina di validatori, si confronta il CONTATORE degli errori mostrati:
    // se non e' cresciuto, il Run e' filato liscio e cio' che l'utente ha
    // scritto e' andato a schermo. RunOutcomeGuard lo rileva all'uscita,
    // qualunque essa sia.
    //
    // Un Run riuscito porta a schermo il testo della superficie, che da li'
    // conta per l'avviso "vuoi salvare?" (noteSurfaceApplied); un Run con errori
    // lascia tutto com'era.
    RunOutcomeGuard runOutcomeGuard(this);

    m_geodesicErrorPending = false;
    m_geoErrorShown = false;   // riarma il popup geodetico per la nuova azione
    if (origin != RunOrigin::ServiceCommit)
        m_collapseErrorShown = false;  // riarma il collasso solo sulle azioni vere (Start/caricamento)

    // Run del dock Equations: agisce SOLO sul modulo equazioni (applica e
    // riavvia il suo orologio), senza toccare rotazioni, path e audio.
    // Vale per il tasto parametrico e per quello implicito (Ray Marching).
    const bool runDockOnly = (origin == RunOrigin::DockRun && dockBtn != nullptr);

    // --- 0. STOP DEL DOCK EQUATIONS ---
    // Se il tasto del dock mostra "Stop", interrompe SOLO l'animazione delle
    // equazioni (orologio geometria + flusso geodetico), lasciando intatti
    // rotazioni, path, texture e audio gestiti dal master.
    if (runDockOnly && dockBtn->text().toUpper() == "STOP") {
        runOutcomeGuard.surfaceApplied = false;   // uno Stop non applica nulla
        performEquationsStop();
        return;
    }

    // --- 1. BLOCCO STOP GLOBALE (MASTER) ---
    if (m_btnStart && m_btnStart->text().toUpper() == "STOP") {
        if (origin == RunOrigin::MasterButton) {
            runOutcomeGuard.surfaceApplied = false;   // uno Stop non applica nulla
            performMasterStop();
            return;
        }
    }

    // --- 2. BLOCCO START GLOBALE (MASTER) / RUN (dock Equations) ---
    const bool masterStart = (m_btnStart && m_btnStart->text().toUpper() == "START"
                              && origin == RunOrigin::MasterButton);
    if (runDockOnly || masterStart) {
        m_masterStopped = false;
        // Run del dock Equations o master Start: entrambi riavviano ESPLICITAMENTE
        // il modulo geometria, quindi riarmano lo stop manuale del suo clock.
        m_userStoppedGeomClock = false;
        snapshotActiveEquations();
    }

    // COMMIT DA INVIO su un campo equazione (RunOrigin::EnterKey, dalla lambda
    // del filtro EnterApplyFilter): non e' ne' runDockOnly ne' masterStart, e
    // non passava dal riarmo qui sopra. Ma
    // premere Invio su un'equazione e' un'intenzione di ESEGUIRE esattamente
    // come premere Run: dopo uno Stop del dock, l'Invio applicava la nuova
    // equazione lasciando il clock spento -- la superficie restava ferma e il
    // tasto su Run/START, e serviva un Run esplicito per ripartire. Le due vie
    // d'ingresso devono equivalersi.
    // Si riarma SOLO il clock della geometria: un commit di equazione non e' un
    // master Start, quindi non tocca suono, texture, sfondo e moti camera (vedi
    // il blocco 'masterStart' qui sotto, che resta l'unico a governarli).
    // SERVE L'ORIGINE ESPLICITA, non "assenza di sender": il Run e' chiamato
    // anche da altri percorsi senza tasto (RunOrigin::Program) -- p.es.
    // handleTextureSelection al cambio scheda -- e riarmare li'
    // riaccenderebbe la geometria che l'utente aveva fermato, che e' la
    // regressione nota "caricare una texture dalla libreria fa ripartire la
    // scena".
    if (origin == RunOrigin::EnterKey) {
        m_userStoppedGeomClock = false;
    }
    // Solo un vero master Start riarma il riavvio automatico del suono (un Run di
    // dock o un commit di equazione NON deve riaccendere un suono fermato a mano).
    // Stessa regola per i clock texture/sfondo: il master governa tutti i moduli,
    // il commit di un'equazione no.
    if (masterStart) {
        m_userStoppedSound = false;
        m_userStoppedTexClock = false;
        m_userStoppedBgClock = false;
        m_userStoppedCameraMotion = false;
        // Anche gli stop delle SINGOLE mesh: il master rimette in moto qualunque
        // cosa, quindi riarma pure il gate che li protegge dai ricalcoli.
        m_userStoppedMeshTexClock = false;
    }

    // ==========================================================
    // AZIONI COMUNI (Evita ripetizioni di codice!)
    // ==========================================================
    ui->glWidget->setFocus();

    if (!validateRunInputs(origin)) return;

    // Costanti a cascata (valida per entrambe le modalità): slider e motore.
    const CascadeConstants kc = pushConstantsToEngine(/*restoreTextOnNegative=*/false, /*always=*/true);

    // SOTTO-TAB TUBES: il Run costruisce il tubo attorno alla curva, qualunque
    // cosa (equazioni o script) ci fosse a schermo prima.
    if (tubesShown()) {
        runSceneTube(origin, runDockOnly, kc);
        return;
    }

    // 2.5. RIPRESA PER GLI SCRIPT
    if (ui->glWidget->getEngine()->isScriptModeActive()) {
        runSceneScript(origin, runDockOnly);
        return;
    }

    // ==========================================================
    // MODALITÀ IMPLICITA (RAY MARCHING)
    // ==========================================================
    if (implicitMode()) {
        runSceneImplicit(origin, runDockOnly);
        return;
    }

    // ==========================================================
    // MODALITÀ PARAMETRICA
    // ==========================================================
    runSceneParametric(origin, runDockOnly, kc, runOutcomeGuard);
}

// ---- Passi di runScene: validazioni comuni, poi un ramo per modalita' ----

bool MainWindow::validateRunInputs(RunOrigin origin)
{
    // --- VALIDAZIONE EQUAZIONI PARAMETRICHE ---
    // Servono almeno 3 dei 4 campi X/Y/Z/P (vedi hasParametricEquationInput).
    // Il tasto Run del dock e' gia' spento in questo caso, ma il master START
    // non si disabilita mai: senza questo controllo costruiva una superficie
    // degenere sui campi rimasti da quella precedente, senza dire perche'.
    // Si controlla solo il ramo PARAMETRICO e solo se la superficie non viene
    // da uno script (che i campi X/Y/Z/P non li usa affatto) ne' da una
    // metrica; il ramo Ray Marching ha la sua equazione in lineEquation.
    {
        const bool isParametricTab = (!implicitMode());
        const bool fromScript = !m_scene.surfaceScriptApplied.trimmed().isEmpty()
                             && m_surfaceOrigin != OriginBoth;
        if (isParametricTab && !fromScript && !tubesShown() && origin != RunOrigin::Load
            && !hasParametricEquationInput()) {
            if (!m_constantPopupActive) {
                m_constantPopupActive = true;
                InputValidator::showIncompleteEquationsError(this);
                // Reset RIMANDATO a fine ciclo di eventi, come per il popup di
                // costante non valida poco sopra. Un solo Invio su un campo
                // costante porta qui DUE volte: il filtro tastiera fa
                // clearFocus(), che emette editingFinished -> commitFieldsOnEnter
                // -> onStartClicked (primo popup), e al ritorno il filtro chiama
                // di nuovo commitFieldsOnEnter -> onStartClicked. Azzerando il
                // flag qui, in modo sincrono, la seconda passata lo trovava
                // false e mostrava un secondo box: chiuso il primo, ne compariva
                // un altro identico. La condizione (meno di 3 campi X/Y/Z/P
                // compilati) e' la stessa in entrambe le passate, quindi il
                // doppione era sistematico.
                QTimer::singleShot(0, this, [this]{ m_constantPopupActive = false; });
            }
            return false;
        }
    }

    // --- VALIDAZIONE COSTANTI (parametriche e implicite) ---
    if (m_constantPopupActive)
        return false;

    auto constParse = [this](const QString& s, bool* ok) {
        return parseUIConstant(s, 0, 0, 0, 0, 0, 0, 0, ok);
    };
    if (!InputValidator::validateConstants(this, {
        {"A", m_scene.constants.a},
        {"B", m_scene.constants.b},
        {"C", m_scene.constants.c},
        {"D", m_scene.constants.d},
        {"E", m_scene.constants.e},
        {"F", m_scene.constants.f},
        {"S", m_scene.constants.s},
    }, constParse)) {
        return false;
    }
    return true;
}

void MainWindow::runSceneScript(RunOrigin origin, bool runDockOnly)
{
    // Lo script com'e' scritto: il master esegue anche cio' che e' in
    // sospeso. Il COMMIT DI SERVIZIO (Invio su una costante ad animazione
    // ferma) no: riapplica lo script a schermo, come per le equazioni, che
    // riusano lo snapshot dell'ultimo Run -- altrimenti un Invio su una
    // costante eseguiva di straforo uno script ancora in lavorazione.
    const bool serviceCommit = (origin == RunOrigin::ServiceCommit)
                               && !m_scene.surfaceScriptApplied.trimmed().isEmpty();
    QString currentScript = serviceCommit ? m_scene.surfaceScriptApplied : m_scene.surfaceScriptText;

    const bool isImplicit = (implicitMode());

    // Sezione opzionale //CUTOUT_BEGIN..//CUTOUT_END (solo parametrico:
    // il Ray Marching non usa getRawPosition, quindi non ha cutout).
    // Stessa estrazione di onRunScriptClicked: qui era rimasta una copia
    // che non la toglieva dal corpo, quindi il blocco CUTOUT finiva
    // iniettato anche in getRawPosition() -> due "return" di tipo diverso
    // nella stessa funzione -> errore di compilazione SOLO da Master Start
    // (onRunScriptClicked, chiamato da Departure/altri percorsi, la toglieva
    // già correttamente).
    QString scriptForGlsl = currentScript;
    if (!isImplicit) {
        QString cutoutGlsl;
        scriptForGlsl = extractCutoutSection(currentScript, &cutoutGlsl);
        ui->glWidget->getEngine()->setCutoutCodeGLSL(cutoutGlsl);

        // Multi-mesh: come il cutout, va riallineato anche qui o la ripresa
        // da Master Start perderebbe le parti dichiarate dallo script.
        std::vector<MeshPart> meshParts;
        scriptForGlsl = extractMeshSections(scriptForGlsl, &meshParts);
        ui->glWidget->getEngine()->setMeshParts(meshParts);
    }

    // Ri-valida lo script corrente prima di riprendere: se contiene un
    // errore non corretto NON facciamo ripartire il moto (come le equazioni).
    QString glslBody;
    QString copy = scriptForGlsl;
    QTextStream stream(&copy);
    while (!stream.atEnd()) {
        QString line = stream.readLine();
        if (line.contains(":=")) continue;
        glslBody.append(line + "\n");
    }
    glslBody = GlslTranslator::translateEquation(glslBody);

    const bool ok = isImplicit
            ? ui->glWidget->validateAndApplyImplicitScript(glslBody)
            : ui->glWidget->validateAndApplyParametricScript(glslBody);

    if (!ok) {
        showShaderError("Script Compilation Error", ui->glWidget->getShaderError());
        return;
    }
    // Compila: e' lo script a schermo, quello che il Save scrive. Prima il
    // master eseguiva lo script in sospeso senza registrarlo, e il Save
    // scriveva ancora quello dell'ultimo Run del dock.
    m_scene.surfaceScriptApplied = currentScript;

    if (implicitMode()) {
        QString texCode = m_scene.rm.texture;
        QString dispCode = m_scene.rm.displacement;

        // Controllo parentesi (popup di "Unmatched parentheses" chiaro)
        if (!InputValidator::validateParentheses(this, stripCodeComments(texCode))) return;
        if (!InputValidator::validateParentheses(this, stripCodeComments(dispCode))) return;

        // Validazione compilazione GLSL completa per texture+displacement
        if (!ui->glWidget->validateAndApplyTextureDisplacement(texCode, dispCode)) {
            showShaderError("Texture/Displacement Compilation Error",
                                                       ui->glWidget->getShaderError());
            return;
        }

    } else {
        // Lo SCRIPT della texture di superficie (surfaceTextureScript): mai
        // l'editor quando mostra una FASCIA. Qui si prendeva l'editor a
        // prescindere, e con una fascia selezionata e il dock sulla texture il
        // Run applicava lo script della fascia a tutta la superficie
        // (sospetto del 2026-08-03, riprodotto dal test degli scenari).
        // L'accensione e' l'intenzione della superficie, non il checkbox, che
        // in ambito Mesh mostra la fascia.
        if (!editingBackground() && m_scene.surfaceTextureState) {
            const QString texSrc = surfaceTextureScript();
            if (TextureCode::hasLogic(texSrc) && !commitSurfaceTextureCode(texSrc)) {
                showShaderError("Syntax Error (Parametric Texture)", ui->glWidget->getShaderError());
                return;
            }
        }
    }

    if (!applyBackgroundTextureIfNeeded()) return;

    {
        QString soundSrc = m_scene.soundScriptText;
        if (!soundSrc.trimmed().isEmpty()) {
            QString audioErr;
            // Nella forma che suonera' (GLSL nudo avvolto): senza marcatori
            // validateScript non trovava niente da compilare e passava tutto.
            if (!m_audioController->validateScript(wrapSoundCode(soundSrc), &audioErr)) {
                showShaderError("Syntax Error (Sound Script)",
                                                           audioErr.isEmpty() ? "Audio shader compilation failed." : audioErr);
                return;
            }
        }
    }


    const bool applyOnly = (origin == RunOrigin::ServiceCommit);

    // Il 't' delle texture PER-MESH non e' in nessuno di questi tre slot: il
    // codice di una fascia vive in MeshPart::textureCode, che
    // m_scene.surfaceTextureCode (la texture di SUPERFICIE) non contiene. Con la
    // scena animata dalle sole fasce -- superficie senza 't', nessuna
    // texture globale, come Clifford 6-tubes -- la condizione era falsa, il
    // master Start NON chiamava applyAnimationState e le texture restavano
    // ferme mentre rotazioni e path ripartivano. Stessa distinzione che
    // allSurfaceTextureCode() documenta: per le fasce si guarda
    // anyMeshTextureCodeAnimated().
    // DUE DOMANDE DISTINTE, e vanno tenute separate.
    // 1. SERVE il ricalcolo? Si', se un modulo QUALSIASI usa il tempo:
    //    geometria, texture di superficie, sfondo o una fascia. Senza,
    //    applyAnimationState non viene chiamata e i clock di texture e
    //    sfondo non ripartono affatto (era il bug del master Start che non
    //    riaccendeva le texture per-mesh).
    // 2. La GEOMETRIA e' animata? Solo se lo script usa il tempo. Questo, e
    //    nient'altro, e' l'argomento di applyAnimationState: da li' esce
    //    setSurfaceAnimating. Passando 'true' perche' si muove un ALTRO
    //    modulo si accendeva il clock della superficie a vuoto, e il tasto
    //    del dock Script diceva "Stop Parametric" su uno script statico --
    //    con nulla da fermare. Bastava uno sfondo animato (Calabi Yau).
    //    I clock degli altri moduli non si perdono: applyAnimationState li
    //    ricalcola da se' dai rispettivi codici ("ogni modulo guarda il
    //    PROPRIO tempo"). Stessa correzione fatta nel load dei record.
    const bool geomHasTime = hasTimeVariable(currentScript);
    const bool otherHasTime = hasTimeVariable(m_scene.surfaceTextureCode + "\n" + m_scene.bgTextureCode)
                              || anyMeshTextureCodeAnimated();
    if (!applyOnly && (geomHasTime || otherHasTime)) {
        applyAnimationState(geomHasTime, runDockOnly);
    }

    if (!applyOnly && !runDockOnly) {
        applyStartSideEffects();
    }
    ui->glWidget->update();
}

void MainWindow::runSceneImplicit(RunOrigin origin, bool runDockOnly)
{
    // 0. LIMITI SPAZIALI X/Y/Z. Si applicano QUI, al Run, insieme
    // all'equazione: sono il taglio della scena, parte di cio' che il Run
    // porta a schermo. Prima entravano da soli all'uscita dal campo, unico
    // pezzo del modulo a non passare dal Run.
    // Un campo illeggibile ferma il Run come un errore di sintassi: il
    // popup lo mostra applySpaceLimits, e la scena resta quella valida.
    if (!applySpaceLimits(/*notify=*/true)) return;

    // 1. Lettura e validazione equazione. Sorgente (editor/radio) dipende
    // dal sotto-tab attivo: "3D" (3 variabili x,y,z) o "Cross Section"
    // (4 variabili x,y,z,p) — vedi CLAUDE.md, i due sotto-tab hanno stato
    // separato, limiti/Run/Variations restano condivisi.
    const bool crossSectionActive = crossSectionTab();
    const RmField rmEqField = crossSectionActive ? &ImplicitTexts::crossSection
                                                 : &ImplicitTexts::equation;

    QString rawEq = (m_scene.rm.*rmEqField).trimmed();
    // CAMPO VUOTO: si avvisa e si esce, non si esce in silenzio. Il return
    // muto lasciava a schermo la superficie PRECEDENTE senza dire nulla --
    // l'utente cancellava l'equazione, premeva Invio e vedeva la scena di
    // prima, senza capire se il Run fosse andato o no. In Cross Section era
    // anche peggio: il campo vuoto arriva al fallback di
    // createImplicitFragmentShader (glwidget ~4511), che sostituisce la
    // stringa vuota con la sfera -- e quella sfera, passando da
    // %CROSS_SECTION_P%, veniva mostrata come sezione ruotata in 4D, cioe'
    // una superficie che nessuno aveva chiesto.
    // Per svuotare la scena c'e' NEW, che e' esplicito.
    if (rawEq.isEmpty()) {
        InputValidator::notifyEmptyImplicitEquation(this, crossSectionActive);
        return;
    }

    const QString eqFieldLabel = crossSectionActive ? "Cross Section Equation" : "Implicit Equation";
    if (!InputValidator::validateImplicitEquation(this, rawEq, /*allowP=*/crossSectionActive)) return;
    if (!InputValidator::validateExpressionSyntax(this, rawEq, eqFieldLabel)) return;
    if (!InputValidator::validateIdentifiers(this, rawEq, eqFieldLabel)) return;

    QString implicitEqF;
    if (rawEq.contains("=")) {
        QStringList parts = rawEq.split("=");
        implicitEqF = QString("(%1) - (%2)").arg(parts[0].trimmed(), parts[1].trimmed());
    } else {
        // Aggiungiamo silenziosamente "= 0.0" se l'utente lo ha omesso, senza fastidiosi popup
        QString correctedEq = rawEq + " = 0.0";
        setRmText(rmEqField, correctedEq);

        implicitEqF = QString("(%1) - (0.0)").arg(rawEq);
    }

    // 2. Lettura e validazione Texture
    QString texCode = m_scene.rm.texture.trimmed();
    QString dispCode = m_scene.rm.displacement.trimmed();

    if (!InputValidator::validateImplicitScriptContext(this, texCode)) return;

    if (!InputValidator::validateParentheses(this, stripCodeComments(texCode))) return;

    if (!InputValidator::validateParentheses(this, stripCodeComments(dispCode))) return;

    ui->glWidget->setTextureCode(texCode);

    // Displacement applicato PRIMA di questo commit (guardia trasparenza mobile).
    const QString prevDispApplied = ui->glWidget->currentDisplacementCode();

    // TEST E APPLICAZIONE
    bool success = ui->glWidget->validateAndApplyImplicitShader(implicitEqF, texCode, dispCode,
                                                                 crossSectionActive);
    if (!success) {
        showShaderError("Syntax Error (Ray Marching)", ui->glWidget->getShaderError());
        return;
    }

    // Mobile: displacement nuovo + trasparenza attiva -> alpha a 1
    // (vedi guardTransparencyOnDisplacementApply).
    guardTransparencyOnDisplacementApply(prevDispApplied);

    // TUTTE le piattaforme: displacement su scena trasparente -> stop del
    // moto + opaco + popup che chiede se ripristinarli. E' il caso del Cross
    // Section di default (alpha 0.75 programmatico), dove nessuna delle
    // guardie sopra puo' scattare: quelle sono #if mobile. Va DOPO l'apply
    // riuscito: agisce sullo stato davvero applicato, e se l'apply fallisce
    // non c'e' nulla di pesante da cui difendersi.
    guardTransparencyOnHeavyTextureApply(dispCode);

    // Stesso Smart Path Resolver del caricamento dei record. Qui c'era un
    // QFile::exists() sul percorso del tag, cioe' la verifica che
    // TextureCode::resolveImagePath ha smesso di fidarsi: sotto sandbox un file
    // della libreria dell'ALTRA app (DMG vs App Store) esiste ma non e'
    // leggibile, exists() lo accettava e l'immagine non si caricava. Ora il
    // percorso si risolve per nome nella libreria in uso. Un file che non
    // c'e' davvero resta muto come prima: questo e' il Run, non un load, e
    // un avviso a ogni pressione sarebbe rumore.
    const QString imgPath = TextureCode::resolveImagePath(texCode);
    if (!imgPath.isEmpty()) {
        if (!imgPath.startsWith("NOT_FOUND|")) {
            ui->glWidget->loadTextureFromFile(imgPath);
        }
    } else if (surfaceHasImage()) {
        // Il campo non ha il tag: nessuna immagine. Prima si guardava un
        // flag, e un'immagine rimasta caricata col flag gia' abbassato non
        // se ne andava piu'.
        ui->glWidget->clearTexture(); // Rimuove l'immagine dalla GPU
    }

    if (texCode.isEmpty() && dispCode.isEmpty()) {
        m_scene.surfaceTextureState = false;
        applySurfaceTextureToEngine();
        refreshTextureCheckbox();
    } else {
        const bool wasOn = surfaceTextureShown();
        m_scene.surfaceTextureState = true;
        applySurfaceTextureToEngine();
        refreshTextureCheckbox();
        if (!wasOn) updateTextureUIState(true, true); // nuova texture -> focus a Colore 1
    }

    // 4. Animazione dinamica sicura. Teniamo separati i due orologi:
    //  - GEOMETRIA: solo l'SDF (implicitEqF).
    //  - TEXTURE: colore + displacement (entrambi leggono dummyZero.x nello
    //    shader) + background. Il displacement NON appartiene alla geometria,
    //    altrimenti un Run riaccoppierebbe i due moduli (bug texture/superficie).
    bool geomAnimated = hasTimeVariable(implicitEqF);
    // La texture di SUPERFICIE, non cio' che mostra il checkbox: col
    // bersaglio su Background quello e' lo sfondo, e a sfondo spento il
    // master Start non riavviava la texture della superficie.
    bool texAnimated = false;
    if (surfaceTextureShown()) {
        texAnimated = hasTimeVariable(texCode) || hasTimeVariable(dispCode);
    }
    if (ui->glWidget->isBackgroundTextureEnabled() && hasTimeVariable(m_scene.bgTextureCode)) {
        texAnimated = true;
    }

    const bool applyOnly = (origin == RunOrigin::ServiceCommit);

    if (!applyOnly) {
        // Il clock GEOMETRIA è del dock Equations/master: lo guida geomAnimated.
        applyAnimationState(geomAnimated, runDockOnly);
        // Il clock TEXTURE è del suo modulo: NON lo tocca il Run del dock
        // Equations (runDockOnly), solo il master/Start globale. E come per
        // il suono, un COMMIT di equazione non riaccende una texture fermata
        // a mano (m_userStoppedTexClock; un vero master Start l'ha già riarmato).
        if (!runDockOnly && ui->glWidget) {
            ui->glWidget->setSurfaceTextureAnimating(texAnimated && !m_masterStopped
                                                     && !m_userStoppedTexClock);
        }
    }
    updateMasterButtonState();

    ui->glWidget->setGlobalRenderMode(implicitShellSelected() ? 1 : 0);

    ui->glWidget->rebuildShader();
    if (!applyOnly && !runDockOnly) {
        applyStartSideEffects();
    }

    // Run "one-shot" Ray Marching: se l'equazione NON è animata (geomAnimated
    // guarda solo l'SDF, non texture/displacement), la modifica è applicata e
    // il tasto si disabilita finché l'equazione non cambia. Con animazione
    // resta Run/Stop (gestito da updateMasterButtonState).
    if (!geomAnimated) {
        m_implicitApplied = true;
        updateMasterButtonState();
    }

    // Equazione implicita committata: risincronizza lo slider trasparenza
    // (campi a prodotto -> disabilitato + popup). Vedi syncImplicitAlphaSlider.
    syncImplicitAlphaSlider(true, true);

    ui->glWidget->update();
}

void MainWindow::runSceneTube(RunOrigin origin, bool runDockOnly, const CascadeConstants &kc)
{
    const TubeTexts &tube = m_scene.tube;

    // 1. LA CURVA: X, Y, Z obbligatori, P facoltativo (vuoto = curva 3D).
    if (tube.x.trimmed().isEmpty() || tube.y.trimmed().isEmpty() || tube.z.trimmed().isEmpty()) {
        if (origin != RunOrigin::Load) InputValidator::showIncompleteEquationsError(this);
        return;
    }
    if (!InputValidator::validateFieldList(this, {
        {"X(u)", tube.x}, {"Y(u)", tube.y}, {"Z(u)", tube.z}, {"P(u)", tube.p},
    })) return;

    // 2. IL DOMINIO DELLA CURVA: i limiti u. v e' l'angolo attorno, 0..2pi.
    const QVector<InputValidator::LimitField> limitFields = {
        {ui->uMinEdit, true}, {ui->uMaxEdit, true},
    };
    QVector<float> lim;
    auto parseFn = [this](const QString &text, bool *ok) { return this->parseLimitField(text, ok); };
    if (!InputValidator::validateAndParseLimits(this, limitFields, parseFn, lim)) return;
    if (!InputValidator::validateLimits(this, lim[0], lim[1], true, 0.0f, 1.0f, false,
                                        0.0f, 1.0f, false)) return;

    // 3. LO SPESSORE: un numero >= 0.
    bool thicknessOk = false;
    tubeThicknessValue(&thicknessOk);
    if (!thicknessOk) {
        QMessageBox::warning(this, QStringLiteral("Tube"),
                             QStringLiteral("The thickness must be a number greater than or equal to 0."));
        return;
    }

    // 4. MOTORE: curva, riferimento della sezione, spessore e dominio.
    if (!applyTubeToEngine(lim[0], lim[1], kc)) {
        showShaderError("Syntax Error (Tube)", ui->glWidget->getShaderError());
        return;
    }
    SurfaceEngine *engine = ui->glWidget->getEngine();

    // 6. OROLOGIO: la curva con 't' anima la geometria.
    const QString curveText = tube.x + " " + tube.y + " " + tube.z + " " + tube.p;
    const bool applyOnly = (origin == RunOrigin::ServiceCommit);
    applyAnimationState(applyOnly ? false : hasTimeVariable(curveText), runDockOnly);
    updateMasterButtonState();

    // 7. MESH e collasso, come le superfici.
    ui->glWidget->updateSurfaceData();
    if (!engine->isMeshValid()) {
        if (!m_collapseErrorShown) {
            m_collapseErrorShown = true;
            InputValidator::showMathematicalCollapseError(this);
        }
        return;
    }
    if (!applyOnly && !runDockOnly) applyStartSideEffects();
    m_collapseErrorShown = false;

    // Run one-shot: senza 't' la modifica e' a schermo, il tasto si spegne
    // finche' la curva non cambia (markUserEdit).
    if (!hasTimeVariable(curveText)) {
        m_parametricApplied = true;
        updateMasterButtonState();
    }
    ui->glWidget->update();
}

bool MainWindow::applyTubeToEngine(float uMin, float uMax, const CascadeConstants &kc)
{
    const TubeTexts &tube = m_scene.tube;

    // ORIENTAMENTO DELLA SEZIONE: una direzione lontana da tutte le tangenti,
    // e se la curva si chiude. Se la curva non si valuta sulla CPU (sintassi
    // che exprtk non capisce) resta l'asse z: lo shader ha comunque la sua
    // riserva per le tangenti parallele.
    QVector3D reference(0.0f, 0.0f, 1.0f);
    bool uClosed = false;
    sampleTubeCurve(tube, uMin, uMax, kc, &reference, &uClosed);

    // Prima la curva (provata prima di toccare lo stato), poi il resto:
    // rebuildShader rifa' le pipeline al fotogramma dopo, coi valori di
    // adesso. Vincoli e composizione non c'entrano col tubo.
    ui->glWidget->setTubeRadius(tubeThicknessValue() * kTubeRadiusUnit);
    if (!ui->glWidget->setTubeCurve(tube.x, tube.y, tube.z, tube.p, reference, uClosed))
        return false;
    SurfaceEngine *engine = ui->glWidget->getEngine();
    engine->setConstraintMode(SurfaceEngine::ConstraintW);
    engine->setExplicitU(QString());
    engine->setExplicitV(QString());
    engine->setExplicitW(QString());
    ui->glWidget->setRangeU(uMin, uMax);
    ui->glWidget->setRangeV(0.0f, 6.28318530718f);
    ui->glWidget->setRangeW(0.0f, 1.0f);

    // Curva 4D: come le superfici con P, una rotazione 4D appena diversa da
    // zero (vedi runSceneParametric).
    const QString pEq = tube.p.trimmed();
    if (!pEq.isEmpty() && pEq != "0" && pEq != "0.0"
        && std::abs(ui->glWidget->getOmega()) < 0.0001f
        && std::abs(ui->glWidget->getPhi()) < 0.0001f
        && std::abs(ui->glWidget->getPsi()) < 0.0001f) {
        const float safety = 0.0001f;
        ui->glWidget->setRotation4D(safety, safety, safety);
    }
    return true;
}

bool MainWindow::hasCompleteTubeInput()
{
    if (m_scene.tube.x.trimmed().isEmpty() || m_scene.tube.y.trimmed().isEmpty()
        || m_scene.tube.z.trimmed().isEmpty()) return false;
    bool okLo = false, okHi = false;
    const float a = parseLimitField(m_scene.lim.uMin.trimmed(), &okLo);
    const float b = parseLimitField(m_scene.lim.uMax.trimmed(), &okHi);
    bool okThickness = false;
    tubeThicknessValue(&okThickness);
    return okLo && okHi && a < b && okThickness;
}

bool MainWindow::sampleTubeCurve(const TubeTexts &tube, float uMin, float uMax,
                                 const CascadeConstants &kc, QVector3D *reference, bool *uClosed)
{
    double u = 0.0, v = 0.0, w = 0.0, p = 0.0;
    ExpressionParser px, py, pz;
    for (ExpressionParser *e : { &px, &py, &pz }) {
        e->setupVariables<double>(u, v, w, p);
        e->setupConstants<double>((double)kc.a, (double)kc.b, (double)kc.c, (double)kc.d,
                                  (double)kc.e, (double)kc.f, (double)kc.s);
    }
    if (!px.compile(tube.x) || !py.compile(tube.y) || !pz.compile(tube.z)) return false;

    // La curva su N+1 punti; le tangenti per differenze centrali.
    constexpr int N = 400;
    std::vector<QVector3D> pts(N + 1);
    for (int i = 0; i <= N; ++i) {
        u = uMin + (double(uMax) - double(uMin)) * i / N;
        const double x = px.value(), y = py.value(), z = pz.value();
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
        pts[i] = QVector3D(float(x), float(y), float(z));
    }
    *uClosed = pts[0].distanceToPoint(pts[N]) < 0.001f;

    std::vector<QVector3D> tangents;
    tangents.reserve(N);
    for (int i = 1; i < N; ++i) {
        const QVector3D d = pts[i + 1] - pts[i - 1];
        if (d.lengthSquared() > 1e-16f) tangents.push_back(d.normalized());
    }
    if (tangents.empty()) return false;

    // CANDIDATI per la direzione di riferimento: il piano della curva se ne
    // ha uno (binormali concordi: per una curva piana sono tutte la normale
    // del piano, ortogonale a ogni tangente), gli assi e una sfera di punti.
    std::vector<QVector3D> candidates;
    QVector3D binormal;
    for (size_t i = 1; i < tangents.size(); ++i) {
        QVector3D c = QVector3D::crossProduct(tangents[i - 1], tangents[i]);
        if (QVector3D::dotProduct(c, binormal) < 0.0f) c = -c;
        binormal += c;
    }
    if (binormal.lengthSquared() > 1e-12f) candidates.push_back(binormal.normalized());
    candidates.push_back(QVector3D(0.0f, 0.0f, 1.0f));
    candidates.push_back(QVector3D(0.0f, 1.0f, 0.0f));
    candidates.push_back(QVector3D(1.0f, 0.0f, 0.0f));
    constexpr int K = 256;                       // punti di Fibonacci sulla sfera
    const float golden = 3.14159265f * (3.0f - std::sqrt(5.0f));
    for (int k = 0; k < K; ++k) {
        const float y = 1.0f - 2.0f * (k + 0.5f) / K;
        const float r = std::sqrt(std::max(0.0f, 1.0f - y * y));
        candidates.push_back(QVector3D(r * std::cos(golden * k), y, r * std::sin(golden * k)));
    }

    // Vince la direzione il cui allineamento PEGGIORE con una tangente e' il
    // piu' piccolo: la sezione ruota piano lungo tutta la curva.
    float bestScore = 2.0f;
    for (const QVector3D &cand : candidates) {
        float worst = 0.0f;
        for (const QVector3D &t : tangents) {
            worst = std::max(worst, std::abs(QVector3D::dotProduct(t, cand)));
            if (worst >= bestScore) break;
        }
        if (worst < bestScore) {
            bestScore = worst;
            *reference = cand;
        }
    }
    return true;
}

void MainWindow::runSceneParametric(RunOrigin origin, bool runDockOnly,
                                     const CascadeConstants &kc, RunOutcomeGuard &outcome)
{
    RunLimits lim;
    bool geodesic = false;
    if (!checkParametricRunInputs(&lim, &geodesic)) return;
    if (geodesic) {
        runSceneGeodesic(runDockOnly);
        return;
    }

    QString pEq = m_scene.eq.p.trimmed();
    bool isSurface4D = !pEq.isEmpty() && pEq != "0" && pEq != "0.0";

    if (isSurface4D) {
        if (std::abs(ui->glWidget->getOmega()) < 0.0001f &&
                std::abs(ui->glWidget->getPhi()) < 0.0001f &&
                std::abs(ui->glWidget->getPsi()) < 0.0001f)
        {
            float safety = 0.0001f;
            ui->glWidget->setRotation4D(safety, safety, safety);
        }
    }

    // Lo SCRIPT della texture di superficie, da applicare piu' sotto insieme
    // alle equazioni. Solo lettura: qui l'editor si copiava nella copia
    // applicata PRIMA di compilarlo (e senza compilarlo affatto se la texture di
    // prima era la scacchiera), e in ambito Mesh ci finiva lo script della fascia.
    const QString currentScript = surfaceTextureScript();

    ui->glWidget->setScriptCheck(false);

    // 2. EQUATIONS E CONSTRAINTS
    //
    // COMMIT DI SERVIZIO (RunOrigin::ServiceCommit): ci arrivano l'Invio su una costante e
    // l'uscita dal suo campo, cioe' percorsi che NON sono un Run. Le equazioni
    // vanno prese dallo SNAPSHOT dell'ultimo Run, non dai campi: lo stesso
    // disaccoppiamento che il ramo geodetico ottiene con useAppliedEquations.
    //
    // Senza, scrivere 't' in un'equazione e poi confermare una costante
    // committava anche quel 't': entrava nella mesh e nello shader mentre il
    // clock restava fermo (applyOnly non lo accende). La superficie non si
    // muoveva -- l'utente vedeva "la costante entra, t no" -- ma i tasti
    // leggevano il testo dei campi e passavano a Stop, e per far partire
    // davvero l'animazione serviva Stop+Start. E' il difetto gemello di quello
    // corretto nel flusso geodetico.
    //
    // Snapshot assente (mai fatto un Run in questa scena): si ricade sui campi,
    // che e' il comportamento storico -- non c'e' un "gia' applicato" da usare.
    const bool serviceCommit = (origin == RunOrigin::ServiceCommit)
                            && m_eqApplied.has_value();
    outcome.equationsApplied = !serviceCommit;
    auto eqField = [this, serviceCommit](EqField f) -> QString {
        return (serviceCommit && m_eqApplied) ? (*m_eqApplied).*f : m_scene.eq.*f;
    };

    // Composizione e vincoli come le equazioni: nel commit di servizio quelli
    // dell'ultimo applicato, non il testo dei campi.
    QString defU = eqField(&EquationTexts::u);
    QString defV = eqField(&EquationTexts::v);
    QString defW = eqField(&EquationTexts::w);

    QString rawX = composeEquation(eqField(&EquationTexts::x), defU, defV, defW);
    QString rawY = composeEquation(eqField(&EquationTexts::y), defU, defV, defW);
    QString rawZ = composeEquation(eqField(&EquationTexts::z), defU, defV, defW);
    QString rawP = composeEquation(eqField(&EquationTexts::p), defU, defV, defW);

    QString xEq = GlslTranslator::translateEquation(rawX);
    QString yEq = GlslTranslator::translateEquation(rawY);
    QString zEq = GlslTranslator::translateEquation(rawZ);
    QString wEq = GlslTranslator::translateEquation(rawP);

    QString rawU = composeEquation(eqField(&EquationTexts::explicitU), defU, defV, defW).trimmed();
    QString rawV = composeEquation(eqField(&EquationTexts::explicitV), defU, defV, defW).trimmed();
    QString rawW = composeEquation(eqField(&EquationTexts::explicitW), defU, defV, defW).trimmed();

    if (!rawU.isEmpty()) {
        ui->glWidget->getEngine()->setConstraintMode(SurfaceEngine::ConstraintU);
        ui->glWidget->getEngine()->setExplicitU(GlslTranslator::translateEquation(rawU));
    }
    else if (!rawV.isEmpty()) {
        ui->glWidget->getEngine()->setConstraintMode(SurfaceEngine::ConstraintV);
        ui->glWidget->getEngine()->setExplicitV(GlslTranslator::translateEquation(rawV));
    }
    else if (!rawW.isEmpty()) {
        ui->glWidget->getEngine()->setConstraintMode(SurfaceEngine::ConstraintW);
        ui->glWidget->getEngine()->setExplicitW(GlslTranslator::translateEquation(rawW));
    }
    else {
        ui->glWidget->getEngine()->setConstraintMode(SurfaceEngine::ConstraintW);
        ui->glWidget->getEngine()->setExplicitU("");
        ui->glWidget->getEngine()->setExplicitV("");
        ui->glWidget->getEngine()->setExplicitW("");
    }

    {

        if (!equationStaysRenderable(rawX, kc, lim) ||
                !equationStaysRenderable(rawY, kc, lim) ||
                !equationStaysRenderable(rawZ, kc, lim) ||
                !equationStaysRenderable(rawP, kc, lim))
        {
            if (!m_collapseErrorShown) {
                m_collapseErrorShown = true;
                InputValidator::showMathematicalCollapseError(this);
            }
            return;
        }
    }

    bool success = ui->glWidget->setParametricEquations(xEq, yEq, zEq, wEq);

    if (!success) {
        InputValidator::showEquationSyntaxError(this);
        return;
    }

    // SNAPSHOT DELL'APPLICATO: le equazioni dei campi sono appena andate a
    // schermo, quindi sono loro "l'ultimo applicato" che il commit di servizio
    // (Invio su una costante) riusera'. Lo snapshot si faceva solo all'inizio
    // dei Run dati dal tasto: un'equazione applicata al volo con Invio durante
    // il moto non ci entrava, e fermata l'animazione l'Invio su una costante
    // riapplicava quella di PRIMA (test degli scenari). Non nel commit di
    // servizio stesso, che ha appena usato lo snapshot e non i campi.
    if (!serviceCommit) snapshotActiveEquations();

    // 3. READ VALUES
    ui->glWidget->setRangeU(lim.uMin, lim.uMax);
    ui->glWidget->setRangeV(lim.vMin, lim.vMax);
    ui->glWidget->setRangeW(lim.wMin, lim.wMax);

    // Le STESSE equazioni appena applicate (congelate sullo snapshot se questo e'
    // un commit di servizio): e' su questo testo che si decidono il clock e il
    // flag one-shot del Run. Leggere i campi qui rimetteva in gioco il 't' non
    // confermato -- i tasti passavano a Stop per un'animazione che non era
    // partita.
    // SOLO i campi del modulo EQUAZIONI. Gli script di texture e sfondo
    // (m_scene.surfaceTextureCode, m_scene.bgTextureCode) NON vanno inclusi: il loro 't'
    // anima il proprio modulo, che ha i suoi clock (setSurfaceTextureAnimating /
    // setBackgroundTextureAnimating), non la geometria.
    // Includendoli, un record con texture o sfondo animati -- es. "Coiled Coil"
    // in records/Paths, che li ha entrambi con iTime -- accendeva al load il
    // clock della GEOMETRIA per un tempo che non le appartiene. Il clock restava
    // acceso ma inerte finche' le equazioni erano statiche; appena l'utente
    // scriveva una 't', isEquationModuleMoving lo trovava gia' acceso e i tasti
    // Run e master passavano a "Stop" senza che l'animazione fosse partita --
    // serviva Stop+Run. La superficie omonima, senza texture animate, non
    // accendeva quel clock e infatti si comportava correttamente.
    // E' la stessa regola gia' applicata al ramo Ray Marching poco sopra e a
    // isEquationModuleMoving: ogni modulo guarda il proprio tempo.
    QString rawEqsForT = eqField(&EquationTexts::x) + " " +
            eqField(&EquationTexts::y) + " " +
            eqField(&EquationTexts::z) + " " +
            eqField(&EquationTexts::p) + " " +
            m_scene.eq.explicitU + " " +
                         m_scene.eq.explicitV + " " +
                         m_scene.eq.explicitW + " " +
                         m_scene.eq.u + " " +
                         m_scene.eq.v + " " +
                         m_scene.eq.w + " " +
                         m_scene.surfaceScriptApplied;

    if (m_scene.surfaceTextureState && TextureCode::hasLogic(currentScript)
        && !commitSurfaceTextureCode(currentScript)) {
        showShaderError("Syntax Error (Parametric Texture)", ui->glWidget->getShaderError());
        return;
    }

    if (ui->glWidget->isBackgroundTextureEnabled()) {
        QString bgSrc = m_scene.bgTextureScriptText;
        bool bgHasLogic = bgSrc.contains("return") || bgSrc.contains("vec3")
                || bgSrc.contains("vec4") || bgSrc.contains("mainImage");
        if (bgHasLogic) {
            if (!ui->glWidget->validateAndApplyBackgroundShader(bgSrc)) {
                showShaderError("Syntax Error (Background Texture)", ui->glWidget->getShaderError());
                return;
            }
            m_scene.bgTextureCode = bgSrc; // valida: committa
        }
    }

    const bool applyOnly = (origin == RunOrigin::ServiceCommit);
    applyAnimationState(applyOnly ? false : hasTimeVariable(rawEqsForT), runDockOnly);
    updateMasterButtonState();

    // 4. REPAINT E VALIDAZIONE GEOMETRIA
    ui->glWidget->updateSurfaceData(); // Calcola la mesh

    // Controllo Collasso Matematico
    if (!ui->glWidget->getEngine()->isMeshValid()) {
        if (!m_collapseErrorShown) {
            m_collapseErrorShown = true;
            InputValidator::showMathematicalCollapseError(this);
        }
        return;
    }

    if (!applyOnly && !runDockOnly) applyStartSideEffects();
    m_collapseErrorShown = false;  // superficie valida: riarma il popup di collasso

    // Run parametrico "one-shot": se le equazioni NON sono animate (nessun 't'),
    // la modifica grafica è ormai applicata e non c'è nulla da rieseguire finché
    // l'utente non cambia di nuovo le equazioni -> disabilitiamo il tasto. Con
    // animazione il tasto resta Run/Stop (gestito da updateMasterButtonState).
    // Il Run one-shot si spegne solo se a schermo c'e' DAVVERO tutto quello che
    // l'utente ha scritto. In un commit di servizio le equazioni sono state
    // congelate sullo snapshot (vedi eqField): se i campi divergono, la
    // modifica NON e' stata applicata e il Run le serve ancora -- spegnerlo la
    // renderebbe irraggiungibile. mapEquationsMatchSnapshot e' la stessa
    // guardia usata dal ramo geodetico.
    // Fuori dai commit di servizio (Run veri) le equazioni vengono dai campi e
    // la condizione e' sempre vera: comportamento invariato.
    const bool everythingApplied = !serviceCommit || mapEquationsMatchSnapshot();
    if (!hasTimeVariable(rawEqsForT) && everythingApplied) {
        m_parametricApplied = true;
        updateMasterButtonState();
    }

    ui->glWidget->update();
}

bool MainWindow::checkParametricRunInputs(RunLimits *lim, bool *geodesic)
{
    // --- 0. SMART INTERCEPTOR ---
    QString allEqs = m_scene.eq.x + " " + m_scene.eq.y + " " +
            m_scene.eq.z + " " + m_scene.eq.p + " " +
            m_scene.eq.u + " " + m_scene.eq.v + " " +
            m_scene.eq.w;

    bool hasExplicit = !m_scene.eq.explicitU.trimmed().isEmpty() ||
            !m_scene.eq.explicitV.trimmed().isEmpty() ||
            !m_scene.eq.explicitW.trimmed().isEmpty();

    if (!InputValidator::validateWUsage(this, allEqs, hasExplicit)) return false;

    // 1. SAFETY CHECK: VARIABLES AND SYNTAX
    QString mainEqs = m_scene.eq.x + " " + m_scene.eq.y + " " +
            m_scene.eq.z + " " + m_scene.eq.p;

    QString allEqsToTest = mainEqs + " " + m_scene.eq.explicitU + " " +
            m_scene.eq.explicitV + " " + m_scene.eq.explicitW;

    if (!InputValidator::validateParametricVariables(this, mainEqs, allEqsToTest, hasExplicit)) return false;

    if (!InputValidator::validateFieldList(this, {
        {"x(u,v)",     m_scene.eq.x},
        {"y(u,v)",     m_scene.eq.y},
        {"z(u,v)",     m_scene.eq.z},
        {"p(u,v)",     m_scene.eq.p},
        {"U-comp",     m_scene.eq.u},
        {"V-comp",     m_scene.eq.v},
        {"W-comp",     m_scene.eq.w},
        {"Explicit U", m_scene.eq.explicitU},
        {"Explicit V", m_scene.eq.explicitV},
        {"Explicit W", m_scene.eq.explicitW},
    }))  return false;

    // --- BLOCCO VALIDAZIONE COMPOSITION & GEODESIC FLOW ---
    QString cU = m_scene.eq.u.trimmed();
    QString cV = m_scene.eq.v.trimmed();
    QString cW = m_scene.eq.w.trimmed();

    bool geoHasText = hasGeodesicText();

    QString composedTest = composeEquation(mainEqs, cU, cV, cW);

    if (!InputValidator::validateCompositionFields(this, mainEqs, cU, cV, cW, geoHasText, composedTest)) return false;

    // --- 1. LETTURA E VALIDAZIONE DEI LIMITI ---
    bool uActive = ui->uMinEdit->isEnabled();
    bool vActive = ui->vMinEdit->isEnabled();
    bool wActive = ui->wMinEdit->isEnabled();

    QVector<InputValidator::LimitField> limitFields = {
        {ui->uMinEdit, uActive}, {ui->uMaxEdit, uActive},
        {ui->vMinEdit, vActive}, {ui->vMaxEdit, vActive},
        {ui->wMinEdit, wActive}, {ui->wMaxEdit, wActive},
    };

    QVector<float> limitValues;
    auto parseFn = [this](const QString& s, bool* ok) { return this->parseLimitField(s, ok); };
    if (!InputValidator::validateAndParseLimits(this, limitFields, parseFn, limitValues)) return false;

    lim->uMin = limitValues[0]; lim->uMax = limitValues[1];
    lim->vMin = limitValues[2]; lim->vMax = limitValues[3];
    lim->wMin = limitValues[4]; lim->wMax = limitValues[5];

    // Controllo distrazione dell'utente (Min >= Max) solo sulle variabili in uso!
    if (!InputValidator::validateLimits(this, lim->uMin, lim->uMax, uActive, lim->vMin, lim->vMax, vActive,
                                        lim->wMin, lim->wMax, wActive)) return false;

    // --- AGGIORNAMENTO UI: Svuota e disabilita i limiti W in modalità Composition ---
    bool has_U = mainEqs.contains(kReUpperU);
    bool has_V = mainEqs.contains(kReUpperV);
    bool has_W = mainEqs.contains(kReUpperW);
    int upperCount = (has_U ? 1 : 0) + (has_V ? 1 : 0) + (has_W ? 1 : 0);

    bool isCompositionActive = ((upperCount > 0) || !cU.isEmpty() || !cV.isEmpty() || !cW.isEmpty()) && !geoHasText;
    *geodesic = (upperCount > 0) && geoHasText;

    if (isCompositionActive) {
        setLineText(m_scene.lim.wMin, QString());
        setLineText(m_scene.lim.wMax, QString());
        ui->wMinEdit->setEnabled(false);
        ui->wMaxEdit->setEnabled(false);
    }
    return true;
}

void MainWindow::runSceneGeodesic(bool runDockOnly)
{
    // 1. Se il campo del fattore conforme è vuoto, forziamo il default "1.0"
    if (m_scene.eq.conform.trimmed().isEmpty()) {
        setEqText(&EquationTexts::conform, QStringLiteral("1.0"));
    }

    // 2. Controllo Geometria Euclidea (avviso "metrica piatta") DISATTIVATO.
    // Scattava quando una coordinata era costante e il fattore conforme non
    // dipendeva da U/V/W (tipicamente Lambda = 1): un caso legittimo e
    // frequente, quindi l'avviso risultava invadente invece che utile.
    // Il validatore e' INTATTO (InputValidator::validateGeodesicConformalFactor,
    // con il suo "disabilita per questa sessione" e resetGeodesicWarning):
    // per riattivarlo basta ripristinare questa chiamata. Se un giorno torna,
    // conviene prima restringere la condizione ai casi davvero sospetti.
//        if (sender() == m_btnStart || runDockOnly) {
//            InputValidator::validateGeodesicConformalFactor(
//                        this,
//                        m_scene.eq.x, m_scene.eq.y,
//                        m_scene.eq.z, m_scene.eq.p,
//                        m_scene.eq.conform,
//                        true
//                        );
//        }

    if (!InputValidator::validateFieldList(this, {
        {"x(U,V,W)",         m_scene.eq.x},
        {"y(U,V,W)",         m_scene.eq.y},
        {"z(U,V,W)",         m_scene.eq.z},
        {"p(U,V,W)",         m_scene.eq.p},
        {"u(t)",             m_scene.eq.geoU},
        {"v(t)",             m_scene.eq.geoV},
        {"w(t)",             m_scene.eq.geoW},
        {"du/dt",            m_scene.eq.geoDU},
        {"dv/dt",            m_scene.eq.geoDV},
        {"dw/dt",            m_scene.eq.geoDW},
    {"Conformal Factor", m_scene.eq.conform},
})) return;

    QString geoEqs = m_scene.eq.x + " " + m_scene.eq.y + " " +
            m_scene.eq.z + " " + m_scene.eq.p + " " +
            m_scene.eq.geoU + " " + m_scene.eq.geoV + " " + m_scene.eq.geoW + " " +
            m_scene.eq.geoDU + " " + m_scene.eq.geoDV + " " + m_scene.eq.geoDW+
            m_scene.eq.conform + " " +
            m_metricScriptBody;   // t può vivere nel corpo della metrica g_ij(U,V,W,t)

    // TEXTURE E SFONDO: servono a decidere se il ricalcolo va FATTO (i loro
    // clock vanno ripristinati anche quando la sola geometria e' statica),
    // ma NON entrano in geoEqs -- che e' il tempo della GEOMETRIA, cioe'
    // l'argomento di applyAnimationState da cui esce setSurfaceAnimating.
    // Mescolandoli, un record geodetico con texture o sfondo animati (Kerr
    // Black Hole, Kruskal Wormhole) accendeva il clock della superficie a
    // vuoto: dopo uno Stop/Start il tasto del dock Script diceva "Stop
    // Parametric" su uno script statico, senza nulla da fermare.
    // Stessa correzione degli altri tre chiamanti.
    QString otherModulesForT;
    if (surfaceTextureShown()) otherModulesForT += " " + m_scene.surfaceTextureCode;
    if (editingBackground() || ui->glWidget->isBackgroundTextureEnabled()) otherModulesForT += " " + m_scene.bgTextureCode;

    // SNAPSHOT DELLE EQUAZIONI APPLICATE. Qui, non solo nel prologo di
    // runScene: quello e' dietro `runDockOnly || masterStart`, cioe'
    // dipende dall'origine, e i percorsi che applicano senza tasto
    // (Invio su un campo equazione, ~3790) lo saltavano. Le equazioni
    // applicate (m_eqApplied) di conseguenza restavano assenti o STANTIE, e chi legge lo snapshot
    // (l'Invio sui limiti e sui 7 campi del flusso) ricadeva sui campi UI:
    // il bug si vedeva a intermittenza, "dopo qualche tentativo" -- cioe'
    // dopo il primo click su Run che aggiornava lo snapshot.
    // Questo e' il punto in cui le equazioni geodetiche vengono APPLICATE
    // davvero: validate qui sopra e subito passate a updateGeodesicMesh.
    snapshotActiveEquations();
    SE_GEO_PROBE("RUN geodetico: snapshot aggiornato X=%s",
                 qPrintable(m_scene.eq.x.simplified()));

    // updateGeodesicMesh() calcola, verifica e restituisce false se i dati sono corrotti
    if (!updateGeodesicMesh()) {
        if (m_geoErrorType == GeoError::Singularity
                && !m_geoErrorShown) {
            m_geoErrorShown = true;
            InputValidator::showGeodesicSingularityError(this);
        }
        return;
    }

    // Condizione LARGA (serve il ricalcolo?), argomento STRETTO (la
    // geometria e' animata?): vedi la nota su otherModulesForT qui sopra.
    // Se nessuno dei due usa il tempo la chiamata si puo' saltare, ma
    // quando la usa un modulo qualsiasi va fatta, o i clock di texture e
    // sfondo non verrebbero ricalcolati affatto.
    const bool geoAnimated = hasTimeVariable(geoEqs);
    if (geoAnimated || hasTimeVariable(otherModulesForT))
        applyAnimationState(geoAnimated, runDockOnly);
    if (!runDockOnly) applyStartSideEffects();

    // Run "one-shot" del flusso geodetico, come nei rami parametrico (~6888) e
    // Ray Marching (~6506): applicata la mesh e senza 't' da animare non c'e'
    // piu' nulla da rieseguire, quindi il tasto si spegne finche' l'utente non
    // tocca di nuovo equazioni o condizioni iniziali. Questo ramo esce col
    // return qui sotto e non raggiungeva il blocco in fondo alla funzione: il
    // flag restava false e il Run del dock Equations rimaneva acceso per sempre.
    if (!geoAnimated) {
        m_parametricApplied = true;
        updateMasterButtonState();
    }

    ui->glWidget->update();
}

bool MainWindow::equationStaysRenderable(const QString &eq, const CascadeConstants &kc,
                                         const RunLimits &lim)
{
    if (eq.trimmed().isEmpty()) return true;  // P vuoto è lecito

    ExpressionParser p;
    double pu = 0.0, pv = 0.0, pw = 0.0, pp_ = 0.0;
    p.setupVariables<double>(pu, pv, pw, pp_);
    p.setupConstants<double>(
                (double)kc.a, (double)kc.b, (double)kc.c, (double)kc.d,
                (double)kc.e, (double)kc.f, (double)kc.s);

    if (!p.compile(eq)) {
        // Errore di sintassi: lo gestisce il controllo successivo.
        return true;
    }

    constexpr int N = 48;  // griglia più fitta: intercetta i poli vicini ai bordi
    QVector<double> mags;
    mags.reserve((N + 1) * (N + 1));
    double maxMag = 0.0;

    for (int i = 0; i <= N; ++i) {
        for (int j = 0; j <= N; ++j) {
            pu = (double)lim.uMin + ((double)lim.uMax - (double)lim.uMin) * i / (double)N;
            pv = (double)lim.vMin + ((double)lim.vMax - (double)lim.vMin) * j / (double)N;
            double val = p.value();

            // 1) inf / NaN: NON blocchiamo subito. Una singolarità RIMOVIBILE
            //    (es. Torus Artifact: z = B*sin(v)/v, che a v=0 dà 0/0=NaN ma
            //    il limite è B, finito) tocca solo i punti esatti della
            //    singolarità mentre i vicini restano limitati: la GPU la
            //    disegna bene. Saltiamo il campione. Un polo VERO (1/(v-π),
            //    tan, csc) ha invece i vicini finiti che esplodono e vengono
            //    presi dal tetto kMax qui sotto.
            if (!std::isfinite(val)) continue;

            // 2) tetto assoluto di sicurezza (oltre la portata float32 GPU):
            //    qui cade il vicinato di un polo vero -> blocco corretto.
            if (std::abs(val) > kMaxRenderableMagnitude) return false;

            double m = std::abs(val);
            maxMag = std::max(maxMag, m);
            mags.push_back(m);
        }
    }

    // Tutti i campioni non finiti: non c'è nulla di renderizzabile.
    if (mags.isEmpty()) return false;

    // 3) Picco RELATIVO alla scala della superficie. Usiamo il 90°
    //    percentile (NON la mediana) come riferimento di scala "tipica":
    //    superfici legittime possono avere oltre metà dei campioni vicini
    //    a zero (es. Koranyi: x = A*pow(max(cos(v),0),B)*cos(u), dove
    //    max(cos(v),0) annulla mezzo dominio in v e cos(u) lo attraversa).
    //    Con la mediana ~0 il rapporto max/mediana esplodeva e bloccava
    //    per errore queste superfici; un polo vero (1/0, tan ai bordi)
    //    supera comunque il p90 di vari ordini di grandezza ed è preso.
    if (!mags.isEmpty()) {
        size_t p90Idx = static_cast<size_t>(0.90 * (mags.size() - 1));
        std::nth_element(mags.begin(), mags.begin() + p90Idx, mags.end());
        double p90Mag = mags[p90Idx];
        if (p90Mag > 1e-9 && maxMag > kSpikeRatio * p90Mag)
            return false;
    }
    return true;
}

void MainWindow::onStopClicked() {
    // NB: funziona anche DURANTE il REC — i timer restano attivi (tick no-op)
    // e il loop legge lo stato vivo a ogni frame, quindi GO/STOP entrano nel
    // video come a schermo.
    bool isRunning = ui->glWidget->isAnimating();

    if (isRunning) {
        ui->glWidget->pauseMotion();
        m_userStoppedCameraMotion = true;
        if (ui->btnStart_2) ui->btnStart_2->setText("GO");
    } else {
        if (!hasAnyRotationSpeed()) return;

        // Mutua esclusivita': avviando GO fermiamo i due percorsi camera.
        stopPathAnimations();

        ui->glWidget->resumeMotion();
        m_scene.lastCameraMotion = "rotation";
        m_userStoppedCameraMotion = false;
        if (ui->btnStart_2) ui->btnStart_2->setText("STOP");
    }

    // Aggiornamento centralizzato per entrambi i rami
    updateMasterButtonState();
    update4DButtonState();
}

bool MainWindow::isEquationModuleMoving() const
{
    if (!ui->glWidget) return false;

    // Flusso geodetico: l'integrazione e' moto a prescindere da 't'.
    if (m_geoAnimTimer && m_geoAnimTimer->isActive()) return true;

    // Geometria animata da 't': serve sia l'orologio acceso sia un 't' che lo
    // usi -- un clock acceso su equazioni statiche non muove niente.
    if (!ui->glWidget->isSurfaceAnimating()) return false;
    return hasTimeVariable(equationModuleCode());
}

QString MainWindow::equationModuleCode() const
{
    if (implicitMode()) {
        // NB: lineVariations (displacement) e' del MODULO TEXTURE, non della
        // geometria: includerlo farebbe credere al dock Equations che la
        // geometria sia in moto ogni volta che la texture anima il displacement.
        return activeImplicitEquationText() + " " + m_scene.surfaceScriptApplied;
    }
    QString code = m_scene.eq.x + " " + m_scene.eq.y + " " +
            m_scene.eq.z + " " + m_scene.eq.p + " " +
            m_scene.eq.u + " " + m_scene.eq.v + " " + m_scene.eq.w + " " +
            m_scene.eq.explicitU + " " + m_scene.eq.explicitV + " " + m_scene.eq.explicitW + " " +
            m_scene.surfaceScriptApplied;
    if (ui->lnU) {
        code += " " + m_scene.eq.geoU + " " + m_scene.eq.geoV + " " + m_scene.eq.geoW +
                " " + m_scene.eq.geoDU + " " + m_scene.eq.geoDV + " " + m_scene.eq.geoDW +
                " " + m_scene.eq.conform;
    }
    return code;
}

MainWindow::MasterActivity MainWindow::masterActivity() const
{
    MasterActivity a;
    GLWidget *gl = ui->glWidget;
    if (!gl) return a;

    // EQUAZIONI: geometria animata da 't' e flusso geodetico, che col 't' parte
    // dal Run (updateGeodesicMesh).
    a.eqAvailable = hasTimeVariable(equationModuleCode());
    a.eqRunning = isEquationModuleMoving();

    // TEXTURE DI SUPERFICIE: stessa condizione con cui START ne accende
    // l'orologio (applyAnimationState: modulo attivo e codice del modulo, nella
    // modalita' corrente, con il tempo). In ambito "Mesh" contano anche le
    // fasce animate; in "All" no, perche' non si disegnano.
    a.texAvailable = surfaceTextureModuleActive() && hasTimeVariable(surfaceTextureModuleCode());
    a.texRunning = a.texAvailable && gl->isSurfaceTextureAnimating();
    if (!implicitMode() && !gl->meshAppearanceUniform() && anyMeshTextureCodeAnimated()) {
        a.texAvailable = true;
        if (gl->anyMeshTextureAnimating()) a.texRunning = true;
    }

    // SFONDO: come START (applyAnimationState).
    a.bgAvailable = gl->isBackgroundTextureEnabled() && hasTimeVariable(m_scene.bgTextureCode);
    a.bgRunning = a.bgAvailable && gl->isBackgroundTextureAnimating();

    // MOTO DELLA CAMERA: rotazioni, path 4D e path 3D si escludono a vicenda e
    // START ne avvia uno (applyStartSideEffects): basta che uno giri.
    a.cameraAvailable = hasAnyRotationSpeed() || hasPath4DInput() || hasPath3DInput();
    a.cameraRunning = isRotationMotionRunning()
                      || pathRunning(CameraPaths::Path4D)
                      || pathRunning(CameraPaths::Path3D);

    // SUONO: START suona sceneAudioSource(), che e' il testo in cui CERCARE le
    // direttive audio (suono, script, texture, sfondo): c'e' un suono solo se
    // vi compare un tag.
    a.audioAvailable = AudioController::containsAudio(sceneAudioSource());
    a.audioRunning = m_audioController && m_audioController->isPlaying();
    return a;
}

bool MainWindow::isRotationMotionRunning() const
{
    if (!ui->glWidget || !ui->glWidget->isAnimating()) return false;
    return !(ui->btnStart_2 && ui->btnStart_2->text() == "GO");
}

void MainWindow::updateMasterButtonState()
{
    if (!m_btnStart) return;

    // In costruzione i sotto-oggetti (es. m_audioController->QMediaPlayer) possono
    // non essere ancora pronti: i textChanged delle equazioni di default
    // arriverebbero qui troppo presto e crasherebbero in Release. La chiamata
    // finale del costruttore (dopo m_uiReady=true) fa il primo allineamento.
    if (!m_uiReady) return;

    // Uscita dalla SCENA VUOTA. Questa funzione gira dopo ogni Run, mentre
    // updateRenderState no (onStartClicked ha 31 uscite e nessuna la chiama):
    // e' il punto in cui accorgersi che la superficie e' tornata. Il gate si
    // apre da solo; l'aspetto (trasparenza, luce, densita', texture) lo
    // ricalcola updateRenderState, che qui si puo' chiamare senza avvitarsi.
    if (m_emptySceneGated && !isSceneEmpty()) {
        applyEmptySceneGating();   // sgancia il gate e riaccende Steps
        updateRenderState();       // rimette il resto secondo le regole normali
    }

    // AUTO-FIX del tasto del dock Script: in modalita' Sound, se l'audio e'
    // finito da solo il tasto torna su "Run Sound".
    if (m_currentScriptMode == ScriptModeSound && ui->btnRunCurrentScript
        && ui->btnRunCurrentScript->text() != "Run Sound"
        && !(m_audioController && m_audioController->isPlaying()))
        ui->btnRunCurrentScript->setText("Run Sound");

    // Lo stato dei moduli, da una sede sola (masterActivity): ne leggono sia i
    // tasti dei dock qui sotto sia il master.
    const MasterActivity act = masterActivity();

    if (ui->glWidget) {
        const bool geomHasTime = act.eqAvailable;

        // Il tasto Run del dock Equations riflette SOLO il modulo equazioni:
        // mostra "Stop" se la geometria o il flusso geodetico sono in moto.
        // Il tasto parametrico e quello implicito condividono lo stesso stato.
        // Stessa definizione usata dai filtri tastiera per decidere se l'Invio
        // applica al volo: una sede sola (isEquationModuleMoving).
        const bool eqModuleMoving = act.eqRunning;

        // Se la superficie è definita da uno SCRIPT (dock Script), la geometria è
        // gestita da lì: il dock Equations non è in uso e i suoi tasti Run/Stop
        // (entrambi i tab) vanno DISABILITATI. Il controllo del modulo passa al
        // tasto Run del dock Script (btnRunCurrentScript).
        // ECCEZIONE, gli script METRICI (OriginBoth): lo script definisce la
        // metrica, ma carta e condizioni iniziali del flusso geodetico stanno nel
        // dock Equations (cfr. updateConstantsUIState, che per questo tiene vivo
        // il tab Geodesic Flow). Trattarli come "superficie da script" spegneva i
        // Run del dock Equations per sempre: si potevano cambiare le condizioni
        // iniziali senza avere un modo per applicarle. Ogni dock abilita il
        // proprio tasto -- lo script il suo, le equazioni i loro.
        bool surfaceFromScript = !m_scene.surfaceScriptApplied.trimmed().isEmpty()
                              && m_surfaceOrigin != OriginBoth;

        if (ui->btnRunParametric) {
            ui->btnRunParametric->setText((eqModuleMoving && (tubesShown() || !surfaceFromScript))
                                          ? "Stop" : "Run");
            // Run "one-shot" senza animazione: quando il modulo non è in moto e la
            // modifica è già stata applicata (m_parametricApplied), il tasto è
            // disabilitato finché le equazioni non cambiano. ECCEZIONE: se l'equazione
            // contiene 't' (t-motion) il Run da fermo NON è un no-op, serve a
            // RIAVVIARE l'animazione del modulo (es. dopo un master STOP), quindi
            // resta abilitato. Sempre disabilitato se la superficie è da script.
            // Servono almeno 3 dei 4 campi X/Y/Z/P: con meno non c'e' una
            // superficie da costruire e il Run produrrebbe una forma degenere
            // sui campi rimasti da quella precedente. Stessa protezione che il
            // tasto DEPARTURE ha da sempre sui campi dei path (hasPath4DInput).
            // Non si applica a moto in corso (li' il tasto e' "Stop") ne' alle
            // superfici da script, gia' escluse da surfaceFromScript.
            // Gate completo: equazioni, dominio e campi del tab attivo (vedi
            // hasCompleteParametricInput). A moto in corso il tasto e' "Stop" e
            // non si valuta l'input: fermare dev'essere sempre possibile.
            // Sotto-tab Tubes: il Run costruisce il tubo, che sostituisce
            // anche una superficie da script; servono la curva e i limiti u.
            const bool tubes = tubesShown();
            const bool eqInputOk = eqModuleMoving
                                   || (tubes ? hasCompleteTubeInput() : hasCompleteParametricInput());
            ui->btnRunParametric->setEnabled((tubes || !surfaceFromScript) && eqInputOk &&
                                             (eqModuleMoving || !m_parametricApplied || geomHasTime));
        }
        if (ui->btnImplicit) {
            ui->btnImplicit->setText((eqModuleMoving && !surfaceFromScript) ? "Stop" : "Run");
            // Stessa logica one-shot del tasto parametrico, con la stessa eccezione
            // t-motion; e disabilitato anch'esso se la superficie è da script.
            ui->btnImplicit->setEnabled(!surfaceFromScript &&
                                        (eqModuleMoving || !m_implicitApplied || geomHasTime));
        }

        if (ui->btnTextureCode) {
            ui->btnTextureCode->setText(act.texRunning ? "Stop" : "Run");

            // Tre stati del Run texture (Ray Marching), alimentato da lineTexture
            // (colore) + lineVariations (displacement):
            //  1. entrambi i campi vuoti -> niente da applicare -> disabilitato;
            //  2. script in moto -> è un tasto Stop, attivo;
            //  3. script statico -> Run one-shot: disabilitato dopo l'applicazione
            //     (m_rmTextureApplied), riabilitato all'edit degli script.
            bool texFieldsEmpty = m_scene.rm.texture.trimmed().isEmpty()
                                  && m_scene.rm.displacement.trimmed().isEmpty();
            ui->btnTextureCode->setEnabled(!texFieldsEmpty
                                           && (act.texRunning || !m_rmTextureApplied));
        }

        // Save texture: attivo solo se c'è del codice/immagine da salvare,
        // disabilitato a campi vuoti (rispecchia i campi letti da saveTexture()).
        if (ui->btnSave) {
            ui->btnSave->setEnabled(hasSavableTexture());
        }
    }


    // MASTER: STOP solo quando TUTTI i moduli accendibili sono accesi -- allora
    // il tasto li ferma tutti; altrimenti START, che accende tutto cio' che e'
    // spento lasciando com'e' cio' che gira. Prima bastava UN modulo in moto
    // per avere STOP: con una parte accesa, accendere il resto voleva due
    // pressioni (STOP, poi START). I singoli moduli si accendono e si spengono
    // coi loro tasti.
    m_btnStart->setText(act.anyRunning() && act.allRunning() ? "STOP" : "START");

    updateScriptButtonText();
}

void MainWindow::applyAnimationState(bool animated, bool dockOnly) {
    const bool effective = animated && !m_masterStopped;

    if (ui->glWidget) {
        // Il clock GEOMETRIA appartiene al modulo equazioni: lo governa sia il
        // master sia il tasto Run del dock Equations. Se l'utente l'ha fermato
        // col tasto Stop del dock, resta fermo finché un Run/Start esplicito
        // non riarma il flag (vedi m_userStoppedGeomClock).
        ui->glWidget->setSurfaceAnimating(effective && !m_userStoppedGeomClock);

        // I clock TEXTURE (colore) e SFONDO appartengono ai rispettivi moduli:
        // il tasto Run del dock NON deve toccarli (regola "ogni tasto non-master
        // agisce solo sul suo modulo"). Solo il master può accenderli/spegnerli.
        if (!dockOnly) {
            // Ogni clock va acceso SOLO se il suo modulo è davvero attivo: 'animated'
            // è un OR su geometria+texture+sfondo, quindi da solo accenderebbe anche
            // un clock il cui modulo è spento. In particolare lo SFONDO disabilitato
            // restava con il clock acceso (m_bgAnimating=true): poi, riattivando la
            // texture di sfondo, updateMasterButtonState lo vedeva "in moto" e il
            // master tornava su STOP facendo "ripartire tutto". Gate esplicito.
            // MULTI-MESH: il modulo e' attivo anche se la texture ce l'ha solo
            // una FASCIA. Ne' il checkbox (che in ambito Mesh e' il display
            // della parte, e in Background quello dello sfondo) ne'
            // m_scene.surfaceTextureState (che e' la sola texture globale) lo dicono:
            // con la texture animata sulla fascia 1 e nessuna globale, entrambi
            // sono false e questo ricalcolo SPEGNEVA il clock. Bastava un click
            // su un bottone che passa di qui -- ad esempio il toggle della
            // texture di sfondo -- per fermare l'animazione della superficie.
            const bool surfTexActive = surfaceTextureModuleActive();
            // Modulo attivo NON basta: se l'utente ha fermato il clock col suo
            // Stop (dock texture/script), il ricalcolo non deve riaccenderlo.
            // Senza questo gate, con path/rotazioni/t-motion in corso (master su
            // STOP) bastava accendere lo sfondo o togglare la checkbox Texture
            // per far ripartire la texture fermata a mano.
            // NB: si guarda m_masterStopped, NON 'effective'. 'animated' descrive
            // la sola GEOMETRIA: da quando rawEqsForT ha smesso (giustamente) di
            // includere gli script di texture e sfondo, il 't' della texture non
            // vi compare piu', e legare il clock TEXTURE a 'effective' lo rendeva
            // ostaggio del tempo di un ALTRO modulo.
            // Sintomo: record con superficie E texture animate, master Stop, poi
            // Start -> la superficie ripartiva e la texture no, ogni volta che
            // 'animated' arrivava false (es. un commit di servizio, che lo forza
            // a false, o una superficie statica con texture animata).
            // Ogni modulo guarda il PROPRIO tempo: qui decidono l'attivita' del
            // modulo texture (surfTexActive), il suo stop manuale e il master.
            // Il codice del modulo NELLA MODALITA' CORRENTE: in Ray Marching sta
            // nei campi Texture e Variations, non in allSurfaceTextureCode() --
            // con quella il conto dava sempre "statica", e ogni ricalcolo che
            // passa di qui (per esempio spegnere lo sfondo) fermava la texture
            // Ray Marching.
            const bool texHasTime = hasTimeVariable(surfaceTextureModuleCode());
            const bool texClockOn = !m_masterStopped && texHasTime
                                    && surfTexActive && !m_userStoppedTexClock;
            ui->glWidget->setSurfaceTextureAnimating(texClockOn);
            // OROLOGI PER-MESH: si SCRIVONO tutti (adozione esplicita), mai per
            // ereditarieta' -- una parte che copiasse il clock globale ne
            // copierebbe anche il freeze e non ripartirebbe piu' da sola.
            // Il gate serve perche' qui non ci passa solo il master: anche il
            // commit di un'equazione, il load di un record, il toggle dello
            // sfondo. Senza, il primo di quei ricalcoli riaccendeva le mesh
            // fermate a mano. Lo riarma il master Start (onStartClicked).
            // La riaccensione e' PER PARTE: una fascia con texture statica non
            // deve restare con l'orologio acceso a vuoto.
            // Le fasce hanno il PROPRIO script e il proprio orologio, quindi non
            // seguono ne' texClockOn (che riguarda la texture di SUPERFICIE) ne'
            // 'animated' (che descrive la GEOMETRIA): ognuna riparte se il SUO
            // codice usa il tempo, e questo lo decide restartAnimatedMeshTextures.
            // Legandole a quei due valori, caricare un record con superficie
            // statica le spegneva tutte -- le texture per-mesh nascevano ferme.
            // L'unico gate e' il master: m_masterStopped ferma tutto, e il master
            // Start riarma m_userStoppedMeshTexClock passando di qui.
            if (!m_userStoppedMeshTexClock) {
                if (!m_masterStopped) restartAnimatedMeshTextures();
                else                  ui->glWidget->setAllMeshTexturesAnimating(false);
            }
            // Stessa regola per lo SFONDO: il suo 't' e' in m_scene.bgTextureCode, che
            // rawEqsForT (e quindi 'animated') non contiene piu'. Legarlo a
            // 'effective' lo faceva dipendere dal tempo della geometria.
            ui->glWidget->setBackgroundTextureAnimating(
                        !m_masterStopped && hasTimeVariable(m_scene.bgTextureCode)
                                  && ui->glWidget->isBackgroundTextureEnabled()
                                  && !m_userStoppedBgClock);
        }
    }

    updateMasterButtonState();
}
