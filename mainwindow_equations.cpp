// mainwindow_equations.cpp - MainWindow: equazioni, costanti A..F/S, limiti u/v/w e taglio
// x/y/z, commit dei campi all'Invio, flusso geodetico.
// Parte della classe MainWindow divisa per argomento (mainwindow_p.h).
#include "mainwindow_p.h"


void MainWindow::checkParametricDependency()
{
    QString eqX = m_eq.x;
    QString eqY = m_eq.y;
    QString eqZ = m_eq.z;
    QString eqP = m_eq.p;

    // Testi espliciti
    QString eqExplU = m_eq.explicitU;
    QString eqExplV = m_eq.explicitV;
    QString eqExplW = m_eq.explicitW;

    // Testi composizione
    QString defU = m_eq.u;
    QString defV = m_eq.v;
    QString defW = m_eq.w;

    // 1. ANALISI RAW: Contiamo ESATTAMENTE cosa ha digitato l'utente
    QString mainEqs = eqX + " " + eqY + " " + eqZ + " " + eqP;

    bool hasRaw_u = mainEqs.contains(kReLowerU);
    bool hasRaw_v = mainEqs.contains(kReLowerV);
    bool hasRaw_w = mainEqs.contains(kReLowerW);
    int rawLowerCount = (hasRaw_u ? 1 : 0) + (hasRaw_v ? 1 : 0) + (hasRaw_w ? 1 : 0);

    bool hasRaw_U = mainEqs.contains(kReUpperU);
    bool hasRaw_V = mainEqs.contains(kReUpperV);
    bool hasRaw_W = mainEqs.contains(kReUpperW);
    int rawUpperCount = (hasRaw_U ? 1 : 0) + (hasRaw_V ? 1 : 0) + (hasRaw_W ? 1 : 0);

    // Variabili di stato per i Tab
    bool needsConstraint = false;
    bool needsComposition = false;
    bool needsGeodesic = false;

    // Controllo se ci sono testi inseriti nelle tab Composition o Geodesic
    bool compHasText = !defU.trimmed().isEmpty() ||
                       !defV.trimmed().isEmpty() ||
                       !defW.trimmed().isEmpty();

   bool geoHasText = hasGeodesicText();

    // Modalità metrica (script che restituisce mat3): la geometria è definita
    // dal tensore g_ij. I campi X/Y/Z/P restano una MAPPA DI VISUALIZZAZIONE
    // (embedding) modificabile: di norma la carta identità x=U,y=V,z=W, ma per
    // certe geometrie (es. paraboloide di Flamm / ponte di Einstein-Rosen) si
    // vuole una mappa esplicita z=±f(U) che pieghi il piano nello spazio 3D.
    // La mappa non altera la metrica: realizza fedelmente la g_ij intrinseca.
    // Constraints resta off (vincoli non hanno senso); Composition resta
    // disponibile per le definizioni intermedie usate dalla mappa.
    const bool metricModeActive = !m_metricScriptBody.trimmed().isEmpty();

    // 2. MACCHINA A STATI: Apertura e Blocco Tab intelligente
    if (ui->panelImplicit) {
        if (metricModeActive) {
            // Vive solo Geodesic Flow: le condizioni iniziali restano l'unico
            // input modificabile dal dock Equations.
            needsGeodesic = true;
            ui->panelImplicit->setTabEnabled(0, false);
            ui->panelImplicit->setTabEnabled(1, false);
            if (ui->panelImplicit->count() > 2) ui->panelImplicit->setTabEnabled(2, true);

            if (ui->panelImplicit->count() > 2 &&
                    !ui->panelImplicit->widget(ui->panelImplicit->currentIndex())->isEnabled()) {
                ui->panelImplicit->setCurrentIndex(2);
            }
        }
        else if (rawLowerCount == 3 && rawUpperCount == 0) {
            // Caso 3 minuscole: Solo Vincoli attivi
            needsConstraint = true;
            ui->panelImplicit->setTabEnabled(0, true);
            ui->panelImplicit->setTabEnabled(1, false); // RIMESSO: Spegne Composition
            if (ui->panelImplicit->count() > 2) ui->panelImplicit->setTabEnabled(2, false); // RIMESSO: Spegne Geodesic

            // Se la tab corrente è disabilitata, allora sposta il focus su Constraints
            if (!ui->panelImplicit->widget(ui->panelImplicit->currentIndex())->isEnabled()) {
                ui->panelImplicit->setCurrentIndex(0);
            }
        }
        else if (rawUpperCount == 3 && rawLowerCount == 0) {
            // Caso 3 maiuscole: Esclusione mutua tra Composition e Geodesic
            ui->panelImplicit->setTabEnabled(0, false); // Vincoli sempre OFF

            if (geoHasText) {
                // Sto scrivendo in Geodesic: blocco Composition
                needsGeodesic = true;
                ui->panelImplicit->setTabEnabled(1, false);
                if (ui->panelImplicit->count() > 2) ui->panelImplicit->setTabEnabled(2, true);
            }
            else if (compHasText) {
                // Sto scrivendo in Composition: blocco Geodesic
                needsComposition = true;
                ui->panelImplicit->setTabEnabled(1, true);
                if (ui->panelImplicit->count() > 2) ui->panelImplicit->setTabEnabled(2, false);
            }
            else {
                // Entrambi vuoti: entrambi abilitati e pronti all'uso
                needsComposition = true;
                needsGeodesic = true;
                ui->panelImplicit->setTabEnabled(1, true);
                if (ui->panelImplicit->count() > 2) ui->panelImplicit->setTabEnabled(2, true);
            }

            // Gestione Focus dolce
            if (!ui->panelImplicit->widget(ui->panelImplicit->currentIndex())->isEnabled()) {
                if (compHasText) ui->panelImplicit->setCurrentIndex(1);
                else if (geoHasText) ui->panelImplicit->setCurrentIndex(2);
                else ui->panelImplicit->setCurrentIndex(1); // Default su Composition
            }
        }
        else {
            // Qualsiasi altra combinazione (incluse solo 2 maiuscole/minuscole): TUTTO SPENTO
            ui->panelImplicit->setTabEnabled(0, false);
            ui->panelImplicit->setTabEnabled(1, false);
            if (ui->panelImplicit->count() > 2) ui->panelImplicit->setTabEnabled(2, false);

            // Con tutti i tab spenti (es. superficie parametrica normale solo
            // u,v) mostra COMUNQUE Constraints, non Geodesic Flow: cosi' non
            // resta in vista il Conformal Factor con l'1.0 di default, inutile
            // qui. Solo estetico: i tab restano tutti disabilitati.
            ui->panelImplicit->setCurrentIndex(0);
        }
    }

    // 2.5 In modalità metrica i campi X/Y/Z/P restano editabili come mappa di
    // visualizzazione (embedding): vuoti => carta identità, oppure una mappa
    // esplicita z=±f(U) per piegare il piano (es. Flamm). Non vanno disabilitati.

    // 3. Applica stato fisico ai campi dei Vincoli
    ui->lineExplicitU->setEnabled(needsConstraint);
    ui->lineExplicitV->setEnabled(needsConstraint);
    ui->lineExplicitW->setEnabled(needsConstraint);

    // 4. Applica stato fisico ai campi Composition
    ui->lineU->setEnabled(needsComposition && hasRaw_U);
    ui->lineV->setEnabled(needsComposition && hasRaw_V);
    ui->lineW->setEnabled(needsComposition && hasRaw_W);

    // 5. Applica stato fisico ai campi Geodesic
    if (ui->lnU) {
        ui->lnU->setEnabled(needsGeodesic);
        ui->lnV->setEnabled(needsGeodesic);
        ui->lnW->setEnabled(needsGeodesic);
        ui->lndU->setEnabled(needsGeodesic);
        ui->lndV->setEnabled(needsGeodesic);
        ui->lndW->setEnabled(needsGeodesic);
    }

    // 6. ANALISI COMPOSTA: Accensione degli Slider (Rigidamente Case-Sensitive!)
    QString allEqs = mainEqs + " " + eqExplU + " " + eqExplV + " " + eqExplW;
    QString composedAllEqs = composeEquation(allEqs, defU, defV, defW);

    bool isGeodesicActive = (rawUpperCount > 0) && geoHasText && (!implicitMode());

    if (ui->stepSlider->maximum() != 1000) {
        ui->stepSlider->setMaximum(1000);
    }

    updateConstraintState();
    updateConstantsUIState();
}

void MainWindow::updateConstraintState()
{
    QString txtU = m_eq.explicitU.trimmed();
    QString txtV = m_eq.explicitV.trimmed();
    QString txtW = m_eq.explicitW.trimmed();

    bool hasConstraintU = !txtU.isEmpty();
    bool hasConstraintV = !txtV.isEmpty();
    bool hasConstraintW = !txtW.isEmpty();

    QString defU = m_eq.u;
    QString defV = m_eq.v;
    QString defW = m_eq.w;

    QString allMainEqs = m_eq.x + " " +
                         m_eq.y + " " +
                         m_eq.z + " " +
                         m_eq.p;

    QString composedEqs = composeEquation(allMainEqs, defU, defV, defW);

    bool usesU = composedEqs.contains(kReLowerU);
    bool usesV = composedEqs.contains(kReLowerV);
    bool usesW = composedEqs.contains(kReLowerW);

    // --- Disabilitazione e svuotamento Limiti W in modalità Geodesic Flow / Composition ---
    int upperCount = (allMainEqs.contains(kReUpperU) ? 1 : 0) +
            (allMainEqs.contains(kReUpperV) ? 1 : 0) +
            (allMainEqs.contains(kReUpperW) ? 1 : 0);

    bool geoHasText = hasGeodesicText();

    bool isGeodesicActive = (upperCount > 0) && geoHasText && (!implicitMode());

    // Aggiungiamo la rilevazione per la modalità Composition
    bool isCompositionActive = (upperCount > 0 || !defU.trimmed().isEmpty() || !defV.trimmed().isEmpty() || !defW.trimmed().isEmpty()) && !geoHasText && (!implicitMode());

    if (isGeodesicActive || isCompositionActive) {
        usesW = false;
    }

    // Modalità metrica: u e v parametrizzano il fascio di geodetiche e il
    // tempo di integrazione, non la mappa di visualizzazione. I limiti devono
    // restare attivi anche se la mappa non cita una delle variabili (es. una
    // carta alla Flamm senza V), altrimenti si svuotano e il flusso non parte.
    if (!m_metricScriptBody.trimmed().isEmpty() &&
            !implicitMode()) {
        usesU = true;
        usesV = true;
        usesW = false;
    }

    auto applyLimitsState = [](QLineEdit* minEdit, QLineEdit* maxEdit, bool enable) {
        minEdit->setEnabled(enable);
        maxEdit->setEnabled(enable);
        if (!enable) {
            minEdit->clear();
            maxEdit->clear();
        }
    };

    if (hasConstraintU) {
        UiStyleManager::applyConstraintStyle(ui->lineExplicitU, UiStyleManager::ConstraintState::Active);
        UiStyleManager::applyConstraintStyle(ui->lineExplicitV, UiStyleManager::ConstraintState::Inactive);
        UiStyleManager::applyConstraintStyle(ui->lineExplicitW, UiStyleManager::ConstraintState::Inactive);

        applyLimitsState(ui->uMinEdit, ui->uMaxEdit, false);
        applyLimitsState(ui->vMinEdit, ui->vMaxEdit, usesV);
        applyLimitsState(ui->wMinEdit, ui->wMaxEdit, usesW);

        ui->glWidget->getEngine()->setConstraintMode(SurfaceEngine::ConstraintU);
    }
    else if (hasConstraintV) {
        UiStyleManager::applyConstraintStyle(ui->lineExplicitV, UiStyleManager::ConstraintState::Active);
        UiStyleManager::applyConstraintStyle(ui->lineExplicitU, UiStyleManager::ConstraintState::Inactive);
        UiStyleManager::applyConstraintStyle(ui->lineExplicitW, UiStyleManager::ConstraintState::Inactive);

        applyLimitsState(ui->vMinEdit, ui->vMaxEdit, false);
        applyLimitsState(ui->uMinEdit, ui->uMaxEdit, usesU);
        applyLimitsState(ui->wMinEdit, ui->wMaxEdit, usesW);

        ui->glWidget->getEngine()->setConstraintMode(SurfaceEngine::ConstraintV);
    }
    else {
        if (hasConstraintW) {
            UiStyleManager::applyConstraintStyle(ui->lineExplicitW, UiStyleManager::ConstraintState::Active);
            UiStyleManager::applyConstraintStyle(ui->lineExplicitU, UiStyleManager::ConstraintState::Inactive);
            UiStyleManager::applyConstraintStyle(ui->lineExplicitV, UiStyleManager::ConstraintState::Inactive);

            applyLimitsState(ui->wMinEdit, ui->wMaxEdit, false);
        } else {
            if (usesW) {
                UiStyleManager::applyConstraintStyle(ui->lineExplicitW, UiStyleManager::ConstraintState::Default);
                UiStyleManager::applyConstraintStyle(ui->lineExplicitU, UiStyleManager::ConstraintState::Default);
                UiStyleManager::applyConstraintStyle(ui->lineExplicitV, UiStyleManager::ConstraintState::Default);
            } else {
                UiStyleManager::applyConstraintStyle(ui->lineExplicitW, UiStyleManager::ConstraintState::Disabled);
                UiStyleManager::applyConstraintStyle(ui->lineExplicitU, UiStyleManager::ConstraintState::Disabled);
                UiStyleManager::applyConstraintStyle(ui->lineExplicitV, UiStyleManager::ConstraintState::Disabled);
            }
            applyLimitsState(ui->wMinEdit, ui->wMaxEdit, usesW);
        }

        applyLimitsState(ui->uMinEdit, ui->uMaxEdit, usesU);
        applyLimitsState(ui->vMinEdit, ui->vMaxEdit, usesV);

        ui->glWidget->getEngine()->setConstraintMode(SurfaceEngine::ConstraintW);
    }
}

// Le costanti che NON appartengono alla superficie: non citate dalle equazioni
// (o dall'equazione implicita in RM), ne' dai campi geodetici, dai limiti u/v/w
// o dai path camera. Sono quelle che un caricamento di texture puo' riportare al
// default senza deformare nulla di cio' che e' gia' a schermo.
//
// Il criterio e' lo STESSO di updateConstantsUIState -- mathText, match
// case-insensitive sui campi exprtk, commenti esclusi -- perche' li' una
// costante citata da un limite o da un path e' "usata" e non va toccata: la
// stessa ragione vale qui, dove il rischio non e' uno slider spento a torto ma
// una superficie che cambia forma da sola.
QSet<QString> MainWindow::constantsNotUsedBySurface() const
{
    QString mathText;

    if (!implicitMode()) {
        mathText = m_eq.x + " " + m_eq.y + " " +
                   m_eq.z + " " + m_eq.p + " " +
                   m_eq.explicitU + " " + m_eq.explicitV + " " +
                   m_eq.explicitW + " " + m_eq.u + " " +
                   m_eq.v + " " + m_eq.w;
        if (ui->lnU) {
            mathText += " " + m_eq.geoU + " " + m_eq.geoV +
                        " " + m_eq.geoW + " " + m_eq.geoDU +
                        " " + m_eq.geoDV + " " + m_eq.geoDW +
                        " " + m_eq.conform;
        }
        // Lo SCRIPT della superficie parametrica e' GLSL, ma e' comunque della
        // SUPERFICIE: una costante che vi compare non va resettata.
        mathText += " " + stripCodeComments(m_surfaceScriptText + "\n" + m_surfaceScriptApplied);
    } else {
        // Sotto-tab ATTIVO, non lineEquation fisso: il criterio dev'essere lo
        // STESSO di updateConstantsUIState (vedi il commento sopra), che legge
        // gia' l'equazione del sotto-tab a schermo. Divergendo, le costanti usate
        // solo dal Cross Section (A/B/C del T^3) risultavano "non usate dalla
        // superficie" e un caricamento di texture le riportava al default,
        // deformando la superficie 4D a schermo.
        mathText = stripCodeComments(activeImplicitEquationText());
        // Idem per lo script implicito in Ray Marching.
        mathText += " " + stripCodeComments(m_surfaceScriptText + "\n" + m_surfaceScriptApplied);
    }

    // Limiti e path: valutati con A..F/S registrate, quindi contano come uso.
    mathText += " " + m_lim.uMin + " " + m_lim.uMax +
                " " + m_lim.vMin + " " + m_lim.vMax +
                " " + m_lim.wMin + " " + m_lim.wMax;
    // Stessa regola per il taglio x/y/z del Ray Marching.
    mathText += " " + m_lim.xMin + " " + m_lim.xMax +
                " " + m_lim.yMin + " " + m_lim.yMax +
                " " + m_lim.zMin + " " + m_lim.zMax;
    mathText += " " + m_path.x + " " + m_path.y +
                " " + m_path.z + " " + m_path.p +
                " " + m_path.alpha + " " + m_path.beta +
                " " + m_path.gamma +
                " " + m_path.x3D + " " + m_path.y3D +
                " " + m_path.z3D + " " + m_path.roll3D;

    // TEXTURE DELLE FASCE: non sono la superficie, ma le costanti che usano non
    // vanno riportate al default quando si carica una texture GLOBALE -- le
    // fasce continuano a disegnare, e se ne ritroverebbero una cambiata (la F
    // della densita' di Static Holography sui labirinti di Clifford). Sono GLSL:
    // regola case-sensitive, non il match di mathText.
    const QStringList meshCodes = meshTextureCodesForConstants();

    QSet<QString> free;
    for (const QString &letter : { QStringLiteral("A"), QStringLiteral("B"),
                                   QStringLiteral("C"), QStringLiteral("D"),
                                   QStringLiteral("E"), QStringLiteral("F"),
                                   QStringLiteral("S") }) {
        QRegularExpression re("\\b" + letter + "\\b",
                              QRegularExpression::CaseInsensitiveOption);
        if (mathText.contains(re)) continue;
        bool byMesh = false;
        for (const QString &c : meshCodes)
            if (glslUsesConstant(c, letter)) { byMesh = true; break; }
        if (!byMesh) free.insert(letter);
    }
    return free;
}

// Le texture delle FASCE contano come "uso" delle costanti, esattamente come la
// texture globale. Prima non contavano: una costante citata SOLO da una fascia
// finiva nel ramo !used di updateConstantsUIState, che la riporta a 1 e spegne
// lo slider. Emerso spostando il raggio dei Clifford Labyrinth da F ad A: F
// restava della sola Static Holography delle fasce, e all'apertura del record
// tornava a 1 (densita' diversa da quella salvata) con lo slider bloccato.
// TUTTE le parti con texture propria, anche spente e anche in ambito "All" (dove
// sono sospese): tornano a disegnare con i valori di adesso, e azzerarli ora
// vorrebbe dire ritrovarle cambiate. Al peggio uno slider resta acceso senza
// effetto visibile, mai il contrario.
// Anche m_pendingMeshParts: durante il load di un record le parti possono essere
// ancora li' (applyPendingMeshAppearance non e' passata), e gli slider si
// ricalcolano prima. Il vettore si svuota appena applicato, quindi non e' mai
// il residuo di un record precedente.
// Solo in parametrico: in Ray Marching le fasce non esistono.
QStringList MainWindow::meshTextureCodesForConstants() const
{
    QStringList out;
    if (implicitMode()) return out;
    auto add = [&out](const MeshPart &mp) {
        if (mp.hasCustomTexture && !mp.textureCode.trimmed().isEmpty())
            out << stripCodeComments(mp.textureCode);
    };
    if (ui->glWidget && ui->glWidget->getEngine())
        for (const MeshPart &mp : ui->glWidget->getEngine()->getMeshParts()) add(mp);
    for (const MeshPart &mp : m_pendingMeshParts) add(mp);
    return out;
}

// GLSL: conta solo la MAIUSCOLA (il case delle costanti iniettate), e solo se la
// lettera non e' dichiarata come variabile dello shader, singola ("vec2 F =
// fragCoord") o in lista ("float i = 0.0, S = 0.0"): li' e' la locale, non la
// costante. Dichiarazioni con inizializzatori a chiamata di funzione in lista
// sfuggono al pattern: al peggio lo slider resta attivo (status quo), mai il
// contrario.
bool MainWindow::glslUsesConstant(const QString &glsl, const QString &letter)
{
    const QRegularExpression reGlsl("\\b" + letter + "\\b");
    if (!glsl.contains(reGlsl)) return false;
    const QRegularExpression reDecl(
        "\\b(?:float|int|uint|bool|vec[234]|mat[234])\\s+"
        "(?:\\w+\\s*(?:=[^,;()]*)?,\\s*)*" + letter + "\\b");
    return !glsl.contains(reDecl);
}

void MainWindow::updateConstantsUIState() {
    // Due nature di testo, due regole di match:
    // - mathText: campi exprtk (equazioni, geodetica, path). Case-INSENSITIVE
    //   come sempre: 'a' e 'A' sono la stessa costante.
    // - glslText: codice shader (texture, sfondo, editor script GLSL). Nel
    //   GLSL le costanti sono iniettate come 'float A..F' e 'S' (case-
    //   sensitive): le minuscole a..f di uno shader NON sono mai le costanti,
    //   e una lettera DICHIARATA come variabile locale (es. 'vec2 F =
    //   fragCoord') e' la locale che ombreggia la costante, non un uso. Il
    //   vecchio match unico case-insensitive accendeva gli slider a vuoto su
    //   record senza costanti (float a/b/c/d/s locali negli shader).
    QString mathText = "";
    QString glslText = "";
    // MODIFICHE DELL'UTENTE NON ANCORA ESEGUITE. Quando il ricalcolo parte da
    // una digitazione (il segnale di un campo di testo, fuori dal riempimento
    // di un preset), cio' che e' scritto non e' piu' cio' che e' a schermo: da
    // qui al prossimo Run conta anche l'applicato (vedi piu' sotto). Il flag
    // lo spengono refreshConstants -- cioe' load, reset, scelte in Library e
    // Run riuscito -- perche' li' campi e schermo tornano a coincidere.
    // I campi multilinea emettono textChanged solo per la digitazione (il
    // programma li scrive a segnali bloccati); quelli a una riga (limiti, path)
    // anche per setText e clear, quindi per loro lo segna il loro textEdited,
    // che Qt emette PRIMA di textChanged.
    if (m_uiReady && qobject_cast<QPlainTextEdit *>(sender()))
        m_constantsEditPending = true;
    // Ray Marching con il campo equazione CANCELLATO: il ramo !used disabilita
    // le costanti ma NON le resetta (vedi la nota dov'e' usata).
    bool equationFieldIsEmpty = false;
    int currentTab = implicitMode() ? 1 : 0;

    // 1. RACCOLTA TESTO SPECIFICA PER TAB
    if (currentTab == 0) { // MODALITÀ PARAMETRICA
        mathText = m_eq.x + " " + m_eq.y + " " +
                   m_eq.z + " " + m_eq.p + " " +
                   m_eq.explicitU + " " + m_eq.explicitV + " " +
                   m_eq.explicitW + " " + m_eq.u + " " +
                   m_eq.v + " " + m_eq.w;

        if (ui->lnU) { // Campi Geodetici (Tab 0)
            mathText += " " + m_eq.geoU +
                    " " + m_eq.geoV +
                    " " + m_eq.geoW +
                    " " + m_eq.geoDU +
                    " " + m_eq.geoDV +
                    " " + m_eq.geoDW +
                    " " + m_eq.conform;
        }
        // In parametrica aggiungiamo lo script della superficie se non siamo in Ray Marching
        glslText += stripCodeComments(m_surfaceTextureCode);
        // ...e le texture delle FASCE: una costante citata solo da una di loro e'
        // usata quanto una della texture globale (vedi
        // meshTextureCodesForConstants). Blocco per blocco, come qui sotto.
        for (const QString &c : meshTextureCodesForConstants())
            glslText += " " + c;

        // CIO' CHE E' A SCHERMO, oltre a cio' che e' scritto. Una costante cade
        // in disuso -- e torna al valore neutro -- solo quando non la usa ne'
        // l'applicato ne' il testo dei campi. Guardando i soli campi, riscrivere
        // un'equazione senza B la riportava a 1 PRIMA del Run, con a schermo la
        // superficie che B la usava ancora; rimessa B nell'equazione, il suo
        // valore era perso (deciso con l'utente il 2026-10-01, trovato dal test
        // degli scenari). Le fonti: le equazioni compilate nel motore
        // (composizione compresa) e lo snapshot dell'ultimo Run per il flusso
        // geodetico. Lo script di superficie e' gia' coperto piu' sotto dal suo
        // slot m_surfaceScriptText.
        // SOLO con modifiche in sospeso: a campi e schermo allineati l'applicato
        // non aggiunge nulla, e durante un load e' ancora quello del preset di
        // PRIMA (in modo script le equazioni compilate restano le vecchie; il
        // load di una superficie le applica in differita) -- contarlo sempre
        // rendeva le costanti dipendenti dal preset caricato prima (round-trip).
        if (m_constantsEditPending) {
            mathText += " " + activeEquationsText();
            const bool scriptSurface = ui->glWidget && ui->glWidget->getEngine()
                                       && ui->glWidget->getEngine()->isScriptModeActive();
            if (ui->glWidget && !scriptSurface)
                glslText += " " + ui->glWidget->parametricEquationsApplied();
        }
        // ...e lo script della texture non ancora eseguito (l'applicato e'
        // m_surfaceTextureCode, qui sopra).
        glslText += " " + stripCodeComments(m_surfaceTextureScriptText);
    }
    else { // MODALITÀ RAY MARCHING
        // L'equazione implicita e' matematica utente (translateEquation);
        // texture e variations sono snippet GLSL. Sorgente dell'equazione
        // dipende dal sotto-tab attivo (3D vs Cross Section, stato separato
        // — vedi CLAUDE.md), altrimenti le costanti usate SOLO nell'equazione
        // Cross Section (es. A/B/C del T^3) risultavano "non usate" e
        // updateControl le azzerava/disabilitava a vuoto.
        mathText = stripCodeComments(activeImplicitEquationText());
        // Equazione CANCELLATA: lo si registra PRIMA di concatenare limiti e
        // campi path, che renderebbero mathText non vuoto anche senza
        // equazione. Serve al ramo !used piu' sotto, che senza questo
        // riscriveva le costanti a 1 su una superficie ancora a schermo.
        // Solo in Ray Marching: qui l'equazione e' UNA, e vuota vuol dire
        // "non c'e' niente da cui dedurre". In parametrico le equazioni sono
        // molte e svuotarne una non ha lo stesso significato.
        equationFieldIsEmpty = mathText.trimmed().isEmpty();
        glslText += " " + stripCodeComments(m_rm.texture) +
                    " " + stripCodeComments(m_rm.displacement);
        // L'APPLICATO, come nel ramo parametrico e con la stessa condizione:
        // equazione, texture e rilievo compilati nel marcher.
        if (m_constantsEditPending && ui->glWidget) {
            const bool scriptSurface = ui->glWidget->getEngine()
                                       && ui->glWidget->getEngine()->isScriptModeActive();
            if (!scriptSurface)
                mathText += " " + stripCodeComments(ui->glWidget->activeImplicitEquation());
            glslText += " " + stripCodeComments(ui->glWidget->currentTextureCode()) +
                        " " + stripCodeComments(ui->glWidget->currentDisplacementCode());
        }
    }

    // 2. AGGIUNGI CAMPI SEMPRE ATTIVI (Shared)
    // I COMMENTI non contano come "uso" di una costante: un commento italiano
    // basta ad accendere uno slider a vuoto (es. "Se c'è il segmento" in una
    // texture -> l'elisione c' viene presa per la costante C nel match
    // case-insensitive). Strip PER BLOCCO e non sull'insieme: i blocchi sono
    // concatenati con spazi, e un commento di linea non terminato a fine
    // blocco inghiottirebbe l'inizio del blocco successivo (falso negativo:
    // costante vera creduta inutilizzata -> reset a 1).
    glslText += " " + stripCodeComments(m_bgTextureCode); // Lo sfondo è comune
    // ...con il suo script non ancora eseguito (l'applicato e' la riga sopra).
    glslText += " " + stripCodeComments(m_bgTextureScriptText);

    // L'EDITOR E' SEMPRE GLSL. Vale per tutti e tre i modi dello script
    // (superficie, texture, sound) e per entrambe le tab: lo script di
    // superficie in parametrica e' GLSL con "return vec4(...)", quello in ray
    // marching e' GLSL implicito, e gli script metrici restituiscono mat3.
    // Non esiste uno script d'editor in sintassi exprtk.
    //
    // Perche' la distinzione conta: mathText matcha CASE-INSENSITIVE e non
    // riconosce le dichiarazioni locali. Mandandoci il GLSL, una 'a' minuscola
    // dello shader ("float a = 3.0;" nei tubi) accendeva lo slider A a vuoto:
    // sbloccato ma inerte, perche' nessuna costante e' davvero usata. Riguardava
    // 12 preset (Trefoil Tube, Clover Tube, i Clifford Labyrinth, i record
    // Clover/Trefoil/Tube). E' la stessa famiglia di falsi positivi descritta
    // sopra per gli shader; la protezione non arrivava fin qui.
    //
    // In glslText il match e' case-sensitive (le costanti sono iniettate come
    // "float A..F") ed esclude le lettere dichiarate come variabili locali:
    // coerente con generateGlslHelperVars(), che davanti a "float A" non
    // inietta affatto la costante, quindi li' A e' la locale e lo slider non ha
    // modo di influenzarla.
    glslText += " " + stripCodeComments(scriptText(shownScriptSlot()));

    // LO SCRIPT DELLA SUPERFICIE, scritto e applicato, qualunque modulo il dock
    // stia mostrando (la riga sopra conta solo lo slot in vista).
    // L'editor mostra UN modulo per volta (superficie, texture o suono) e in
    // ambito Mesh la texture della fascia: appena mostra altro, lo script
    // parametrico spariva da questo testo e le costanti citate SOLO li'
    // finivano nel ramo !used, che non si limita a bloccarle -- SCRIVE 1 nel
    // campo. Su Clifford 6-Tubes, dove E (quanti tubi) e F (raggio) vivono solo
    // nello script, bastava passare alla tab Texture o selezionare una mesh per
    // ritrovarsi E=1, F=1: un tubo solo e raggio degenere, cioe' la superficie
    // "collassata" -- e un salvataggio in quello stato scriveva i valori
    // sbagliati nel record.
    glslText += " " + stripCodeComments(m_surfaceScriptText + "\n" + m_surfaceScriptApplied);

    // Anche i path camera 4D/3D valgono come "uso" di una costante: le loro
    // espressioni sono compilate su exprtk con A..F/s registrate
    // (m_pathSymbolTable in SurfaceEngine), quindi una costante citata solo da
    // un path deve restare sbloccata e NON essere resettata dal ramo !used.
    // Anche i limiti U/V/W valgono come "uso": sono valutati da parseLimitField
    // con A..F/S registrate, quindi "uMax = 2*A" deve tenere A sbloccata (e
    // soprattutto NON farla resettare a 1 dal ramo !used, che cambierebbe
    // l'estensione della superficie di sorpresa). Lo stesso il taglio x/y/z
    // del Ray Marching.
    mathText += " " + m_lim.uMin + " " + m_lim.uMax +
                " " + m_lim.vMin + " " + m_lim.vMax +
                " " + m_lim.wMin + " " + m_lim.wMax;
    mathText += " " + m_lim.xMin + " " + m_lim.xMax +
                " " + m_lim.yMin + " " + m_lim.yMax +
                " " + m_lim.zMin + " " + m_lim.zMax;

    mathText += " " + m_path.x + " " + m_path.y +
                " " + m_path.z + " " + m_path.p +
                " " + m_path.alpha + " " + m_path.beta +
                " " + m_path.gamma +
                " " + m_path.x3D + " " + m_path.y3D +
                " " + m_path.z3D + " " + m_path.roll3D;

    // 3. LOGICA DI BLOCCO/SBLOCCO E RESET
    bool resetToNeutral = false;
    auto updateControl = [&](ConstField field) {
        const QString letter = constantName(field);
        QSlider *slider = constantSlider(field);
        QLineEdit *line = constantFieldEdit(field);

        bool used = false;

        // FIX FONDAMENTALE: In Ray Marching (tab 1), "S" funge da Step Relax!
        // Deve rimanere sempre attivo e NON deve mai essere resettato a 0,
        // altrimenti i raggi si congelano causando glitch grafici e cerchi concentrici.
        // Vale per ENTRAMBI i marcher: anche in Precise lo Step Relax governa
        // l'avvicinamento (vedi il commento su Ray Steps in updateRenderState).
        if (currentTab == 1 && letter == "S") {
            used = true;
        } else {
            QRegularExpression reMath("\\b" + letter + "\\b", QRegularExpression::CaseInsensitiveOption);
            used = mathText.contains(reMath);

            if (!used) {
                // GLSL: maiuscola e non dichiarata come locale (vedi
                // glslUsesConstant, unica sede della regola).
                used = glslUsesConstant(glslText, letter);
            }
        }

        if (!used) {
            // CAMPO EQUAZIONE VUOTO: si DISABILITA ma non si resetta. "Nessuna
            // costante citata" qui non vuol dire "nessuna costante serve": vuol
            // dire che non c'e' ancora un'equazione da cui dedurlo, e il valore
            // corrente e' l'unica informazione buona che abbiamo.
            // Sintomo: caricata la default del Cross Section (T^3, che vive su
            // A=0.9 B=0.4 C=0.2) e cancellata l'equazione, il textChanged
            // arrivava qui e riscriveva A=B=C=1 -- valori per cui il T^3
            // DEGENERA (serve A>B>C). Nessun rebuildShader, perche' le costanti
            // sono uniform: la superficie a schermo cambiava forma da sola,
            // restando quella "deformata" anche dopo il popup di equazione
            // mancante. In 3D non si notava: la sfera di default non usa
            // costanti, quindi non c'era niente da rovinare.
            // Il reset resta per il caso vero -- equazione presente che NON cita
            // quella costante.
            if (!equationFieldIsEmpty) {
                // RESET: S a 0.0, le altre (A-F) a 1.0 (lo slider lo
                // riallinea refreshConstantSliders, in fondo).
                const QString neutral = letter == "S" ? QStringLiteral("0") : QStringLiteral("1");
                if (m_const.*field != neutral) {
                    setConstText(field, neutral);
                    resetToNeutral = true;
                }
            }

            slider->setEnabled(false);
            line->setEnabled(false);
        } else {
            slider->setEnabled(true);
            line->setEnabled(true);
            // CASCATA: il campo di una costante IN USO puo' essere
            // un'espressione delle precedenti ("A/10"): quelle lettere sono
            // usate tramite lei. Senza, con B = A/10 e le equazioni che citano
            // la sola B, A veniva dichiarata in disuso e riportata a 1 -- e B,
            // cioe' la superficie, cambiava con lei (test degli scenari).
            mathText += " " + m_const.*field;
        }
    };

    // Dall'ULTIMA alla prima: la cascata va solo in avanti (B puo' citare A, S
    // tutte le altre), quindi quando si giudica una lettera i campi di quelle
    // che possono citarla sono gia' stati aggiunti al testo.
    const auto &fields = constantFields();
    for (auto it = fields.rbegin(); it != fields.rend(); ++it) updateControl(*it);
    if (resetToNeutral) refreshConstantSliders();
}


// ==========================================================
// EQUATIONS & MATHEMATICS
// ==========================================================

bool MainWindow::applySpaceLimits(bool notify, bool reapply)
{
    if (!ui->glWidget) return true;

    // Campo vuoto = nessun taglio su quel lato: si usa il default largo, non un
    // errore. Un campo NON valutabile invece ferma tutto: applicare gli assi
    // buoni e saltare quello rotto darebbe un taglio a meta', senza dire perche'.
    // Come i limiti u/v/w, ammettono le costanti A..F/S (parseLimitField).
    auto readField = [this, notify](QLineEdit* edit, const QString& txt, float def,
                                    const QString& axis, const QString& side,
                                    bool* good) -> float {
        *good = true;
        if (txt.trimmed().isEmpty()) return def;
        bool ok = false;
        const float v = parseLimitField(txt, &ok);
        if (!ok) {
            *good = false;
            if (notify && !m_constantPopupActive) {
                m_constantPopupActive = true;
                InputValidator::showInvalidLimitError(this, axis + " " + side, txt);
                edit->setFocus();
                edit->selectAll();
                m_constantPopupActive = false;
            }
            return def;
        }
        return v;
    };

    struct Axis {
        QLineEdit* lo;
        QLineEdit* hi;
        void (GLWidget::*setter)(float, float);
        const char* name;
    };
    const Axis axes[] = {
        { ui->lineXMin, ui->lineXMax, &GLWidget::setRangeX, "X" },
        { ui->lineYMin, ui->lineYMax, &GLWidget::setRangeY, "Y" },
        { ui->lineZMin, ui->lineZMax, &GLWidget::setRangeZ, "Z" },
    };

    // DUE PASSATE: prima si legge e valida tutto, poi si applica. Cosi' un
    // errore sull'asse Z non lascia X e Y gia' tagliati con la scena in uno
    // stato intermedio che nessun campo descrive.
    // I TESTI: al Run quelli scritti; con reapply quelli dell'ultimo taglio
    // applicato, rivalutati perche' sono cambiate le costanti (vedi
    // refreshLimitsFromConstants). Lo scritto dopo il Run aspetta il Run.
    QString txt[6];
    for (int i = 0; i < 3; ++i) {
        txt[2 * i]     = reapply ? m_spaceLimitsApplied[2 * i]     : lineText(axes[i].lo);
        txt[2 * i + 1] = reapply ? m_spaceLimitsApplied[2 * i + 1] : lineText(axes[i].hi);
    }

    float lo[3], hi[3];
    for (int i = 0; i < 3; ++i) {
        bool okLo = true, okHi = true;
        lo[i] = readField(axes[i].lo, txt[2 * i], -1000.0f, axes[i].name, "min", &okLo);
        if (!okLo) return false;
        hi[i] = readField(axes[i].hi, txt[2 * i + 1], 1000.0f, axes[i].name, "max", &okHi);
        if (!okHi) return false;

        // Intervallo impossibile: si lascia l'asse com'e' (nessun taglio nuovo),
        // senza popup -- e' lo stesso trattamento che aveva prima.
        if (lo[i] >= hi[i]) { lo[i] = -1000.0f; hi[i] = 1000.0f; }
    }

    for (int i = 0; i < 3; ++i) {
        (ui->glWidget->*axes[i].setter)(lo[i], hi[i]);
    }
    if (!reapply) std::copy(txt, txt + 6, m_spaceLimitsApplied);
    return true;
}

bool MainWindow::updateULimits() {
    float lo = parseLimitField(m_lim.uMin);
    float hi = parseLimitField(m_lim.uMax);
    if (lo >= hi) return false;            // limiti impossibili: non applicare né ridisegnare
    uMin = lo; uMax = hi;
    if (ui->glWidget) ui->glWidget->setRangeU(uMin, uMax);
    return true;
}

bool MainWindow::updateVLimits() {
    float lo = parseLimitField(m_lim.vMin);
    float hi = parseLimitField(m_lim.vMax);
    if (lo >= hi) return false;            // limiti impossibili: non applicare né ridisegnare
    vMin = lo; vMax = hi;
    if (ui->glWidget) ui->glWidget->setRangeV(vMin, vMax);
    return true;
}

bool MainWindow::updateWLimits() {
    float lo = parseLimitField(m_lim.wMin);
    float hi = parseLimitField(m_lim.wMax);
    if (lo >= hi) return false;            // limiti impossibili: non applicare né ridisegnare
    wMin = lo; wMax = hi;
    if (ui->glWidget) ui->glWidget->setRangeW(wMin, wMax);
    return true;
}

// Le costanti A..F/S sono cambiate: i limiti u/v/w CONFERMATI che le citano
// ("uMax = A", "6.28/C") vanno rivalutati e registrati di nuovo, come le
// equazioni che le leggono dal vivo. Senza, il dominio restava il numero
// calcolato alla conferma del campo: lo slider cambiava la superficie ma non
// la sua estensione. Il flusso geodetico rilegge i limiti a ogni ridisegno e
// l'Invio su una costante passa dal Run di servizio, che li rilegge anche lui:
// restava scoperto lo slider.
// Si salta l'asse spento, quello con una digitazione in attesa (il dominio
// cambia alla conferma del campo, non mentre si scrive) e quello con un campo
// vuoto o illeggibile, che la conferma ha rifiutato: il parse del vuoto passa
// (0.0) e lo registrerebbe di straforo. min >= max lo rifiuta updateU/V/WLimits.
void MainWindow::refreshLimitsFromConstants()
{
    struct Axis { QLineEdit *lo, *hi; bool (MainWindow::*update)(); };
    const Axis axes[] = {
        { ui->uMinEdit, ui->uMaxEdit, &MainWindow::updateULimits },
        { ui->vMinEdit, ui->vMaxEdit, &MainWindow::updateVLimits },
        { ui->wMinEdit, ui->wMaxEdit, &MainWindow::updateWLimits },
    };
    for (const Axis &a : axes) {
        if (!a.lo->isEnabled() || !a.hi->isEnabled()) continue;
        if (a.lo->property("userEditPending").toBool()
            || a.hi->property("userEditPending").toBool()) continue;
        const QString loTxt = lineText(a.lo).trimmed();
        const QString hiTxt = lineText(a.hi).trimmed();
        if (loTxt.isEmpty() || hiTxt.isEmpty()) continue;
        bool okLo = false, okHi = false;
        parseLimitField(loTxt, &okLo);
        parseLimitField(hiTxt, &okHi);
        if (okLo && okHi) (this->*a.update)();
    }

    // Taglio x/y/z del Ray Marching: si applica al Run, quindi qui si
    // rivalutano i testi dell'ultimo taglio applicato, non quelli scritti dopo.
    applySpaceLimits(/*notify=*/false, /*reapply=*/true);
}

MainWindow::CascadeConstants MainWindow::resolveCascadeConstants(bool restoreTextOnNegative,
                                                                 CascadeIssues *issues)
{
    const auto &fields = constantFields();

    // v[i] resta 0 finche' la sua costante non e' stata valutata: ogni campo
    // vede solo le costanti che lo precedono.
    float v[7] = { 0, 0, 0, 0, 0, 0, 0 };
    for (int i = 0; i < 7; ++i) {
        QLineEdit *edit = constantFieldEdit(fields[i]);
        bool ok = true;
        float raw = parseUIConstant(m_const.*fields[i], v[0], v[1], v[2], v[3], v[4], v[5], 0, &ok);

        if (!ok) {
            // Testo che non si valuta: vale 0 e NON diventa l'ultimo valore valido.
            if (issues && !issues->invalidEdit) {
                issues->invalidEdit = edit;
                issues->invalidName = constantName(fields[i]);
            }
        } else if (i < 6) {
            // A..F non ammettono valori negativi: si torna all'ultimo valore
            // valido. S invece li accetta (in parametrica servono per invertire
            // il tempo).
            if (raw < 0.0f) {
                const float prev = m_lastValidConst.value(edit, 1.0f);
                if (restoreTextOnNegative)
                    setConstValue(fields[i], prev);   // a segnali bloccati: niente ricorsione
                if (issues && issues->negativeName.isEmpty())
                    issues->negativeName = constantName(fields[i]);
                raw = prev;
            } else {
                m_lastValidConst[edit] = raw;
            }
        }
        v[i] = raw;
    }
    return { v[0], v[1], v[2], v[3], v[4], v[5], v[6] };
}

void MainWindow::evaluateCascade()
{
    CascadeIssues issues;
    const CascadeConstants kc = resolveCascadeConstants(/*restoreTextOnNegative=*/true, &issues);

    if (issues.invalidEdit) {
        if (!m_constantPopupActive) {
            m_constantPopupActive = true;
            InputValidator::showInvalidConstantError(this, issues.invalidName,
                                                     issues.invalidEdit->text());

            // Ripristino focus senza ri-emettere editingFinished
            {
                QSignalBlocker blocker(issues.invalidEdit);
                issues.invalidEdit->setFocus();
                issues.invalidEdit->selectAll();
            }
            // Reset rimandato: il flag resta true per tutto il resto
            // del ciclo di eventi (compreso il setFocus in onStartClicked)
            QTimer::singleShot(0, this, [this]{ m_constantPopupActive = false; });
        }
        return;
    }

    syncConstantSliders(kc);

    if (ui->glWidget) {
        setEngineConstants(kc, /*onlyIfChanged=*/false);
        refreshLimitsFromConstants();   // i limiti che citano le costanti
        m_meshDebounce->start();
    }

    if (!issues.negativeName.isEmpty() && !m_constantPopupActive) {
        m_constantPopupActive = true;
        InputValidator::showNegativeConstantError(this, issues.negativeName);
        QTimer::singleShot(0, this, [this]{ m_constantPopupActive = false; });
    }
}

// Ritorna true se il limite e' stato ACCETTATO (false = rifiutato: numero non
// valido o min>=max). Il limite accettato viene REGISTRATO subito nell'engine;
// a schermo entra subito se la superficie e' in moto (la mesh viene rigenerata
// qui: nessun tick lo farebbe), al Run se e' ferma. Vedi il blocco finale.
bool MainWindow::commitLimitFieldOnEnter(const QString& fieldName)
{
    // Limite u/v/w confermato (Invio o uscita dal campo): si VALIDA e si
    // REGISTRA nell'engine, ma a superficie ferma non si ridisegna. I limiti
    // sono parte della definizione della superficie come X/Y/Z/P, composizioni,
    // vincoli e flusso geodetico: il modulo equazioni aspetta il Run, cosi' il
    // dominio nuovo entra in vigore INSIEME alle equazioni nuove e non si vede
    // mai la superficie vecchia tagliata dai limiti nuovi (un'immagine che non
    // corrisponde ne' a cio' che c'era ne' a cio' che si sta scrivendo).
    // A superficie IN MOTO il ridisegno non si aspetta: lo porta il tick.
    // Restano fuori da questa regola i soli parametri che non ridefiniscono la
    // superficie -- costanti A..F/S e Steps -- che si applicano all'Invio.
    // Deliberatamente NON si passa da commitFieldsOnEnter: quella strada rifa'
    // un Run intero e committerebbe anche le equazioni in corso di scrittura.
    // NB: chiamata dai filtri tastiera (che consumano il Return prima che il
    // QLineEdit possa emettere returnPressed) e da editingFinished, cosi' anche
    // il solo cambio di focus conferma il campo: l'Invio non e' obbligatorio.

    // Il campo appena editato deve essere valutabile: se contiene una formula
    // rotta ("2*", "2*Z") o una costante spenta, avvisiamo e NON registriamo,
    // lasciando in vista la superficie valida precedente.
    QLineEdit* edited = nullptr;
    QString axisLabel;
    // Etichette MINUSCOLE: sono i parametri del dominio (u, v, w). Le maiuscole
    // U/V/W nel progetto sono le variabili di COMPOSIZIONE, un'altra cosa:
    // nominarle qui indicava all'utente il campo sbagliato.
    if      (fieldName == "uMinEdit") { edited = ui->uMinEdit; axisLabel = "u min"; }
    else if (fieldName == "uMaxEdit") { edited = ui->uMaxEdit; axisLabel = "u max"; }
    else if (fieldName == "vMinEdit") { edited = ui->vMinEdit; axisLabel = "v min"; }
    else if (fieldName == "vMaxEdit") { edited = ui->vMaxEdit; axisLabel = "v max"; }
    else if (fieldName == "wMinEdit") { edited = ui->wMinEdit; axisLabel = "w min"; }
    else if (fieldName == "wMaxEdit") { edited = ui->wMaxEdit; axisLabel = "w max"; }
    if (!edited) return false;

    // Campo disabilitato (es. W in Composition, svuotato apposta): niente da fare.
    if (!edited->isEnabled()) return false;

    // CONFERMA UNICA. Il flag userEditPending dice che c'e' una digitazione
    // dell'utente in attesa: qui si CONTROLLA e si consuma, in questo ordine.
    // Chi arriva secondo lo trova gia' consumato ed esce senza rivalidare.
    //
    // Serve perche' un solo Invio produce DUE chiamate: il filtro tastiera fa
    // prima w->clearFocus(), che emette subito editingFinished -- il cui
    // handler chiama gia' questa funzione -- e solo al ritorno il filtro
    // chiama a sua volta commitLimitFieldOnEnter. Limitandosi ad azzerare il
    // flag senza leggerlo, la seconda chiamata rivalidava lo stesso testo
    // sbagliato: su desktop notify() e' un exec() bloccante senza guardia
    // s_boxActive, quindi si vedevano due popup in fila (chiuso il primo,
    // compariva il secondo). Stesso effetto sul setFocus() del ramo d'errore
    // qui sotto, che alla chiusura del box fa perdere di nuovo il focus.
    //
    // Il ramo Android di notify() non salva: li' s_boxActive scarta il secondo
    // box, ma la seconda validazione girava comunque.
    if (!edited->property("userEditPending").toBool()) return false;
    edited->setProperty("userEditPending", false);

    const QString currentText = lineText(edited);
    bool ok = false;
    parseLimitField(currentText, &ok);
    // Il campo vuoto PASSA il parse (parseUIConstant: vuoto -> ok, 0.0), quindi
    // qui non si ferma: lo intercetta il ramo min>=max piu' sotto. Lo segnaliamo
    // comunque con emptyMeansNoLimit=false, cosi' il messaggio parla di dominio
    // mancante e non di numero illeggibile.
    if (!ok || currentText.trimmed().isEmpty()) {
        if (!m_constantPopupActive) {
            m_constantPopupActive = true;
            InputValidator::showInvalidLimitError(this, axisLabel, currentText,
                                                  /*emptyMeansNoLimit=*/false);
            edited->setFocus();
            edited->selectAll();
            m_constantPopupActive = false;
        }
        return false;
    }

    // Coerenza dell'ASSE e REGISTRAZIONE del dominio. updateU/V/WLimits rifiuta
    // da sola i limiti impossibili (min >= max) restituendo false senza toccare
    // nulla; se accetta, scrive i membri uMin/uMax... e il dominio dell'engine
    // (setRangeU/V/W).
    //
    // La registrazione va fatta ORA, non al Run, e il motivo e' il caso animato:
    // a superficie in moto per 't' il tasto e' "Stop" -- non c'e' nessun Run da
    // premere -- e il tick rigenera la mesh dal dominio DELL'ENGINE. Lasciando
    // il dominio vecchio la superficie restava identica: si riducevano gli
    // intervalli e non cambiava niente. Registrare non e' "applicare a meta'
    // digitazione": il dominio e' un intervallo numerico, non un'espressione da
    // comporre con le altre, e non puo' trovarsi in uno stato incoerente come
    // una X aggiornata con Y e Z ancora vecchie.
    //
    // Cio' che resta legato al Run e' il RIDISEGNO a superficie ferma (vedi
    // sotto): li' ridisegnare mostrerebbe la superficie VECCHIA tagliata dai
    // limiti NUOVI, un'immagine che non corrisponde a niente.
    bool applied = true;
    if      (fieldName.startsWith("u")) applied = updateULimits();
    else if (fieldName.startsWith("v")) applied = updateVLimits();
    else                                applied = updateWLimits();

    if (!applied) {
        if (!m_constantPopupActive) {
            m_constantPopupActive = true;
            // Qui il numero e' LEGGIBILE: updateU/V/WLimits ha rifiutato perche'
            // min >= max. Il messaggio "non e' un numero valido" era fuorviante,
            // mostrava il valore appena scritto come se fosse illeggibile.
            const QChar axis = axisLabel.isEmpty() ? QChar('u') : axisLabel.at(0);
            InputValidator::showLimitOrderError(this, axis);
            edited->setFocus();
            edited->selectAll();
            m_constantPopupActive = false;
        }
        return false;
    }

    // SUPERFICIE IN MOTO (geometria animata da 't' o flusso geodetico in corsa):
    // qui il dominio va anche RIGENERATO subito, perche' nessun tick lo fara'.
    // L'animazione da 't' della parametrica vive nel VERTEX SHADER (glwidget
    // manda m_uboData.time alla GPU a ogni frame): la mesh CPU non viene
    // ricalcolata, e il dominio u/v e' invece campionato da computeMesh() sulla
    // CPU. setRangeU/V/W qui sopra alza meshNeedsUpdate, ma quel flag governa
    // solo il RE-UPLOAD alla GPU dei vertici che l'engine ha gia' in pancia:
    // senza updateSurfaceData() si ricarica la mesh VECCHIA e a schermo non
    // cambia niente -- e' esattamente il "riduco gli intervalli e la superficie
    // non cambia" che si vedeva.
    // Non c'e' il problema di coerenza che consiglia di aspettare il Run a
    // superficie ferma: la forma a schermo e' quella delle equazioni GIA'
    // applicate (le modifiche in corso di scrittura aspettano il Run come
    // sempre), e qui se ne cambia il solo dominio.
    const bool geodesicActive = isGeodesicRoutingActive();

    const bool geomMoving = ui->glWidget && ui->glWidget->isSurfaceAnimating();
    const bool geoFlowMoving = (m_geoAnimTimer && m_geoAnimTimer->isActive());
    if (geomMoving || geoFlowMoving) {
        if (ui->glWidget) {
            if (geodesicActive) {
                // Flusso geodetico: la mesh NON viene dalle equazioni X/Y/Z/P
                // (sola mappa di visualizzazione) ma dall'integrazione, che ha
                // il suo imbuto. updateSurfaceData() la valuterebbe come mesh
                // parametrica, collassandola in una LAMINA.
                // useAppliedEquations: si applica SOLO il dominio; equazioni e
                // condizioni iniziali modificate restano in attesa del Run.
                updateGeodesicMesh(/*useAppliedLimits=*/false,
                                   /*useAppliedEquations=*/true);
            } else {
                ui->glWidget->updateSurfaceData();
            }
            ui->glWidget->update();
        }
        updateMasterButtonState();
        // Il dominio nuovo e' a schermo: da qui e' lavoro da proteggere.
        noteSurfaceApplied({ QStringLiteral("limits/") });
        return true;
    }

    // SUPERFICIE FERMA: il limite e' REGISTRATO e vale al prossimo Run, ma non
    // si ridisegna. Il tasto Run e' gia' acceso (il textEdited del campo ha
    // azzerato m_parametricApplied al primo carattere) ed e' lui a dire che c'e'
    // qualcosa in attesa; qui si riallinea comunque lo stato dei tasti, perche'
    // il dominio appena completato puo' aver chiuso l'ultima casella mancante
    // del gate (hasCompleteParametricInput).
    updateMasterButtonState();
    return true;
}

// Limite u/v della MESH SELEZIONATA confermato (Invio o uscita dal campo).
// Gemello di commitLimitFieldOnEnter: stessa validazione, stessi popup, stesso
// meccanismo userEditPending contro il doppio commit di un solo Invio.
//
// LA DIFFERENZA CHE CONTA: qui si applica SEMPRE subito, mentre i limiti
// globali a superficie ferma aspettano il Run. Il motivo di quell'attesa e' la
// coerenza fra dominio ed equazioni -- ridisegnare mostrerebbe la superficie
// VECCHIA tagliata dai limiti NUOVI. Qui quel rischio non c'e': le equazioni (o
// lo script) sono gia' applicate e si sta cambiando il dominio di una sola
// parte, esattamente come si cambia il suo colore o la sua densita' wireframe.
// Non applicarlo subito, oltretutto, lo renderebbe inutilizzabile: il Run dello
// script RISCRIVE i domini per-mesh dalle sezioni //MESH_BEGIN, quindi un
// dominio "in attesa del Run" verrebbe cancellato proprio da cio' che lo
// dovrebbe applicare.
bool MainWindow::commitMeshLimitFieldOnEnter(const QString& fieldName)
{
    if (!ui->glWidget) return false;

    QLineEdit* edited = nullptr;
    if      (fieldName == "meshUMinEdit") edited = ui->meshUMinEdit;
    else if (fieldName == "meshUMaxEdit") edited = ui->meshUMaxEdit;
    else if (fieldName == "meshVMinEdit") edited = ui->meshVMinEdit;
    else if (fieldName == "meshVMaxEdit") edited = ui->meshVMaxEdit;
    if (!edited) return false;

    // Campo spento (ambito "All", mesh singola, Ray Marching): niente da fare.
    if (!edited->isEnabled()) return false;

    // CONFERMA UNICA, come nei limiti globali: un solo Invio produce DUE
    // chiamate (il filtro tastiera fa clearFocus(), che emette subito
    // editingFinished, e poi chiama a sua volta il commit). Il flag si
    // CONTROLLA e si consuma qui, in questo ordine: chi arriva secondo lo
    // trova gia' consumato ed esce senza rivalidare, o si vedrebbero due
    // popup in fila per un solo errore.
    if (!edited->property("userEditPending").toBool()) return false;
    edited->setProperty("userEditPending", false);

    // AMBITO: in "Mesh" si scrive nella parte selezionata, in "All" nel dominio
    // di All (che vale per tutte le mesh e sospende i tagli per-parte senza
    // cancellarli). Si parte sempre dal dominio IN VIGORE per quell'ambito,
    // cosi' i tre campi non editati restano quelli che sono.
    const bool editingAll = (ui->glWidget->activeMeshPart() < 0);
    float uLo = 0.0f, uHi = 0.0f, vLo = 0.0f, vHi = 0.0f;
    const bool haveDomain = editingAll
                          ? ui->glWidget->allMeshDomain(uLo, uHi, vLo, vHi)
                          : ui->glWidget->activeMeshDomain(uLo, uHi, vLo, vHi);
    if (!haveDomain) return false;

    // Il campo appena editato deve essere leggibile. Gli altri tre NON si
    // rileggono dai widget: si parte dal dominio in vigore nella parte, cosi'
    // un campo lasciato a meta' da un'altra digitazione non entra nel commit.
    const QString currentText = lineText(edited);
    bool ok = false;
    const float value = parseLimitField(currentText, &ok);
    if (!ok || currentText.trimmed().isEmpty()) {
        if (!m_constantPopupActive) {
            m_constantPopupActive = true;
            // Etichetta con il NUMERO della mesh: col pannello Multi Mesh
            // aperto su piu' fasce, "u min" da solo non direbbe quale.
            const QString axisLabel =
                QString("mesh %1 %2").arg(ui->spinMeshSel->value())
                                     .arg(fieldName.contains("UMin") ? "u min"
                                        : fieldName.contains("UMax") ? "u max"
                                        : fieldName.contains("VMin") ? "v min" : "v max");
            InputValidator::showInvalidLimitError(this, axisLabel, currentText,
                                                  /*emptyMeansNoLimit=*/false);
            edited->setFocus();
            edited->selectAll();
            m_constantPopupActive = false;
        }
        return false;
    }

    if      (fieldName == "meshUMinEdit") uLo = value;
    else if (fieldName == "meshUMaxEdit") uHi = value;
    else if (fieldName == "meshVMinEdit") vLo = value;
    else                                  vHi = value;

    // Entrambi i setter rifiutano da se' i limiti impossibili (min >= max)
    // senza toccare nulla, come updateU/V/WLimits per i globali.
    const bool applied = editingAll
                       ? ui->glWidget->setAllMeshDomain(uLo, uHi, vLo, vHi)
                       : ui->glWidget->setActiveMeshDomain(uLo, uHi, vLo, vHi);
    if (!applied) {
        if (!m_constantPopupActive) {
            m_constantPopupActive = true;
            InputValidator::showLimitOrderError(this, fieldName.contains('U') ? QChar('u')
                                                                              : QChar('v'));
            edited->setFocus();
            edited->selectAll();
            m_constantPopupActive = false;
        }
        // Il campo mostra un valore che NON e' stato applicato: si riallinea al
        // dominio vero della parte, o resterebbe a mentire fino al prossimo
        // cambio di selezione.
        syncMeshLimitFields();
        return false;
    }

    // Applicato. setActiveMeshDomain ha gia' rigenerato la griglia (e riemesso
    // meshPartsChanged, che ripassa da qui a riallineare i campi): non serve
    // altro. Il master non va rivalutato -- nessun clock e' cambiato.
    return true;
}

void MainWindow::discardPendingLimitEdits()
{
    for (QLineEdit *e : { ui->uMinEdit, ui->uMaxEdit, ui->vMinEdit, ui->vMaxEdit,
                          ui->wMinEdit, ui->wMaxEdit,
                          ui->meshUMinEdit, ui->meshUMaxEdit, ui->meshVMinEdit, ui->meshVMaxEdit })
        if (e) e->setProperty("userEditPending", false);
}

// Porta i quattro campi u/v del pannello Multi Mesh sul dominio della parte
// selezionata. A segnali bloccati: questi setText sono DISPLAY, e senza il
// blocco il textEdited li segnerebbe come digitazione in attesa (userEditPending),
// facendo validare all'uscita dal campo un testo che l'utente non ha scritto.
void MainWindow::syncMeshLimitFields()
{
    if (!ui->glWidget || !ui->meshUMinEdit) return;

    // LA FONTE DIPENDE DALL'AMBITO, come per ogni altro controllo per-mesh: i
    // campi mostrano cio' che stanno per modificare. In "Mesh" e' il dominio
    // della parte scelta; in "All" quello di All -- se c'e' un taglio, quello,
    // altrimenti il dominio che la figura sta disegnando adesso (la prima
    // parte), cosi' il campo non e' mai vuoto su una superficie visibile.
    float uLo = 0.0f, uHi = 0.0f, vLo = 0.0f, vHi = 0.0f;
    const bool hasPart = (ui->glWidget->activeMeshPart() >= 0)
                       ? ui->glWidget->activeMeshDomain(uLo, uHi, vLo, vHi)
                       : ui->glWidget->allMeshDomain(uLo, uHi, vLo, vHi);

    auto show = [](QLineEdit *e, bool on, float v) {
        if (!e) return;
        QSignalBlocker b(e);
        // 'g' con 12 cifre come i limiti globali (mainwindow.cpp ~1807): un
        // 6.28318531 dello script non deve tornare indietro arrotondato.
        e->setText(on ? QString::number(v, 'g', 12) : QString());
        // La digitazione eventualmente in sospeso su questo campo non vale
        // piu': il testo l'ha appena riscritto il programma.
        e->setProperty("userEditPending", false);
    };

    show(ui->meshUMinEdit, hasPart, uLo);
    show(ui->meshUMaxEdit, hasPart, uHi);
    show(ui->meshVMinEdit, hasPart, vLo);
    show(ui->meshVMaxEdit, hasPart, vHi);
}

// Equazioni parametriche sufficienti a definire una superficie: almeno TRE dei
// quattro campi X/Y/Z/P non vuoti. Stessa idea della soglia >=2 di
// hasPath4DInput, tarata sui dati: fra le superfici parametriche in libreria
// nessuna ha 1 o 2 campi pieni -- sono 4 campi (la gran parte) o 3 (P vuoto,
// che e' legittimo: le superfici puramente 3D non usano la quarta coordinata).
// Con meno di tre non c'e' una superficie da disegnare, e premere Run
// costruiva una forma degenere sui campi rimasti dalla superficie precedente.
// NB: le superfici da SCRIPT hanno tutti e quattro i campi vuoti e non passano
// di qui -- il tasto Run parametrico e' gia' spento per loro
// (surfaceFromScript in updateMasterButtonState).
bool MainWindow::hasParametricEquationInput() const
{
    int filled = 0;
    if (ui->lineX && !m_eq.x.trimmed().isEmpty()) filled++;
    if (ui->lineY && !m_eq.y.trimmed().isEmpty()) filled++;
    if (ui->lineZ && !m_eq.z.trimmed().isEmpty()) filled++;
    if (ui->lineP && !m_eq.p.trimmed().isEmpty()) filled++;
    return filled >= 3;
}

// Gate del TASTO RUN del dock Equations: tutti i campi che servono davvero
// alla superficie che si sta scrivendo sono compilati. E' piu' severo di
// hasParametricEquationInput (che guarda i soli X/Y/Z/P ed e' la validazione
// del master START, con il suo popup dedicato): qui si aggiungono il dominio e
// i campi del TAB ATTIVO del pannello, perche' con quelli incompleti il Run
// non ha niente di sensato da costruire e il tasto deve restare spento invece
// di produrre una mesh degenere. Il tasto e' cosi' anche un indicatore di
// completezza: si accende quando c'e' tutto.
//
// Nessun popup qui: e' un predicato di UI, chiamato di continuo da
// updateMasterButtonState a ogni carattere digitato.
bool MainWindow::hasCompleteParametricInput()
{
    // 1. Equazioni principali: la soglia storica (>=3 di X/Y/Z/P).
    if (!hasParametricEquationInput()) return false;

    // 2. DOMINIO: i limiti dei parametri effettivamente in uso devono essere
    //    compilati e coerenti. Si guarda isEnabled(), che checkParametricDependency
    //    ha gia' allineato all'uso reale di u/v/w (un asse spento -- es. w in
    //    Composition -- non fa testo). I limiti non si applicano piu' da soli
    //    all'Invio: se sono incompleti, il Run costruirebbe sul dominio vecchio.
    auto axisOk = [this](QLineEdit* lo, QLineEdit* hi) -> bool {
        if (!lo || !hi || !lo->isEnabled() || !hi->isEnabled()) return true;  // asse non in uso
        const QString loTxt = lineText(lo).trimmed();
        const QString hiTxt = lineText(hi).trimmed();
        if (loTxt.isEmpty() || hiTxt.isEmpty()) return false;
        bool okLo = false, okHi = false;
        const float a = parseLimitField(loTxt, &okLo);
        const float b = parseLimitField(hiTxt, &okHi);
        if (!okLo || !okHi) return false;
        return a < b;
    };
    if (!axisOk(ui->uMinEdit, ui->uMaxEdit)) return false;
    if (!axisOk(ui->vMinEdit, ui->vMaxEdit)) return false;
    if (!axisOk(ui->wMinEdit, ui->wMaxEdit)) return false;

    // 3. Campi del TAB ATTIVO del pannello. Si guarda il tab CORRENTE e solo se
    //    e' abilitato: checkParametricDependency spegne i tab che non c'entrano
    //    con le equazioni scritte, e un tab spento non ha campi da pretendere.
    if (ui->panelImplicit) {
        const int tab = ui->panelImplicit->currentIndex();
        if (ui->panelImplicit->isTabEnabled(tab)) {
            if (tab == 1) {
                // COMPOSITION: ogni variabile maiuscola citata dalle equazioni
                // dev'essere definita. Un campo abilitato e vuoto e' una U senza
                // definizione: la superficie non e' scrivibile.
                for (QPlainTextEdit* e : { ui->lineU, ui->lineV, ui->lineW }) {
                    if (e && e->isEnabled() && e->toPlainText().trimmed().isEmpty())
                        return false;
                }
            }
            else if (tab == 2 && ui->lnU) {
                // GEODESIC FLOW: serve una DIREZIONE iniziale non nulla, cioe'
                // almeno uno fra du/dv/dw compilato. Con tutte e tre a zero non
                // c'e' geodetica da integrare e il Run non produce nulla.
                //
                // NON si pretendono tutti e sei i campi compilati: un campo
                // vuoto vale ZERO, che e' una condizione iniziale legittima e
                // usatissima (22 dei 37 preset geodetici di fabbrica lasciano
                // vuoto w0, o du/dv/dw non usati). Pretenderli avrebbe spento
                // il Run su piu' di meta' della libreria.
                // Il punto di partenza u0/v0/w0 puo' quindi essere tutto vuoto:
                // e' l'origine, un punto come un altro. Il fattore conforme
                // (lineConform) ha un default di 1.0 e non si pretende.
                bool anyDir = false;
                for (QPlainTextEdit* e : { ui->lndU, ui->lndV, ui->lndW }) {
                    if (!e || !e->isEnabled()) continue;
                    const QString txt = e->toPlainText().trimmed();
                    // "0" esplicito conta come non-direzione, esattamente come
                    // il campo vuoto: entrambi danno componente nulla.
                    if (!txt.isEmpty() && txt != "0" && txt != "0.0") anyDir = true;
                }
                if (!anyDir) return false;
            }
            // tab == 0 (CONSTRAINTS): i vincoli sono FACOLTATIVI e mutuamente
            // esclusivi (scriverne uno svuota gli altri due: vedi il blocco
            // "Mutua esclusione dei vincoli" nel costruttore): pretenderli
            // compilati spegnerebbe il Run su ogni normale superficie u,v.
        }
    }

    return true;
}


 // --- Parsing, Strings & Scripts ---

float MainWindow::parseMath(const QString &text, bool *ok)
{
    QString clean = text.trimmed();
    if (clean.isEmpty()) {
        if (ok) *ok = false;
        return 0.0f;
    }

    // Uniformiamo la virgola decimale (locale italiano)
    clean.replace(',', '.');

    bool parseOk = false;
    float v = ExpressionParser::evaluateSimple(clean, parseOk);
    if (ok) *ok = parseOk;
    return parseOk ? v : 0.0f;
}

float MainWindow::parseLimitField(const QString &text, bool *ok)
{
    // I limiti ammettono le costanti A..F/S oltre a pi/e/tau. Non passiamo da
    // parseMath: la sua symbol table non conosce le costanti e un "2*A"
    // cadrebbe nel fallback toFloat() -> 0.0 silenzioso (il limite diventa 0
    // e, peggio, viene salvato 0 nel preset).
    //
    // restoreTextOnNegative=false: qui stiamo solo LEGGENDO un limite, non
    // editando le costanti; riscrivere i campi A..F da dentro la lettura di
    // uMax sarebbe un effetto collaterale a sorpresa.
    const CascadeConstants k = resolveCascadeConstants(false);
    return parseUIConstant(text, k.a, k.b, k.c, k.d, k.e, k.f, k.s, ok);
}

float MainWindow::parseUIConstant(const QString &exprStr, float A, float B, float C, float D, float E, float F, float S, bool* ok)
{
    QString cleanExpr = exprStr.trimmed();
    if (cleanExpr.isEmpty()) { if (ok) *ok = true; return 0.0f; }

    // 1. Uniformiamo la punteggiatura
    cleanExpr.replace(",", ".");

    // 2. PASSAGGIO A DOUBLE: ExprTk è nativo e infallibile in double
    typedef exprtk::symbol_table<double> symbol_table_t;
    typedef exprtk::expression<double>   expression_t;
    typedef exprtk::parser<double>       parser_t;

    symbol_table_t symbol_table;
    symbol_table.add_constants();

    // 3. AGGIUNTA MANUALE FORZATA: Nel caso add_constants() faccia i capricci
    symbol_table.add_constant("pi", 3.14159265358979323846);
    symbol_table.add_constant("PI", 3.14159265358979323846);
    symbol_table.add_constant("e",  2.71828182845904523536);
    symbol_table.add_constant("tau", 6.28318530717958647692);
    symbol_table.add_constant("TAU", 6.28318530717958647692);

    // 4. FIX FONDAMENTALE: Usiamo add_constant invece di add_variable!
    // Inserendo i numeri come costanti assolute evitiamo qualsiasi crash
    // di puntatori o reference in memoria da parte di ExprTk.
    symbol_table.add_constant("A", (double)A);
    symbol_table.add_constant("B", (double)B);
    symbol_table.add_constant("C", (double)C);
    symbol_table.add_constant("D", (double)D);
    symbol_table.add_constant("E", (double)E);
    symbol_table.add_constant("F", (double)F);
    symbol_table.add_constant("S", (double)S); symbol_table.add_constant("s", (double)S);

    expression_t expression;
    expression.register_symbol_table(symbol_table);

    parser_t parser;

    // 5. COMPILAZIONE
    if (parser.compile(cleanExpr.toStdString(), expression)) {
        if (ok) *ok = true;
        return static_cast<float>(expression.value());
    } else {
        if (ok) *ok = false;
        return 0.0f;
    }
}

QString MainWindow::composeEquation(const QString &eq, const QString &uDef, const QString &vDef, const QString &wDef) {
    if (eq.isEmpty()) return eq;

    QString res = eq;

    // Se un campo è vuoto, il suo valore di default è la rispettiva variabile minuscola.
    // Usiamo le parentesi per garantire l'ordine delle operazioni matematiche!
    QString subU = uDef.trimmed().isEmpty() ? "u" : "(" + uDef.trimmed() + ")";
    QString subV = vDef.trimmed().isEmpty() ? "v" : "(" + vDef.trimmed() + ")";
    QString subW = wDef.trimmed().isEmpty() ? "w" : "(" + wDef.trimmed() + ")";

    // \b indica un "word boundary", così sostituisce la "U" isolata,
    // ma ignora ad esempio la "U" dentro una parola fittizia
    res.replace(kReUpperU, subU);
    res.replace(kReUpperV, subV);
    res.replace(kReUpperW, subW);

    return res;
}

bool MainWindow::hasTimeVariable(const QString& code) const {
    // Commenti rimossi per evitare falsi positivi (es. "don't")
    QString cleaned = stripCodeComments(code);

    // Rimuoviamo il blocco audio //SOUND_BEGIN..//SOUND_END prima del match: la
    // firma mainSound(int samp, float time) e le sue 'time'/'t' LOCALI sono del
    // sintetizzatore audio, NON dell'uniforme tempo grafico. Senza questo, ogni
    // record con un suono (la maggioranza) risultava "animato" anche a geometria
    // statica -> clock accesi a vuoto e potenziali riavvii spuri. La 't'/'iTime'
    // del codice GRAFICO (es. "float t = iTime;") resta e segnala animazione vera.
    // Stesso regex (con blocchi annidati) di cleanCodeForComparison.
    QRegularExpression soundBlock(R"(//\s*SOUND_BEGIN.*?//\s*SOUND_END\n?)",
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    while (cleaned.contains(soundBlock)) cleaned.remove(soundBlock);

    return cleaned.contains(kReTimeVar);
}

bool MainWindow::applyDiscreteConstants()
{
    if (m_discreteConsts.isEmpty() && m_minConsts.isEmpty()) return false;

    bool changed = false;
    for (ConstField field : constantFields()) {
        const QString key = constantName(field);
        auto it    = m_discreteConsts.constFind(key);
        auto itMin = m_minConsts.constFind(key);
        const bool isDiscrete = (it != m_discreteConsts.constEnd());
        const bool hasMin     = (itMin != m_minConsts.constEnd());
        if (!isDiscrete && !hasMin) continue;

        bool ok = false;
        const float cur = (m_const.*field).trimmed().toFloat(&ok);
        if (!ok) continue;   // espressione (es. "A*2"): non la tocchiamo

        float target = cur;
        if (isDiscrete) {
            // Intero PIU' VICINO, poi dentro il range dichiarato.
            target = float(qBound(it->lo, qRound(cur), it->hi));
        }
        if (hasMin && target < *itMin) {
            target = *itMin;   // costante continua, solo la soglia inferiore
        }

        if (qFuzzyCompare(cur, target)) continue;

        // Solo il campo: slider e cascata li riallinea il chiamante, una volta
        // sola, altrimenti ogni riga qui ne scatenerebbe una (e con essa un
        // ricalcolo di mesh per ciascuna costante). 'g' evita lo zero decimale
        // sugli interi (4, non 4.00) e tiene le frazioni dei minimi continui (0.3).
        setConstValue(field, target);
        changed = true;
    }
    return changed;
}

void MainWindow::syncConstantSliders(const CascadeConstants &k)
{
    auto setSmartSlider = [this](QSlider* s, float v, bool isS) {
        bool old = s->blockSignals(true);
        int intVal = static_cast<int>(v * 100.0f);

        int newMin;
        int newMax;

        // --- NUOVA LOGICA: Se siamo sul Tab 1 (Ray Marching) e stiamo aggiornando lo slider S ---
        if (isS && implicitMode()) {
            newMin = 0; // In Ray Marching lo slider parte rigorosamente da 0
            // Il massimo è 100 (1.0), ma se l'utente digita un numero enorme, si espande!
            newMax = std::max(100, intVal);

            // Forza anche il valore in modo che non scenda mai sotto lo 0
            if (intVal < 0) { intVal = 40; /* 0.4 di default */ }
        }
        // --- VECCHIA LOGICA: Parametrica o altri slider ---
        else {
            // Garantisce un limite standard di 10 (1000) o espande se il valore digitato è maggiore
            newMin = isS ? std::min(-1000, intVal) : 0;
            newMax = std::max(1000, intVal);
        }

        // Protezione "anti-collasso" se usi il mouse
        if (s->hasFocus() || s->isSliderDown() || s->underMouse()) {
            newMin = std::min(newMin, s->minimum());
            newMax = std::max(newMax, s->maximum());
        }

        s->setRange(newMin, newMax);
        s->setValue(intVal);
        s->blockSignals(old);
    };

    setSmartSlider(ui->aSlider, k.a, false);
    setSmartSlider(ui->bSlider, k.b, false);
    setSmartSlider(ui->cSlider, k.c, false);
    setSmartSlider(ui->dSlider, k.d, false);
    setSmartSlider(ui->eSlider, k.e, false);
    setSmartSlider(ui->fSlider, k.f, false);
    setSmartSlider(ui->sSlider, k.s, true); // true = questo è lo slider S!
}

void MainWindow::setEngineConstants(const CascadeConstants &kc, bool onlyIfChanged)
{
    if (!ui->glWidget) return;
    if (onlyIfChanged) {
        // setEquationConstants segna la mesh da rifare: chi passa di qui a ogni
        // uscita di Run non deve farla rigenerare a valori invariati.
        const QMap<QString, float> &now = ui->glWidget->getConstantsMap();
        const float want[7] = { kc.a, kc.b, kc.c, kc.d, kc.e, kc.f, kc.s };
        static const char *const names[7] = { "A", "B", "C", "D", "E", "F", "S" };
        bool same = now.size() == 7;
        for (int i = 0; same && i < 7; ++i)
            same = now.value(QLatin1String(names[i]), want[i] + 1.0f) == want[i];
        if (same) return;
    }
    ui->glWidget->setEquationConstants(kc.a, kc.b, kc.c, kc.d, kc.e, kc.f, kc.s);
}

MainWindow::CascadeConstants MainWindow::pushConstantsToEngine(bool restoreTextOnNegative,
                                                               bool always)
{
    const CascadeConstants kc = resolveCascadeConstants(restoreTextOnNegative);
    // Gli slider mostrano i valori RISOLTI: una costante definita come
    // espressione di un'altra cambia con lei anche quando a cambiarla non e'
    // un gesto dell'utente.
    syncConstantSliders(kc);
    setEngineConstants(kc, /*onlyIfChanged=*/!always);
    return kc;
}

void MainWindow::refreshConstants(bool restoreTextOnNegative)
{
    // Chi chiama da qui ha appena cambiato cio' che e' a schermo (load, reset,
    // Library, Run riuscito): campi e applicato coincidono di nuovo.
    m_constantsEditPending = false;
    updateConstantsUIState();
    pushConstantsToEngine(restoreTextOnNegative);
}

bool MainWindow::isGeodesicRoutingActive() const
{
    if (implicitMode()) return false;
    if (!hasGeodesicText()) return false;

    const QString mainEqs = m_eq.x + " " + m_eq.y + " "
                          + m_eq.z + " " + m_eq.p;
    const int upperCount = (mainEqs.contains(kReUpperU) ? 1 : 0)
                         + (mainEqs.contains(kReUpperV) ? 1 : 0)
                         + (mainEqs.contains(kReUpperW) ? 1 : 0);
    // Con uno script METRICO la mappa X/Y/Z/P puo' legittimamente non citare
    // U/V/W (resta la carta identita'): il solo upperCount non riconoscerebbe
    // il caso.
    const bool metricScriptActive = !m_metricScriptBody.trimmed().isEmpty();
    return upperCount > 0 || metricScriptActive;
}

void MainWindow::commitUiFieldsDuringMotion() {
    // Conta come commit da Invio: il ramo generico del filtro mobile chiama
    // QUESTA, non commitFieldsOnEnter, e va contata sullo stesso contatore --
    // altrimenti il confronto attorno al clearFocus() non la vedrebbe e il
    // doppione resterebbe (qui i popup sono quelli di validateAndParseLimits e
    // di tutto onStartClicked).
    ++m_commitOnEnterCount;

    // Questa funzione applica AL VOLO le equazioni della geometria: ha senso
    // solo se e' il MODULO EQUAZIONI a essere in moto. Guardare il master
    // button (su STOP anche per un path camera, una rotazione o l'audio, con la
    // geometria ferma) la faceva girare su una superficie STATICA, committando
    // equazioni che dovevano attendere il Run -- vedi il commento in
    // commitFieldsOnEnter. Il ramo generico del filtro mobile la chiama
    // direttamente, quindi la guardia deve stare anche qui.
    if (!isEquationModuleMoving()) return;
    m_geodesicErrorPending = false;

    QString mainEqs = m_eq.x + " " + m_eq.y + " " +
                      m_eq.z + " " + m_eq.p;
    int upperCount = (mainEqs.contains(kReUpperU) ? 1 : 0) +
                     (mainEqs.contains(kReUpperV) ? 1 : 0) +
                     (mainEqs.contains(kReUpperW) ? 1 : 0);
    bool geoHasText = hasGeodesicText();
    bool isGeodesicActive = (upperCount > 0) && geoHasText &&
            (!implicitMode());
    if (!isGeodesicActive) {
        onStartClicked();
        return;
    }

    QVector<InputValidator::LimitField> limitFields = {
        {ui->uMinEdit, true}, {ui->uMaxEdit, true},
        {ui->vMinEdit, true}, {ui->vMaxEdit, true},
    };
    QVector<float> dummy;
    auto parseFn = [this](const QString& s, bool* ok) { return this->parseLimitField(s, ok); };
    if (!InputValidator::validateAndParseLimits(this, limitFields, parseFn, dummy)) {
        return;  // popup mostrato dal validator, niente dry-run
    }

    // Snapshot dei valori attuali prima di provare i nuovi.
        const std::optional<EquationTexts> previous = m_eqApplied;
        bool wasTimerActive = isGeodesicMotionActive();

        // Tentativo: applichiamo i nuovi e validiamo.
        snapshotActiveEquations();
        if (updateGeodesicMesh()) {
            return;  // OK: i nuovi valori restano in m_eqApplied, il moto continua.
        }

        // Errore: ripristiniamo i valori validi precedenti.
        // Il tasto rimane su STOP; mostriamo un solo popup.
        m_eqApplied = previous;
        m_geodesicErrorPending = false;
        setProperty("geoErrorType", "none");

        if (!property("geoErrorShown").toBool()) {
            setProperty("geoErrorShown", true);
            InputValidator::showGeodesicSingularityError(this);
        }

        // Riavvia il timer con i dati ripristinati (se era in moto).
        if (wasTimerActive) {
            if (m_geoAnimTimer && !m_geoAnimTimer->isActive())
                m_geoAnimTimer->start();
        }
    }

// Ritorna true se la modifica e' stata APPLICATA (vedi commitLimitFieldOnEnter).
bool MainWindow::commitFieldsOnEnter() {
    // Contatore dei commit: i filtri tastiera lo confrontano prima e dopo il
    // loro w->clearFocus() per non chiamare due volte per un solo Invio (vedi
    // commitOnEnterCount in mainwindow.h e il ramo generico dei filtri).
    ++m_commitOnEnterCount;

    m_geodesicErrorPending = false;

    // MODULO EQUAZIONI in moto: usa la logica live già esistente, che applica al
    // volo senza fermare l'animazione.
    //
    // Si guarda il MODULO EQUAZIONI, non il master button. Il master va su STOP
    // per QUALUNQUE cosa si muova -- clock della texture o dello sfondo, path
    // camera, rotazioni, audio -- anche con la GEOMETRIA FERMA. In quel caso si
    // finiva nel ramo live, che chiama onStartClicked SENZA rmApplyOnly, quindi
    // senza il congelamento delle equazioni sullo snapshot.
    // Basta un path camera in corsa, o una texture animata, perche' il master
    // dica STOP: da li' si finiva nel ramo live e la t appena scritta veniva
    // committata al primo Invio, senza Run.
    // E' lo stesso difetto gia' corretto per il commit di servizio e per
    // l'avvio del flusso geodetico: qualcosa entra senza un Run, e lo stato dei
    // tasti descrive una realta' diversa da quella a schermo. La radice comune
    // e' usare il master button come se dicesse "la geometria e' in moto".
    // A geometria ferma si prosegue sotto, dove il commit passa da rmApplyOnly
    // e le equazioni restano in attesa del Run.
    if (isEquationModuleMoving()) {
        commitUiFieldsDuringMotion();
        return true;
    }

    // A superficie ferma: applica solo nel tab parametrico.
    if (implicitMode()) return false;

    // Stesso routing di checkAndTriggerMeshUpdate: geodetico vs standard.
    QString mainEqs = m_eq.x + " " + m_eq.y + " " +
                      m_eq.z + " " + m_eq.p;
    int upperCount = (mainEqs.contains(kReUpperU) ? 1 : 0) +
                     (mainEqs.contains(kReUpperV) ? 1 : 0) +
                     (mainEqs.contains(kReUpperW) ? 1 : 0);

    if ((upperCount > 0) && hasGeodesicText()) {
        // useAppliedEquations: qui arriva l'Invio sulle COSTANTI A..F/S, che non
        // e' un Run. Si aggiorna il VALORE delle costanti sulla superficie gia'
        // applicata; tutte le equazioni -- la mappa X/Y/Z/P e i 7 campi del
        // flusso -- restano congelate sullo snapshot e aspettano il Run,
        // esattamente come nella parametrica standard.
        // (L'Invio sui 7 campi del flusso non passa piu' di qui a superficie
        // ferma: il filtro tastiera li tratta come campi differiti.)
        const bool meshOk = updateGeodesicMesh(/*useAppliedLimits=*/false,
                                               /*useAppliedEquations=*/true);
        if (!meshOk
                && property("geoErrorType").toString() == "singularity"
                && !property("geoErrorShown").toBool()) {
            setProperty("geoErrorShown", true);
            InputValidator::showGeodesicSingularityError(this);
        }

        // Il Run NON si spegne qui: questo percorso non ha applicato alcuna
        // equazione (le congela tutte), quindi se ce n'e' una in sospeso il Run
        // e' l'unico modo di applicarla. Lo rialza solo un Run vero.
        updateMasterButtonState();
        return meshOk;
    }

    // Standard / composition / constraint: applica equazioni, composizioni e
    // vincoli correnti e rigenera la mesh, senza far ripartire moto/rotazioni/audio.
    setProperty("rmApplyOnly", true);
    onStartClicked();                 // sender != m_btnStart -> niente toggle START/STOP
    setProperty("rmApplyOnly", false);
    return true;
}

bool MainWindow::updateGeodesicMesh(bool useAppliedLimits, bool useAppliedEquations)
{
    // Resettiamo il flag degli errori per questa esecuzione
    this->setProperty("geoErrorType", "none");

    if (m_geodesicErrorPending) return false;

    // GUARD RAII anti-congelamento: più sotto, con isInitialLoad, disabilitiamo
    // gli update del glWidget (setUpdatesEnabled(false)) e li riabilitiamo solo
    // nel ramo di successo. Ognuno dei numerosi `return false` di errore qui
    // sotto, se raggiunto DOPO quella disabilitazione, lascerebbe il widget
    // congelato sull'ultima mesh valida. Questo guard riabilita SEMPRE gli
    // update all'uscita per errore (e su qualunque return futuro), e va
    // disarmato esplicitamente solo sul percorso di successo.
    bool meshSucceeded = false;
    auto updatesGuard = qScopeGuard([this, &meshSucceeded]() {
        if (!meshSucceeded && ui->glWidget && !ui->glWidget->updatesEnabled())
            ui->glWidget->setUpdatesEnabled(true);
    });

    // --- 1. LETTURA E VALIDAZIONE DEI LIMITI (U/V sempre attivi nel geodesic) ---
    QVector<InputValidator::LimitField> limitFields = {
        {ui->uMinEdit, true}, {ui->uMaxEdit, true},
        {ui->vMinEdit, true}, {ui->vMaxEdit, true},
    };

    QVector<float> limitValues;
    bool allOk = true;
    for (const auto& field : limitFields) {
        if (!field.active) {
            limitValues.append(0.0f);
            continue;
        }
        bool ok = false;
        float v = parseLimitField(field.edit->text(), &ok);
        if (!ok) { allOk = false; break; }
        limitValues.append(v);
    }
    if (!allOk) {
        return false;
    }

    float uMin = limitValues[0], uMax = limitValues[1];
    float vMin = limitValues[2], vMax = limitValues[3];

    // MOTO IN CORSO: il dominio e' quello GIA' APPLICATO, non il testo dei
    // campi. Questa funzione gira a ogni tick del timer su un record animato,
    // e leggendo i campi raccoglieva le cifre a meta' digitazione: il limite
    // entrava in vigore al primo carattere, senza attendere l'Invio. I campi
    // restano la sorgente per i percorsi interattivi (Run, Invio sui limiti),
    // che passano da updateU/VLimits e quindi aggiornano proprio questi valori.
    if (useAppliedLimits && ui->glWidget && ui->glWidget->getEngine()) {
        const auto *eng = ui->glWidget->getEngine();
        uMin = eng->getUMin(); uMax = eng->getUMax();
        vMin = eng->getVMin(); vMax = eng->getVMax();
    }

    int steps = m_steps;
    int safeSteps = std::min(steps, 500);  // Limite fisico per le geodetiche

    // Controllo Min >= Max (U e V sempre attivi nel geodesic)
    if (!InputValidator::validateLimits(this, uMin, uMax, true, vMin, vMax, true, 0.0f, 0.0f, false)) {
        stopGeodesicAnimation();
        return false;
    }

    // 1. GESTIONE TEMPO (Animazione)
    double t = this->property("geoTime").toDouble();

    if (this->property("isInitialLoad").toBool()) {
        if (ui->glWidget) ui->glWidget->setUpdatesEnabled(false);
        if (m_statusLabel) {
            m_statusLabel->setStyleSheet("color: #00bfff; font-weight: bold;");
        }
        this->setProperty("isInitialLoad", false);
    }

    // --- LOGICA DI DISACCOPPIAMENTO ---
    // Due gruppi di campi, stessa regola: a schermo ci va solo cio' che e' gia'
    // stato confermato con un Run.
    //
    // A MOTO ATTIVO (isRunning) si congela TUTTO: il tick gira di continuo e
    // leggere i campi raccoglierebbe le cifre a meta' digitazione.
    //
    // A MOTO FERMO con useAppliedEquations (percorsi che NON sono un Run:
    // costanti A..F/S da slider o Invio, slider Steps, limiti u/v/w, ripristino
    // path, navigazione) si congela di nuovo TUTTO:
    //  - X/Y/Z/P sono la MAPPA DI VISUALIZZAZIONE e appartengono al dock
    //    Equations: si applicano SOLO col Run. Vanno presi dallo snapshot,
    //    altrimenti il commit di un limite committa di straforo equazioni
    //    modificate e non confermate -- il bug per cui il tasto Run restava
    //    acceso senza piu' niente da applicare.
    //  - I 7 campi del flusso (u/v/w iniziali, du/dv/dw, fattore conforme)
    //    DEFINISCONO la superficie geodetica, ma anche loro aspettano il Run,
    //    esattamente come le equazioni della parametrica standard. Prima erano
    //    letti DAL VIVO qui, e da li' nasceva il disallineamento: scritta una
    //    costante nuova in un campo del flusso, bastava muovere lo slider di
    //    quella costante (o dare Invio sul suo campo) perche' la modifica
    //    entrasse senza Run -- mentre nella parametrica standard restava in
    //    attesa. L'Invio sui 7 campi non passa piu' di qui a moto fermo: il
    //    filtro tastiera li tratta come campi differiti (isDeferredEquationField)
    //    e a superficie ferma non esegue nulla.
    const bool motionRunning = isGeodesicMotionActive();
    const bool freezeMapEqs  = motionRunning || useAppliedEquations;   // X/Y/Z/P
    const bool freezeFlowEqs = motionRunning || useAppliedEquations;   // i 7 campi

    // Snapshot mancante (mai fatto un Run: preset appena caricato da script
    // metrico, avvio a freddo). Ripiegare sui campi qui rimetterebbe in gioco
    // proprio le equazioni non confermate: meglio non ridisegnare affatto. Il
    // chiamante interattivo ha gia' REGISTRATO la sua modifica (limiti scritti
    // nell'engine, campi del flusso nella UI) e al prossimo Run vale tutto.
    SE_GEO_PROBE("updateGeodesicMesh useAppliedEqs=%d motion=%d snapshot=%d "
                 "freezeMap=%d freezeFlow=%d",
                 int(useAppliedEquations), int(motionRunning),
                 int(m_eqApplied.has_value()),
                 int(freezeMapEqs), int(freezeFlowEqs));

    if (useAppliedEquations && !m_eqApplied.has_value()) {
        SE_GEO_PROBE("ABORT: snapshot assente, non ridisegno "
                     "(il campo resta registrato, vale al prossimo Run)");
        return false;
    }

    auto pick = [this](bool frozen, EqField f) -> QString {
        return (frozen && m_eqApplied) ? (*m_eqApplied).*f : m_eq.*f;
    };

    QString rawX = pick(freezeMapEqs, &EquationTexts::x);
    QString rawY = pick(freezeMapEqs, &EquationTexts::y);
    QString rawZ = pick(freezeMapEqs, &EquationTexts::z);
    QString rawP = pick(freezeMapEqs, &EquationTexts::p);

    QString rawU = pick(freezeFlowEqs, &EquationTexts::geoU);
    QString rawV = pick(freezeFlowEqs, &EquationTexts::geoV);
    QString rawW = pick(freezeFlowEqs, &EquationTexts::geoW);

    QString rawDU = pick(freezeFlowEqs, &EquationTexts::geoDU);
    QString rawDV = pick(freezeFlowEqs, &EquationTexts::geoDV);
    QString rawDW = pick(freezeFlowEqs, &EquationTexts::geoDW);

    QString rawConf = pick(freezeFlowEqs, &EquationTexts::conform);
    // --- FINE LOGICA DI DISACCOPPIAMENTO ---

    QRegularExpression varRegex("\\b(u|v|w|U|V|W|x|y|z|t|iTime|u_time)\\b");

    // Costanti dai CAMPI (cascata), le stesse che vanno al motore piu' sotto.
    // Leggendo gli slider (passo 0.01) un fattore conforme "200*A" con
    // A = 0.005 risultava nullo, e il Run dava errore.
    const CascadeConstants kc = resolveCascadeConstants(true);

    if (!rawConf.contains(varRegex)) {
        float lambdaVal = parseUIConstant(rawConf, kc.a, kc.b, kc.c, kc.d, kc.e, kc.f, kc.s);

        if (lambdaVal <= 1e-8f) {
            m_geodesicErrorPending = true;
            if (m_geoAnimTimer && m_geoAnimTimer->isActive()) m_geoAnimTimer->stop();
            if (!property("geoErrorShown").toBool()) {
                setProperty("geoErrorShown", true);
                InputValidator::showInvalidConformalConstantError(this);
            }
            return false;
        }
    }

    SurfaceEngine* engine = ui->glWidget->getEngine();
    QRhi* rhi = ui->glWidget->getRhi(); // <-- Recuperiamo l'interfaccia GPU

    if (!rhi) {
        return false;
    }

    // ==============================================================
    // 2.5 ESTRAZIONE DELLE COSTANTI
    // ==============================================================
    {
        float cA = kc.a, cB = kc.b, cC = kc.c, cD = kc.d, cE = kc.e, cF = kc.f, cS = kc.s;
        setEngineConstants(kc, /*onlyIfChanged=*/false);

        if (!m_inGeoAnimTick &&
                !geodesicFieldsAreFinite({rawU, rawV, rawW, rawDU, rawDV, rawDW,
                                         rawConf, rawX, rawY, rawZ, rawP},
                                         uMin, uMax, vMin, vMax,
                                         cA, cB, cC, cD, cE, cF, cS)) {
            m_geodesicErrorPending = true;
            if (m_geoAnimTimer && m_geoAnimTimer->isActive()) m_geoAnimTimer->stop();
            this->setProperty("geoErrorType", "nonfinite");
            if (!property("geoErrorShown").toBool()) {
                setProperty("geoErrorShown", true);
                InputValidator::showGeodesicSingularityError(this);
            }
            return false;
        }
    }

    // Avviso "costante ambigua" (una sola volta per configurazione): la stessa
    // A..F nella metrica e nelle condizioni iniziali rende lo slider ambiguo.
    // Qui intercetta anche le modifiche fatte dal dock, che non passano da
    // runMetricScript. Non durante i tick di animazione, per non interromperla.
    if (!m_inGeoAnimTick)
        checkMetricConstantAmbiguity();

    // Recupera la mappa delle costanti (già calcolate a cascata da onStartClicked)
    QMap<QString, float> constantsMap = ui->glWidget->getConstantsMap();

    QString shaderError; // <--- Prepariamo la stringa per l'errore

    // 3. CALCOLO SINCRONO SU GPU
    QVector<QVector<QVector4D>> grid = engine->computeGeodesicFlow(
                rhi,
                rawX, rawY, rawZ, rawP,
                rawU, rawV, rawW,
                rawDU, rawDV, rawDW,
                rawConf,
                m_metricScriptBody,
                uMin, uMax, safeSteps,
                vMin, vMax, safeSteps,
                constantsMap,
                static_cast<float>(t),       // <--- NUOVO: tempo come uniform
                &shaderError
                );

    // --- GESTIONE DEGLI ERRORI ---
    if (grid.isEmpty()) {
        if (!shaderError.isEmpty()) {
            m_geodesicErrorPending = true;
            if (m_geoAnimTimer && m_geoAnimTimer->isActive()) m_geoAnimTimer->stop();
            setProperty("geoErrorShown", true);
            showShaderError("Geodesic Shader Error", shaderError);
            this->setProperty("geoErrorType", "syntax");
        } else {
            m_geodesicErrorPending = true;
            if (m_geoAnimTimer && m_geoAnimTimer->isActive()) m_geoAnimTimer->stop();
            this->setProperty("geoErrorType", "singularity");
        }
        return false;
    }

    // 4. APPLICHIAMO IMMEDIATAMENTE IL RISULTATO (grid non vuota: verificato sopra)
    // uMin/uMax/vMin/vMax: il dominio appena integrato. Ancora le coordinate
    // texture al dominio invece che agli indici di griglia, cosi' restringere
    // l'intervallo TAGLIA la texture invece di comprimerla (vedi setCustomMesh).
    if (!ui->glWidget->setCustomMesh(grid, !m_metricScriptBody.trimmed().isEmpty(),
                                     uMin, uMax, vMin, vMax)) {
        m_geodesicErrorPending = true;
        if (m_geoAnimTimer && m_geoAnimTimer->isActive()) m_geoAnimTimer->stop();
        this->setProperty("geoErrorType", "singularity");
        return false;
    }

    // Percorso di successo: disarma il guard anti-congelamento e ripristina la
    // UI post-caricamento riabilitando gli update del glWidget.
    meshSucceeded = true;
    if (ui->glWidget && !ui->glWidget->updatesEnabled())
        ui->glWidget->setUpdatesEnabled(true);

    // 5. LOGICA DEL TIMER CPU PER ANIMAZIONI
    QString geoEqs = rawX + " " + rawY + " " + rawZ + " " + rawP + " " +
            rawU + " " + rawV + " " + rawW + " " +
            rawDU + " " + rawDV + " " + rawDW + " " + rawConf + " " +
            m_metricScriptBody;

    bool hasTime = hasTimeVariable(geoEqs);

    if (!m_geoAnimTimer) {
        m_geoAnimTimer = new QTimer(this);
        m_geoAnimTimer->setObjectName("geoAnimTimer");   // VideoRecorder lo cerca per nome
        m_geoAnimTimer->setInterval(16);

        connect(m_geoAnimTimer, &QTimer::timeout, this, [this]() {
            // Guardia 1: finestra nascosta (app in background su iOS/Android)
            if (!isVisible()) return;

            // Guardia 2: contesto GPU non ancora ripristinato dopo il ritorno
            if (!ui->glWidget || !ui->glWidget->getRhi()) return;

            // Guardia 3: l'utente sta editando una costante (A..F / S).
            {
                QWidget* fw = qApp->focusWidget();
                if (fw == ui->lineA || fw == ui->lineB || fw == ui->lineC ||
                        fw == ui->lineD || fw == ui->lineE || fw == ui->lineF ||
                        fw == ui->lineS)
                    return;
            }

            bool meshOk = advanceGeodesicFlowBy(m_geoAnimTimer->interval() / 1000.0);
            if (!meshOk) {
                if (this->property("geoErrorType").toString() == "singularity") {
                    InputValidator::showAnimatedGeodesicSingularityError(this);
                }
            }
        });
    }

    // Il timer del ricalcolo geodetico (animazione di t nella metrica/condizioni)
    // dipende dallo stato logico "in moto", non dal TESTO del bottone: al primo
    // Start il bottone diventa "STOP" solo dopo updateGeodesicMesh, quindi qui
    // leggerlo darebbe ancora "START" e il timer non partirebbe mai.
    // Il flusso appartiene al modulo Equations: se l'utente l'ha fermato col
    // suo Stop (performEquationsStop), un ricalcolo mesh qualsiasi (slider
    // costanti, navigazione) NON deve riavviarlo. Run del dock / master Start
    // riarmano il flag prima di arrivare qui.
    // Durante la registrazione il tempo lo detta il loop del recorder
    // (advanceGeodesicFlowBy per frame): il timer asincrono resta fermo,
    // altrimenti i suoi tick nei processEvents avanzerebbero geoTime due volte.
    // useAppliedEquations distingue i RUN veri (false: master Start, Run del dock,
    // avvio del flusso) dai ricalcoli DI SERVIZIO (true: debounce di costanti e
    // Steps, Invio su un singolo campo, commit di un limite). Solo un Run puo'
    // AVVIARE il flusso: un ricalcolo di servizio lo mantiene se gia' in corsa,
    // ma non lo fa partire.
    // Senza questa condizione bastava scrivere 't' in un campo del flusso e poi
    // muovere uno slider: il ricalcolo del debounce rileggeva allora i 7 campi
    // dal vivo, vedeva il 't' appena scritto e faceva partire il timer da solo.
    // I tasti passavano a "Stop" -- il master pure -- senza che l'utente avesse
    // premuto Run, e per l'utente l'animazione non era partita affatto: era
    // partito il timer, non l'attesa applicazione delle equazioni. Oggi anche i
    // 7 campi sono congelati sui ricalcoli di servizio, quindi un 't' non
    // confermato non arriva nemmeno fin qui: la guardia resta come rete.
    const bool runCanStartFlow = !useAppliedEquations;
    if (hasTime && !m_masterStopped && !m_userStoppedGeomClock && !m_isRecording) {
        // Il flusso puo' ESSERE AVVIATO solo da un Run vero. La condizione va
        // QUI, sull'avvio, e non sull'if esterno: messa la' faceva cadere nel
        // ramo else ogni ricalcolo di servizio, che FERMA un timer gia' in
        // corsa -- il moto si arrestava modificando un limite ad animazione
        // avviata. Un ricalcolo di servizio non avvia e non ferma: lascia il
        // flusso com'e'.
        if (!m_geoAnimTimer->isActive() && runCanStartFlow) {
            m_geoAnimTimer->start();
            // Il bottone master deve riflettere subito il timer appena avviato,
            // senza dipendere dal fatto che un chiamante a valle richiami
            // updateMasterButtonState: lo facciamo qui, fuori dai tick (durante
            // l'animazione il bottone è già coerente e non va ritoccato).
            if (!m_inGeoAnimTick) updateMasterButtonState();
        }
    } else {
        if (m_geoAnimTimer->isActive()) {
            m_geoAnimTimer->stop();
            if (!m_inGeoAnimTick) updateMasterButtonState();
        }
    }

    setProperty("geoErrorShown", false);
    m_geodesicErrorPending = false;
    return true;
}

bool MainWindow::advanceGeodesicFlowBy(double dtSeconds)
{
    // Stessa velocita' del tick live: 0.015 unita' di geoTime per tick
    // nominale del timer (16 ms). Il tick passa il proprio intervallo e
    // avanza esattamente di 0.015; il recorder passa il dt virtuale del
    // frame (1/fps) e il video riproduce la velocita' vista a schermo.
    const int intervalMs = (m_geoAnimTimer && m_geoAnimTimer->interval() > 0)
                               ? m_geoAnimTimer->interval() : 16;
    const double step = 0.015 * (dtSeconds * 1000.0 / intervalMs);
    setProperty("geoTime", property("geoTime").toDouble() + step);

    m_inGeoAnimTick = true;
    // useAppliedLimits: il tick del moto NON deve raccogliere il testo dei campi
    // a meta' digitazione (vedi updateGeodesicMesh). Il dominio cambia quando
    // l'utente conferma con l'Invio, non mentre scrive.
    bool meshOk = updateGeodesicMesh(/*useAppliedLimits=*/true);
    m_inGeoAnimTick = false;
    return meshOk;
}

void MainWindow::checkAndTriggerMeshUpdate(bool useAppliedEquations) {
    if (!ui->glWidget) return;

    // Ray marching (tab implicito): la superficie è calcolata per pixel dallo
    // shader, non è una mesh poligonale. Cambiare "steps" agisce su setRaySteps,
    // non sulla griglia: qui basta un update() delle uniform. NB: lo script
    // PARAMETRICO (tab 0, es. tubi come Otto) è invece una mesh poligonale la cui
    // densità dipende da numU/numV (setResolution -> computeMesh): DEVE rigenerare,
    // altrimenti lo slider Steps risulta inerte sugli script parametrici. Prima
    // l'early-return copriva ogni script (isScriptModeActive) e bloccava proprio
    // quel caso.
    if (implicitMode()
        && ui->glWidget->getEngine() && ui->glWidget->getEngine()->isScriptModeActive()) {
        ui->glWidget->update(); // Aggiorna solo la visualizzazione (Uniforms)
        return;                 // Uscita anticipata per proteggere la GPU
    }

    // 1. Recupero equazioni principali
    QString mainEqs = m_eq.x + " " + m_eq.y + " " + m_eq.z + " " + m_eq.p;

    // 2. Analisi variabili composte (U, V, W)
    int upperCount = (mainEqs.contains(kReUpperU) ? 1 : 0) +
                     (mainEqs.contains(kReUpperV) ? 1 : 0) +
                     (mainEqs.contains(kReUpperW) ? 1 : 0);

    // 3. Verifica presenza campi Geodetici
    bool geoHasText = hasGeodesicText();

    // 4. Routing: se Geodetico è attivo e siamo nel tab Parametrico (0), usa il
    // calcolatore Tensoriale. Lo script metrico forza il routing geodetico anche
    // se la mappa di visualizzazione X/Y/Z/P non cita U/V/W.
    const bool metricScriptActive = !m_metricScriptBody.trimmed().isEmpty();
    if ((upperCount > 0 || metricScriptActive) && geoHasText && (!implicitMode())) {
        if (m_geodesicErrorPending) return;

        bool success = updateGeodesicMesh(/*useAppliedLimits=*/false,
                                          useAppliedEquations);
        if (!success) {
            // Allinea il percorso Script al percorso dock (onStartClicked): se il
            // flusso è degenerato in una singolarità, l'avviso va mostrato anche
            // qui, altrimenti il Run dallo Script fallisce in silenzio. La guard
            // geoErrorShown evita doppioni se più chiamanti si concatenano; i
            // rami "nonfinite"/"syntax" mostrano già da soli il loro popup.
            if (this->property("geoErrorType").toString() == "singularity"
                    && !property("geoErrorShown").toBool()) {
                setProperty("geoErrorShown", true);
                InputValidator::showGeodesicSingularityError(this);
            }
            return;
        }
    } else {
        // Altrimenti, rigenera la griglia poligonale standard
        ui->glWidget->updateSurfaceData();
        ui->glWidget->update();
    }
}

void MainWindow::stopGeodesicAnimation()
{
    if (m_geoAnimTimer && m_geoAnimTimer->isActive()) m_geoAnimTimer->stop();

    if (m_btnStart) m_btnStart->setText("START");

    if (ui->glWidget) {
        ui->glWidget->setSurfaceAnimating(false);
        if (!ui->glWidget->updatesEnabled())
            ui->glWidget->setUpdatesEnabled(true);
    }
}

bool MainWindow::isGeodesicMotionActive() const {
    // Un tick in corso conta come moto attivo anche a timer FERMO: durante la
    // registrazione il tempo lo detta il loop (advanceGeodesicFlowBy alza
    // m_inGeoAnimTick) e il timer resta spento per contratto. Senza questo,
    // updateGeodesicMesh nel REC saltava il disaccoppiamento scritto/applicato e
    // leggeva i campi UI (vuoti/diversi negli script metrici): la superficie
    // si appiattiva in una lamina solo in registrazione.
    return m_inGeoAnimTick || (m_geoAnimTimer && m_geoAnimTimer->isActive());
}

bool MainWindow::hasGeodesicText() const {
    if (!ui->lnU) return false;
    return !m_eq.geoU.trimmed().isEmpty()  ||
           !m_eq.geoV.trimmed().isEmpty()  ||
           !m_eq.geoW.trimmed().isEmpty()  ||
           !m_eq.geoDU.trimmed().isEmpty() ||
           !m_eq.geoDV.trimmed().isEmpty() ||
           !m_eq.geoDW.trimmed().isEmpty();
}

bool MainWindow::geodesicFieldsAreFinite(const QStringList& exprs,
                                         float uMin, float uMax,
                                         float vMin, float vMax,
                                         float A, float B, float C,
                                         float D, float E, float F, float S)
{
    typedef exprtk::symbol_table<double> symbol_table_t;
    typedef exprtk::expression<double>   expression_t;
    typedef exprtk::parser<double>       parser_t;

    // Variabili legate per riferimento. u,v,t li facciamo variare;
    // gli altri restano a un valore interno "sicuro" così le espressioni che li
    // citano compilano e non incappano nella loro singolarità naturale di bordo
    // (es. il fattore conforme di Poincaré che esplode solo a U^2+V^2+W^2 = 1).
    double u = 0, v = 0, t = 0;
    double w = 0.137, U = 0.137, V = 0.137, W = 0.137;
    double x = 0.137, y = 0.137, z = 0.137, iTime = 0, u_time = 0;

    symbol_table_t st;
    st.add_constants();
    st.add_constant("pi", 3.14159265358979323846);
    st.add_constant("PI", 3.14159265358979323846);
    st.add_constant("e",  2.71828182845904523536);
    st.add_constant("tau", 6.28318530717958647692);
    st.add_constant("TAU", 6.28318530717958647692);
    st.add_constant("A", (double)A); st.add_constant("B", (double)B);
    st.add_constant("C", (double)C); st.add_constant("D", (double)D);
    st.add_constant("E", (double)E); st.add_constant("F", (double)F);
    st.add_constant("S", (double)S); st.add_constant("s", (double)S);
    st.add_variable("u", u); st.add_variable("v", v); st.add_variable("w", w);
    st.add_variable("t", t);
    st.add_variable("U", U); st.add_variable("V", V); st.add_variable("W", W);
    st.add_variable("x", x); st.add_variable("y", y); st.add_variable("z", z);
    st.add_variable("iTime", iTime); st.add_variable("u_time", u_time);

    // Punti campione interni: evitiamo gli estremi esatti per non scambiare una
    // singolarità legittima di bordo (es. il fattore conforme di Poincaré, che
    // esplode solo a U^2+V^2+W^2 = 1) per un errore.
    const double fu = uMax - uMin, fv = vMax - vMin;
    const double us[3] = { uMin + 0.25*fu, uMin + 0.5*fu, uMin + 0.75*fu };
    const double vs[3] = { vMin + 0.25*fv, vMin + 0.5*fv, vMin + 0.75*fv };
    const double ts[3] = { 0.0, 0.5, 1.0 };

    for (const QString& raw : exprs) {
        QString clean = raw.trimmed();
        if (clean.isEmpty()) continue;
        clean.replace(",", ".");

        expression_t expr;
        expr.register_symbol_table(st);
        parser_t parser;
        if (!parser.compile(clean.toStdString(), expr))
            continue;   // errore di sintassi: lo gestisce la validazione esistente

        for (double su : us)
            for (double sv : vs)
                for (double stime : ts) {
                    u = su; v = sv; t = stime; iTime = stime; u_time = stime;
                    if (!isMeshSafeValue(expr.value()))
                        return false;
                }
    }
    return true;
}
