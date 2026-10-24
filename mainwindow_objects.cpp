// mainwindow_objects.cpp - Gli OGGETTI DELLA SCENA lato MainWindow (2.0).
// Ogni oggetto del GLWidget ha qui la sua sessione: la parte dei dock che e'
// sua (ObjectState) e lo stato di lavoro che segue il selezionato. La sessione
// del selezionato e' aperta nei membri di MainWindow (m_scene & co.); cambiare
// oggetto la chiude, apre quella dell'altro e ridisegna i dock. Il motore non
// si tocca: ogni oggetto ha gia' il suo stato nel GLWidget.

#include "mainwindow_p.h"
#include "scenehintoverlay.h"

#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QFontMetrics>

void MainWindow::saveObjectSession(ObjectSession &s) const
{
    s.state = m_scene;   // la sola parte dell'oggetto (ObjectState)
    s.eqApplied = m_eqApplied;
    for (int i = 0; i < 6; ++i) s.spaceLimitsApplied[i] = m_spaceLimitsApplied[i];
    s.meshVisibleExpr = m_meshVisibleExpr;
    s.lastParametricSteps = m_lastParametricSteps;
    s.lastImplicitSteps = m_lastImplicitSteps;
    s.lastImplicitS = m_lastImplicitS;
    s.shellThicknessBeforeSolid = m_shellThicknessBeforeSolid;
    s.textureModified = m_textureModified;
    s.implicitAlphaDisabled = m_implicitAlphaDisabled;
    s.implicitWarnShown = m_implicitWarnShown;
    s.metricScriptBody = m_metricScriptBody;
    s.currentTexturePresetPath = m_currentTexturePresetPath;
    s.textureHintText = m_currentTextureHintText;
    s.textureHintSeconds = m_currentTextureHintSeconds;
    // Il messaggio del preset e' dell'oggetto: quello dell'ambiente e' il
    // messaggio della scena, quello di un oggetto il messaggio della sua Shape.
    s.hintText = m_currentHintText;
    s.hintSeconds = m_currentHintSeconds;
    s.userStoppedGeomClock = m_userStoppedGeomClock;
    s.userStoppedTexClock = m_userStoppedTexClock;
    s.userStoppedMeshTexClock = m_userStoppedMeshTexClock;
    s.userStoppedBorderTexClock = m_userStoppedBorderTexClock;
    s.meshTextureScriptText = m_meshTextureScriptText;
    s.meshTextureScriptBase = m_meshTextureScriptBase;
    s.meshTextureScriptPart = m_meshTextureScriptPart;
    s.borderTextureScriptText = m_borderTextureScriptText;
    s.borderTextureScriptBase = m_borderTextureScriptBase;
    s.surfaceOrigin = int(m_surfaceOrigin);
    s.parametricApplied = m_parametricApplied;
    s.implicitApplied = m_implicitApplied;
    s.rmTextureApplied = m_rmTextureApplied;
    s.cleanScene = m_cleanScene;
    s.appliedSurface = m_appliedSurface;
    s.cleanTexture = m_cleanTexture;
    s.pickedTexture = m_pickedTexture;
    s.cleanSound = m_cleanSound;
    s.pickedSound = m_pickedSound;
    s.viewTouched = m_viewTouched;
    // Il flusso geodetico: dove e' arrivato e se avanza. Da qui, finche' non lo
    // si riseleziona, lo porta avanti advanceGeodesicObjectsBy.
    s.geoTime = m_geoTime;
    s.geoAnimating = m_geoAnimTimer && m_geoAnimTimer->isActive();
    // I suoi moduli per il master della scena (sceneMasterState).
    const MasterActivity a = masterActivity();
    s.activity.eqAvailable = a.eqAvailable;
    s.activity.eqRunning = a.eqRunning;
    s.activity.texAvailable = a.texAvailable;
    s.activity.texRunning = a.texRunning;
    s.activity.texAnyRunning = a.texAnyRunning;
    s.activity.borderAvailable = a.borderAvailable;
    s.activity.borderRunning = a.borderRunning;
    s.activity.rotationAvailable = hasAnyRotationSpeed();
    s.activity.fromGeo = s.geoAnimating;
    s.textureViewTouched = m_textureViewTouched;
    // Si chiude modificato? Le modifiche di un oggetto non selezionato non
    // cambiano finche' non lo si riapre: basta ricordarlo qui.
    s.dirtyAtClose = false;
    for (const QString &k : objectUnsavedKeys())
        if (k.startsWith(QLatin1String("scena:"))) { s.dirtyAtClose = true; break; }
}

void MainWindow::openObjectSession(const ObjectSession &s)
{
    static_cast<ObjectState &>(m_scene) = s.state;   // la scena resta com'e'
    m_eqApplied = s.eqApplied;
    for (int i = 0; i < 6; ++i) m_spaceLimitsApplied[i] = s.spaceLimitsApplied[i];
    m_meshVisibleExpr = s.meshVisibleExpr;
    m_lastParametricSteps = s.lastParametricSteps;
    m_lastImplicitSteps = s.lastImplicitSteps;
    m_lastImplicitS = s.lastImplicitS;
    m_shellThicknessBeforeSolid = s.shellThicknessBeforeSolid;
    m_textureModified = s.textureModified;
    m_implicitAlphaDisabled = s.implicitAlphaDisabled;
    m_implicitWarnShown = s.implicitWarnShown;
    m_metricScriptBody = s.metricScriptBody;
    m_currentTexturePresetPath = s.currentTexturePresetPath;
    m_currentTextureHintText = s.textureHintText;
    m_currentTextureHintSeconds = s.textureHintSeconds;
    m_currentHintText = s.hintText;
    m_currentHintSeconds = s.hintSeconds;
    m_userStoppedGeomClock = s.userStoppedGeomClock;
    m_userStoppedTexClock = s.userStoppedTexClock;
    m_userStoppedMeshTexClock = s.userStoppedMeshTexClock;
    m_userStoppedBorderTexClock = s.userStoppedBorderTexClock;
    m_meshTextureScriptText = s.meshTextureScriptText;
    m_meshTextureScriptBase = s.meshTextureScriptBase;
    m_meshTextureScriptPart = s.meshTextureScriptPart;
    m_borderTextureScriptText = s.borderTextureScriptText;
    m_borderTextureScriptBase = s.borderTextureScriptBase;
    m_surfaceOrigin = static_cast<decltype(m_surfaceOrigin)>(s.surfaceOrigin);
    m_parametricApplied = s.parametricApplied;
    m_implicitApplied = s.implicitApplied;
    m_rmTextureApplied = s.rmTextureApplied;
    m_cleanScene = s.cleanScene;
    m_appliedSurface = s.appliedSurface;
    m_cleanTexture = s.cleanTexture;
    m_pickedTexture = s.pickedTexture;
    m_cleanSound = s.cleanSound;
    m_pickedSound = s.pickedSound;
    m_viewTouched = s.viewTouched;
    m_textureViewTouched = s.textureViewTouched;
}

void MainWindow::selectSceneObject(int index)
{
    GLWidget *gl = ui->glWidget;
    const int current = gl->selectedObject();
    if (index < 0 || index >= gl->objectCount() || index == current) return;
    saveObjectSession(m_objectSessions[current]);
    // m_geoAnimTimer e' il flusso del SELEZIONATO: passa all'oggetto aperto.
    // Quello lasciato, se era in moto, continua con m_geoObjectsTimer.
    if (m_geoAnimTimer) m_geoAnimTimer->stop();
    gl->selectObject(index);
    openObjectSession(m_objectSessions[index]);
    m_geoTime = m_objectSessions[index].geoTime;
    if (m_objectSessions[index].geoAnimating && m_geoAnimTimer) m_geoAnimTimer->start();
    showSelectedObject();
    refreshObjectsUi();
    syncGeodesicObjectsTimer();
}

void MainWindow::advanceGeodesicObjectsBy(double dtSeconds)
{
    GLWidget *gl = ui->glWidget;
    if (!gl) return;
    const int selected = gl->selectedObject();
    for (int k = 0; k < gl->objectCount() && k < int(m_objectSessions.size()); ++k) {
        ObjectSession &s = m_objectSessions[k];
        // Fuori dalla sua finestra della timeline il flusso resta fermo.
        if (k == selected || !s.geoAnimating || !gl->isObjectOnStage(k)) continue;
        // La stessa velocita' del selezionato (advanceGeodesicFlowBy): 0.015
        // unita' per tick nominale di 16 ms.
        s.geoTime += 0.015 * (dtSeconds * 1000.0 / 16.0);
        // Un errore ferma il SUO flusso, in silenzio: i popup sono dell'oggetto
        // selezionato, e riselezionandolo il Run lo rimette in moto.
        if (!updateGeodesicObject(k)) s.geoAnimating = false;
    }
    syncGeodesicObjectsTimer();
}

bool MainWindow::updateGeodesicObject(int k)
{
    GLWidget *gl = ui->glWidget;
    if (!gl || k < 0 || k >= gl->objectCount() || k >= int(m_objectSessions.size())) return false;
    SurfaceEngine *engine = gl->objectEngine(k);
    if (!engine) return false;
    const ObjectSession &s = m_objectSessions[k];
    // Gli ingressi APPLICATI, come fa il selezionato a moto in corso: le
    // equazioni dell'ultimo Run, il dominio e le costanti del suo motore.
    const EquationTexts &eq = s.eqApplied ? *s.eqApplied : s.state.eq;
    GeodesicInputs in;
    in.x = eq.x; in.y = eq.y; in.z = eq.z; in.p = eq.p;
    in.u = eq.geoU; in.v = eq.geoV; in.w = eq.geoW;
    in.du = eq.geoDU; in.dv = eq.geoDV; in.dw = eq.geoDW;
    in.conform = eq.conform;
    in.metric = s.metricScriptBody;
    in.uMin = engine->getUMin(); in.uMax = engine->getUMax();
    in.vMin = engine->getVMin(); in.vMax = engine->getVMax();
    in.steps = std::min(s.state.steps, 500);
    in.constants = gl->objectConstants(k);
    QString error;
    return computeGeodesicMesh(k, in, s.geoTime, &error) == GeoMeshResult::Ok;
}

void MainWindow::syncGeodesicObjectsTimer()
{
    GLWidget *gl = ui->glWidget;
    if (!gl) return;
    bool any = false;
    for (int k = 0; k < gl->objectCount() && k < int(m_objectSessions.size()); ++k)
        if (k != gl->selectedObject() && m_objectSessions[k].geoAnimating) any = true;
    if (!any || !isVisible()) {
        if (m_geoObjectsTimer) m_geoObjectsTimer->stop();
        return;
    }
    if (!m_geoObjectsTimer) {
        m_geoObjectsTimer = new QTimer(this);
        m_geoObjectsTimer->setInterval(16);
        connect(m_geoObjectsTimer, &QTimer::timeout, this, [this]() {
            // Durante il REC lo avanza il loop del recorder, col dt del frame.
            if (m_isRecording || !ui->glWidget || !ui->glWidget->getRhi()) return;
            advanceGeodesicObjectsBy(m_geoObjectsTimer->interval() / 1000.0);
        });
    }
    if (!m_geoObjectsTimer->isActive()) m_geoObjectsTimer->start();
}

int MainWindow::addSceneObject(const QString &name)
{
    // Il primo oggetto fa dell'ambiente l'ambiente di una scena: le sue
    // rotazioni si fermano, e non sono piu' il moto da riavviare. La scena si
    // compone ferma; GO sull'ambiente, a path fermo, la fa poi girare tutta
    // (GLWidget::sceneRotation).
    if (ui->glWidget->objectCount() == 1) {
        ui->glWidget->setObjectRotating(0, false);
        if (m_scene.lastCameraMotion == QLatin1String("rotation")) m_scene.lastCameraMotion.clear();
        if (ui->btnStart_2) ui->btnStart_2->setText("GO");
    }
    const int index = ui->glWidget->addObject();
    m_objectSessions.resize(index + 1);
    m_objectSessions[index].name = name;
    refreshObjectsUi();
    return index;
}

int MainWindow::addPresetAsObject(const LibraryItem &d, const QString &name)
{
    // L'oggetto nuovo, selezionato, coi dock sulla sua sessione vuota; poi lo
    // stesso load di una Shape, ristretto all'oggetto (asObject): la scena --
    // camera, sfondo, path, suono, luce 4D -- resta com'e'.
    const int index = addSceneObject(name);
    selectSceneObject(index);
    // Durante il load un reset annidato (cambio di linguetta) non deve tornare
    // al solo ambiente: toglierebbe l'oggetto che si sta riempiendo.
    const bool wasLoading = m_loadingAsObject;
    m_loadingAsObject = true;
    // Il load ristretto all'oggetto non passa da showSceneHint: il messaggio
    // della Shape si scrive qui, nell'oggetto selezionato.
    m_currentHintText = d.hintText.trimmed();
    m_currentHintSeconds = d.hintSeconds;
    // Un record (gli oggetti di una scena salvata, i duplicati) porta anche
    // texture, fasce e moti: il suo load, ristretto all'oggetto.
    if (d.type == LibraryType::Motion) applyMotionExample(d, /*asObject=*/true);
    else applySurfaceExample(d, /*asObject=*/true);
    m_loadingAsObject = wasLoading;
    // L'oggetto e' quello del preset: pulito rispetto a se stesso. Che la
    // SCENA sia cambiata lo dice chi lo aggiunge (m_sceneObjectsChanged).
    markSceneClean();
    return index;
}

void MainWindow::removeSceneObject(int index)
{
    GLWidget *gl = ui->glWidget;
    if (index <= 0 || index >= gl->objectCount()) return;
    const bool wasSelected = gl->selectedObject() == index;
    if (wasSelected) selectSceneObject(0);
    // Il suo GO se ne va con lui: senza altri oggetti in rotazione il tick si ferma.
    gl->setObjectRotating(index, false);
    gl->removeObject(index);
    m_objectSessions.erase(m_objectSessions.begin() + index);
    // Senza oggetti la rotazione della scena si azzera (GLWidget::removeObject):
    // anche quella d'apertura, o un RESET dopo un Add to Scene la rimetterebbe.
    if (gl->objectCount() == 1) m_startSceneRotation = GLWidget::SceneRotation();
    m_sceneObjectsChanged = true;
    refreshObjectsUi();
    syncGeodesicObjectsTimer();
}

// P "4D": un'espressione, o un numero non nullo. Lo stesso criterio del tasto
// 4D della barra di stato (update4DButtonState).
static bool pCoordinateIs4D(QString p)
{
    p = p.trimmed();
    if (p.isEmpty()) return false;
    p.replace(QLatin1Char(','), QLatin1Char('.'));
    bool isNumber = false;
    const float v = p.toFloat(&isNumber);
    return !isNumber || std::abs(v) > 0.001f;
}

// SCENA 3D O 4D (decisione dell'utente, 2026-10-20): una scena 3D contiene
// oggetti 3D e Cross Section in ray marching, tagliate tutte dalla stessa
// quota p; una scena 4D oggetti 3D e 4D, e niente Cross Section. La decide
// l'ambiente: parametrico con la P 4D. Il path 4D no: anche i record delle
// Cross Section ne hanno uno.
void MainWindow::restartSceneTimeline()
{
    if (!ui->glWidget) return;
    ui->glWidget->setSceneTime(0.0f);
    m_sceneTimeReached = 0.0f;
}

void MainWindow::advanceSceneBy(double dt, bool selectedGeoFlow)
{
    GLWidget *gl = ui->glWidget;
    // Path della camera: lo stesso avanzamento del tick live
    // (CameraPaths::advance), in tick REALI del suo timer (l'intervallo e' in
    // ms): la velocita' e' quella vista a schermo. Il vecchio 30.0f nominale
    // del recorder (vs 33.3 reali) rendeva i path ~11% piu' lenti nel video.
    for (CameraPaths::Path p : { CameraPaths::Path4D, CameraPaths::Path3D }) {
        if (!pathRunning(p)) continue;
        m_paths->advance(p, float(dt * 1000.0 / std::max(1, m_paths->interval(p))));
        break;   // uno solo: i due path sono esclusivi
    }
    // Rotazioni: lo stesso predicato del tick live (rotationTimer in corsa e
    // almeno un GO acceso) e la stessa cinematica (advanceRotationsBy, ogni
    // oggetto col suo GO). FUORI dalla catena dei path: in una scena gli
    // oggetti ruotano mentre la camera segue un path.
    if (gl->anyObjectRotating()) gl->advanceRotationsBy(float(dt));
    // Flussi geodetici, del selezionato e degli altri oggetti.
    if (selectedGeoFlow) advanceGeodesicFlowBy(dt);
    advanceGeodesicObjectsBy(dt);
    // Orologi di animazione (t di geometria, texture, sfondo, fasce) e path
    // degli oggetti. T avanza col path della camera, qui sopra.
    gl->advanceClocksBy(float(dt));
}

void MainWindow::seekSceneTime(float target)
{
    GLWidget *gl = ui->glWidget;
    const std::optional<CameraPaths::Path> path = cameraPath();
    if (!gl || !path || m_isRecording) return;
    target = std::max(0.0f, target);
    // Indietro si riparte da 0 (T compreso), poi avanti con CameraPaths::advance,
    // lo stesso del tick, UN TICK ALLA VOLTA alla velocita' di adesso: la
    // camera del path 4D dipende anche dal passo prima (la proiezione usa
    // l'osservatore precedente, l'anti-ribaltamento l'"up" precedente), e con
    // un salto unico il punto d'arrivo cambiava secondo da dove si partiva. Un
    // minuto sono 2000 tick: istantaneo. In corsa o fermo, il path resta
    // com'era; gli oggetti non si toccano (il cursore e' solo dell'ambiente).
    if (target < gl->sceneTime()) m_paths->resetTime(*path);
    float ticks = (target - gl->sceneTime()) * 1000.0f / float(std::max(1, m_paths->interval(*path)));
    // Prima il pezzo di tick, poi i tick interi: l'ultimo passo e' sempre un
    // tick, come dal vivo, e l'arrivo non dipende dalla partenza.
    const float fraction = ticks - std::floor(ticks);
    if (ticks <= 0.0f) m_paths->applyCameraAt(*path, m_paths->time(*path));
    else if (fraction > 1e-4f) m_paths->advance(*path, fraction);
    for (int k = int(std::floor(ticks)); k > 0; --k) m_paths->advance(*path, 1.0f);
    gl->setSceneTime(target);   // esatto, senza gli arrotondamenti dei tick
    m_sceneTimeReached = std::max(m_sceneTimeReached, target);
    refreshSceneTimeLabel();
}

// La scala del cursore: almeno 10 s, e fino al piu' lontano fra il T
// raggiunto e i bordi delle finestre, a multipli di 5 s.
float MainWindow::sceneTimeSpan() const
{
    const GLWidget *gl = ui->glWidget;
    float span = std::max(10.0f, m_sceneTimeReached);
    if (gl) {
        span = std::max(span, gl->sceneTime());
        for (int k = 1; k < gl->objectCount(); ++k)
            span = std::max({ span, gl->objectWindowIn(k), gl->objectWindowOut(k) });
    }
    return std::ceil(span / 5.0f) * 5.0f;
}

void MainWindow::refreshSceneTimeSlider()
{
    if (!m_sceneTimeSlider || !ui->glWidget) return;
    GLWidget *gl = ui->glWidget;
    const float t = gl->sceneTime();
    m_sceneTimeReached = std::max(m_sceneTimeReached, t);
    // Il cursore muove il path dell'ambiente: serve che la camera ne segua uno.
    m_sceneTimeSlider->setEnabled(cameraPath().has_value() && !m_isRecording);
    // Le finestre contano su T: senza un path dell'ambiente T non corre.
    const bool windows = gl->selectedObject() > 0 && (hasPath4DInput() || hasPath3DInput());
    m_objectWindowIn->setEnabled(windows);
    m_objectWindowOut->setEnabled(windows);
    m_sceneTimeValue->setText(QStringLiteral("%1 s").arg(double(t), 0, 'f', 1));
    if (m_sceneTimeSlider->isSliderDown()) return;   // lo sta muovendo l'utente
    const QSignalBlocker block(m_sceneTimeSlider);
    m_sceneTimeSlider->setMaximum(int(std::lround(sceneTimeSpan() * 10.0f)));
    m_sceneTimeSlider->setValue(int(std::lround(t * 10.0f)));
}

void MainWindow::setEnvironmentName(const QString &name)
{
    if (m_objectSessions.empty()) return;
    m_objectSessions[0].name = name;
    refreshObjectsUi();
}

// Il contatore tra parentesi, "Mobius 2 (2)": con lo spazio ("Mobius 2 2") non
// si distingueva da un nome che finisce con un numero.
QString MainWindow::uniqueObjectName(const QString &base) const
{
    auto taken = [this](const QString &n) {
        for (size_t k = 1; k < m_objectSessions.size(); ++k)
            if (m_objectSessions[k].name == n) return true;
        return false;
    };
    if (!taken(base)) return base;
    for (int i = 2;; ++i) {
        const QString n = QStringLiteral("%1 (%2)").arg(base).arg(i);
        if (!taken(n)) return n;
    }
}

const ObjectState &MainWindow::environmentState() const
{
    if (ui->glWidget->selectedObject() == 0 || m_objectSessions.empty())
        return static_cast<const ObjectState &>(m_scene);
    return m_objectSessions[0].state;
}

void MainWindow::updateEnvironmentRotationGate()
{
    GLWidget *gl = ui->glWidget;
    if (!gl) return;
    const QList<QPushButton *> rotation3D = {
        ui->btnStart_2,
        ui->btnPrecessionPlus, ui->btnPrecessionMinus,
        ui->btnNutationPlus, ui->btnNutationMinus,
        ui->btnSpinPlus, ui->btnSpinMinus };
    const QList<QPushButton *> rotation4D = {
        ui->btnOmegaPlus, ui->btnOmegaMinus, ui->btnPhiPlus, ui->btnPhiMinus,
        ui->btnPsiPlus, ui->btnPsiMinus,
        ui->btnOmegaAhead, ui->btnOmegaRear, ui->btnPhiAhead, ui->btnPhiRear,
        ui->btnPsiAhead, ui->btnPsiRear };
    if (gl->environmentRotationLocked()) {
        for (QPushButton *b : rotation3D) if (b) b->setEnabled(false);
        for (QPushButton *b : rotation4D) if (b) b->setEnabled(false);
        m_environmentRotationGated = true;
        return;
    }
    if (!m_environmentRotationGated) return;
    // Uscita dal blocco: le condizioni di sempre. Le velocita' 3D e GO non hanno
    // altre condizioni; le velocita' 4D quella di updateRenderState; i tasti a
    // scatto quelle dei tasti di navigazione (path in corsa, modalita').
    m_environmentRotationGated = false;
    for (QPushButton *b : rotation3D) if (b) b->setEnabled(true);
    const bool rot4DUsable = !implicitMode() || crossSectionTab();
    for (QPushButton *b : { ui->btnOmegaPlus, ui->btnOmegaMinus, ui->btnPhiPlus,
                            ui->btnPhiMinus, ui->btnPsiPlus, ui->btnPsiMinus })
        if (b) b->setEnabled(rot4DUsable);
    updateViewButtonsEnabled();
}

bool MainWindow::sceneIs4D() const
{
    const ObjectState &env = environmentState();
    return !env.implicitMode && pCoordinateIs4D(env.eq.p);
}

// ADD TO SCENE (menu delle Shapes): la Shape come oggetto della scena a
// schermo, accanto agli altri lungo x. La posizione si regola dal dock Objects.
void MainWindow::addShapeToScene(const QString &shapePath)
{
    const LibraryItem *d = m_libraryManager.getSurfaceByPath(shapePath);
    if (!d || d->name.isEmpty()) return;
    const bool crossSection = d->isImplicitMode && d->usesCrossSection;
    const bool object4D = !d->isImplicitMode && (d->isTube ? pCoordinateIs4D(d->tubeP)
                                                          : pCoordinateIs4D(d->w));
    if (sceneIs4D() && crossSection) {
        QMessageBox::information(this, QStringLiteral("Add to Scene"),
            QStringLiteral("This scene is 4D (its environment has a P coordinate): cross sections "
                           "can only be added to 3D scenes, where they are all cut by the same P."));
        return;
    }
    if (!sceneIs4D() && object4D) {
        QMessageBox::information(this, QStringLiteral("Add to Scene"),
            QStringLiteral("This scene is 3D: objects with a P coordinate can only be added to 4D "
                           "scenes (an environment with a P coordinate)."));
        return;
    }
    const int index = addPresetAsObject(*d, uniqueObjectName(d->name));
    ui->glWidget->setObjectPosition(QVector3D(2.5f * float(index), 0.0f, 0.0f));
    captureObjectStartPose();   // la posa a cui lo riporta il RESET
    m_sceneObjectsChanged = true;
    refreshObjectsUi();
}

// UNA SCENA DAL FILE (PresetSerializer::buildSceneJson): l'ambiente col load
// dei record, poi ogni oggetto col load di una Shape come oggetto, nella sua
// posizione. Alla fine e' selezionato l'ambiente, e la scena e' "pulita".
bool MainWindow::loadSceneFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    if (root.value(QStringLiteral("type")).toString() != QLatin1String("scene")) return false;

    LibraryManager lm;
    const LibraryItem env = lm.parseJsonObject(root.value(QStringLiteral("environment")).toObject(),
                                               LibraryType::Motion, path);
    if (env.name.isEmpty()) return false;
    // Il record e' l'ambiente di una scena: se il cambio di modalita' resetta la
    // scena, l'item da tenere evidenziato e' la scena (m_modeSwitchSourceTree).
    m_loadingSceneFile = true;
    applyMotionExample(env);   // torna al solo ambiente e lo carica
    m_loadingSceneFile = false;
    // La timeline da capo.
    ui->glWidget->setSceneTime(0.0f);

    GLWidget *gl = ui->glWidget;
    for (const QJsonValue &v : root.value(QStringLiteral("objects")).toArray()) {
        const QJsonObject o = v.toObject();
        // L'oggetto e' un record ("record", dal 2026-10-21: texture e moti
        // compresi); le scene di prima avevano la sola Shape ("shape").
        const bool asRecord = o.contains(QStringLiteral("record"));
        const LibraryItem shape = lm.parseJsonObject(
            o.value(asRecord ? QStringLiteral("record") : QStringLiteral("shape")).toObject(),
            asRecord ? LibraryType::Motion : LibraryType::Surface, path);
        if (shape.name.isEmpty()) continue;
        addPresetAsObject(shape, o.value(QStringLiteral("name")).toString(shape.name));
        const QJsonArray p = o.value(QStringLiteral("position")).toArray();
        gl->setObjectPosition(QVector3D(float(p.at(0).toDouble()), float(p.at(1).toDouble()),
                                        float(p.at(2).toDouble())));
        gl->setObjectScale(float(o.value(QStringLiteral("scale")).toDouble(1.0)));
        {
            const QJsonObject window = o.value(QStringLiteral("timeline")).toObject();
            gl->setObjectWindow(gl->selectedObject(), float(window.value(QStringLiteral("in")).toDouble(0.0)),
                                float(window.value(QStringLiteral("out")).toDouble(0.0)));
        }
        {
            const QJsonArray path = o.value(QStringLiteral("path")).toArray();
            if (!path.isEmpty())
                gl->setObjectPath(gl->selectedObject(), path.at(0).toString(), path.at(1).toString(),
                                  path.at(2).toString(), path.at(3).toString());
            gl->setObjectPathRunning(gl->selectedObject(), o.value(QStringLiteral("pathRunning")).toBool(true));
        }
        applyObjectMotion(o.value(QStringLiteral("motion")).toObject());
        captureObjectStartPose();
    }
    // La scena girata con l'ambiente (GLWidget::sceneRotation), se c'e': e'
    // anche quella a cui la riporta il RESET.
    {
        const QJsonObject turn = root.value(QStringLiteral("sceneRotation")).toObject();
        GLWidget::SceneRotation r;
        const QJsonArray q = turn.value(QStringLiteral("quaternion")).toArray();
        if (q.size() == 4)
            r.rotation = QQuaternion(float(q.at(0).toDouble()), float(q.at(1).toDouble()),
                                     float(q.at(2).toDouble()), float(q.at(3).toDouble())).normalized();
        const QJsonArray a = turn.value(QStringLiteral("angles4D")).toArray();
        if (a.size() == 3)
            r.angles4D = QVector3D(float(a.at(0).toDouble()), float(a.at(1).toDouble()),
                                   float(a.at(2).toDouble()));
        if (gl->objectCount() > 1) gl->setSceneRotation(r);
        m_startSceneRotation = gl->sceneRotation();
    }
    selectSceneObject(0);
    // Il momento pulito: la scena e' quella del file (come a fine load).
    markWholeSceneClean();
    // La posa d'apertura della SCENA e' quella fotografata dal load
    // dell'ambiente (SceneCleanGuard), prima che un suo path muova la camera:
    // rifotografarla qui, a path gia' partito, mescolava la posizione del path
    // con la direzione salvata e il RESET View guardava fuori dagli oggetti.
    refreshObjectsUi();
    // Il messaggio della scena: ambiente e oggetti, ciascuno col suo nome.
    showComposedObjectsHint();
    return true;
}

void MainWindow::markWholeSceneClean()
{
    GLWidget *gl = ui->glWidget;
    const int selected = gl->selectedObject();
    for (int k = 0; k < gl->objectCount(); ++k) {
        if (k == selected) continue;
        selectSceneObject(k);
        markSceneClean();
    }
    selectSceneObject(selected);
    markSceneClean();
    for (ObjectSession &s : m_objectSessions) s.dirtyAtClose = false;
    m_sceneObjectsChanged = false;
}

// Torna alla scena di un oggetto solo, l'ambiente (modalita' Superficie):
// prima di un load normale o di un reset, che riscrivono la scena intera.
void MainWindow::resetToSingleObject()
{
    GLWidget *gl = ui->glWidget;
    if (gl->objectCount() <= 1 || m_loadingAsObject) return;
    selectSceneObject(0);
    while (gl->objectCount() > 1) removeSceneObject(gl->objectCount() - 1);
    // Un oggetto solo: niente da ricordare degli altri.
    m_sceneObjectsChanged = false;
    m_objectSessions[0].dirtyAtClose = false;
    refreshObjectsUi();
}

void MainWindow::showSelectedObject()
{
    GLWidget *gl = ui->glWidget;
    // Come nel load: i setter dei testi scrivono a segnali bloccati, e chi
    // guarda m_populatingFields sa che non e' un'azione dell'utente.
    const bool wasPopulating = m_populatingFields;
    m_populatingFields = true;

    // MODALITA' E LINGUETTE (stato + vista a segnali bloccati), poi i testi:
    // la stessa strada del load e del reset, sulla scena com'e' ora (la parte
    // dell'oggetto appena aperta, il resto invariato).
    setImplicitMode(m_scene.implicitMode);
    setCrossSectionTab(m_scene.crossSectionTab);
    const SceneState shown = m_scene;
    assignSceneTexts(shown);
    setSteps(m_scene.steps);
    applyModeDependentStepUI(m_scene.implicitMode);

    // DAL MOTORE: trasparenza, luce, colori, resa, bordo, rotazioni, mesh.
    {
        const QSignalBlocker ba(ui->alphaSlider), bl(ui->lightSlider);
        ui->alphaSlider->setValue(qRound(gl->globalAlpha() * 100.0f));
        ui->lblAlphaVal->setText(QString::number(gl->globalAlpha(), 'f', 2));
        const int light = qRound(gl->globalLightIntensity() * 100.0f);
        ui->lightSlider->setValue(light);
        ui->lblValLight->setText(QString::number(light) + " %");
    }
    syncImplicitAlphaSlider(m_scene.implicitMode, false);
    refreshRenderRadios();
    onColorTargetChanged();
    refreshTextureCheckbox();
    updateTextureUIState(m_scene.surfaceTextureState);
    // Il bordo: SOLO la vista dello spessore. setBorderUI no: e' un setter da
    // scena nuova, che azzera lo stop manuale della texture del bordo.
    {
        const float t = gl->borderStyle().radius;
        const QSignalBlocker bb(ui->borderThicknessSlider);
        ui->borderThicknessSlider->setValue(sliderFromBorderThickness(t));
        ui->lblValBorderThickness->setText(borderThicknessText(t));
    }
    updateBorderControlsGate();
    refreshTubeControls();
    updateMeshCountControls();
    updateMeshSelectorRange();
    updateMeshScopeEnabled();
    refreshRotationSpeedLabels();
    // Il GO e' dell'oggetto: il tasto dice se ruota QUESTO.
    if (ui->btnStart_2) ui->btnStart_2->setText(gl->isAnimating() ? "STOP" : "GO");
    updateEnvironmentRotationGate();

    // COSTANTI, SCRIPT, LIBRARY, PULSANTI.
    refreshConstantSliders();
    updateConstantsUIState();
    updateScriptButtonText();
    refreshScriptEditor();
    syncTextureTreeSelection();
    update4DButtonState();
    updateSurfaceControlsGate();
    updateMasterButtonState();

    m_populatingFields = wasPopulating;
    gl->update();
}

// ==========================================================
// DOCK OBJECTS E SELETTORE
// ==========================================================

void MainWindow::setupObjectsDock()
{
    m_dockObjects = new QDockWidget(QStringLiteral("OBJECTS"), this);
    m_dockObjects->setObjectName(QStringLiteral("dockObjects"));
    // Largo come gli altri dock (400 nel .ui): senza, le righe di campi
    // affiancati lo allargavano, e su iPhone avrebbe preso tutto lo schermo.
    m_dockObjects->setMinimumWidth(400);
    m_dockObjects->setMaximumWidth(400);
    auto *w = new QWidget(m_dockObjects);
    auto *lay = new QVBoxLayout(w);

    auto *help = new QLabel(QStringLiteral("The environment is what you open from the Library "
                                           "(a Shape or a Record), or nothing after NEW. "
                                           "Add objects to it: right-click a Shape, then Add to Scene."), w);
    help->setWordWrap(true);
    lay->addWidget(help);

    m_objectList = new QListWidget(w);
    lay->addWidget(m_objectList, 1);

    // I campi numerici: sul desktop X Y Z in una riga, Scale in una e In Out
    // nell'altra (Scale In Out insieme chiedevano 423px, il dock ne ha 400:
    // provato). Su mobile coi tasti freccia da dito del selettore Multi Mesh
    // (installMobileSpinButtons, in fondo): le frecce native erano quasi
    // irraggiungibili. E uno per riga, etichette in colonna (pareggiate in
    // fondo): tre campi coi tasti chiedono ~630px, il dock ne ha 400 su iPad e
    // ~345 sul telefono (provato: la riga sbordava).
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    const bool fieldPerRow = true;
#else
    const bool fieldPerRow = false;
#endif
    // La riga del campo: un gruppo nuovo, o ogni campo sul telefono, la apre.
    QHBoxLayout *row = nullptr;
    auto fieldRow = [&](bool newGroup) {
        if (!row || newGroup || fieldPerRow) {
            row = new QHBoxLayout();
            lay->addLayout(row);
        }
        return row;
    };
    QList<QLabel*> rowLabels;
    auto rowLabel = [w, &rowLabels](const QString &text) {
        auto *label = new QLabel(text, w);
        rowLabels << label;
        return label;
    };

    // POSIZIONE del selezionato (l'ambiente resta nell'origine: i path della
    // camera sono suoi).
    const char *axes[3] = { "X", "Y", "Z" };
    for (int i = 0; i < 3; ++i) {
        QHBoxLayout *posRow = fieldRow(i == 0);
        posRow->addWidget(rowLabel(QLatin1String(axes[i])));
        m_objectPos[i] = new QDoubleSpinBox(w);
        m_objectPos[i]->setRange(-100.0, 100.0);
        m_objectPos[i]->setSingleStep(0.1);
        m_objectPos[i]->setDecimals(2);
        m_objectPos[i]->setLocale(QLocale::c());   // il punto, come i campi delle equazioni
        posRow->addWidget(m_objectPos[i], 1);
        connect(m_objectPos[i], &QDoubleSpinBox::valueChanged, this, [this](double) {
            if (m_refreshingObjects) return;
            ui->glWidget->setObjectPosition(QVector3D(float(m_objectPos[0]->value()),
                                                      float(m_objectPos[1]->value()),
                                                      float(m_objectPos[2]->value())));
            m_sceneObjectsChanged = true;
        });
    }

    // PATH del selezionato (timeline 2a): X(t), Y(t), Z(t) che si sommano alla
    // posizione e, nelle scene 4D, P(t) lungo la quarta coordinata; t = il
    // tempo dell'oggetto. Vuoti = nessun path. In una matrice 2x2 (X Y / Z P)
    // col suo GO accanto: quattro campi in fila non stanno nei 400px del dock.
    // L'Invio: sul desktop lo consuma il filtro tastiera (isObjectPathField);
    // su mobile il dock nasce dopo il filtro (setupStyling) e arriva come
    // returnPressed. Uscire dal campo non applica nulla.
    auto *pathGrid = new QGridLayout();
    pathGrid->addWidget(rowLabel(QStringLiteral("Path")), 0, 0, 2, 1);
    const char *pathHints[4] = { "X(t)", "Y(t)", "Z(t)", "P(t)" };
    const char *pathNames[4] = { "lineObjectPathX", "lineObjectPathY", "lineObjectPathZ", "lineObjectPathP" };
    for (int i = 0; i < 4; ++i) {
        m_objectPath[i] = new QLineEdit(w);
        m_objectPath[i]->setObjectName(QLatin1String(pathNames[i]));
        m_objectPath[i]->setPlaceholderText(QLatin1String(pathHints[i]));
        pathGrid->addWidget(m_objectPath[i], i / 2, 1 + i % 2);
        connect(m_objectPath[i], &QLineEdit::textChanged, this, [this]() {
            if (!m_refreshingObjects) refreshObjectPathControls();
        });
        connect(m_objectPath[i], &QLineEdit::returnPressed, this, [this, i]() {
            m_objectPath[i]->clearFocus();
            QGuiApplication::inputMethod()->hide();
            applyObjectPathFromDock();
        });
    }
    pathGrid->setColumnStretch(1, 1);
    pathGrid->setColumnStretch(2, 1);
    m_objectPath[3]->setToolTip(QStringLiteral("P(t) moves the object along the fourth coordinate "
                                               "(4D scenes only)."));
    m_btnObjectPathGo = new QPushButton(QStringLiteral("GO"), w);
    pathGrid->addWidget(m_btnObjectPathGo, 0, 3, 2, 1, Qt::AlignVCenter);
    connect(m_btnObjectPathGo, &QPushButton::clicked, this, &MainWindow::onObjectPathGoClicked);
    lay->addLayout(pathGrid);

    // SCALA del selezionato: la stessa che cambiano rotella, pinch e
    // Back/Forward con l'oggetto selezionato (GLWidget::zoomCamera).
    QHBoxLayout *scaleRow = fieldRow(true);
    scaleRow->addWidget(rowLabel(QStringLiteral("Scale")));
    m_objectScale = new QDoubleSpinBox(w);
    m_objectScale->setRange(0.01, 100.0);
    m_objectScale->setSingleStep(0.1);
    m_objectScale->setDecimals(2);
    m_objectScale->setLocale(QLocale::c());
    scaleRow->addWidget(m_objectScale, 1);

    // TIMELINE: la finestra di comparsa del selezionato, in secondi
    // dell'orologio di scena (0 = dall'inizio / fino alla fine).
    auto makeWindowBox = [w](const QString &zeroText) {
        auto *box = new QDoubleSpinBox(w);
        box->setRange(0.0, 3600.0);
        box->setSingleStep(0.5);
        box->setDecimals(1);
        box->setSuffix(QStringLiteral(" s"));
        box->setSpecialValueText(zeroText);
        box->setLocale(QLocale::c());
        return box;
    };
    fieldRow(true)->addWidget(rowLabel(QStringLiteral("In")));
    m_objectWindowIn = makeWindowBox(QStringLiteral("start"));
    row->addWidget(m_objectWindowIn, 1);
    fieldRow(false)->addWidget(rowLabel(QStringLiteral("Out")));
    m_objectWindowOut = makeWindowBox(QStringLiteral("end"));
    row->addWidget(m_objectWindowOut, 1);
    for (QDoubleSpinBox *box : { m_objectWindowIn, m_objectWindowOut })
        box->setToolTip(QStringLiteral("In and Out count on T, the time of the environment's path."));

    // LO SCORRIMENTO: il cursore porta il path dell'ambiente al tempo T
    // (seekSceneTime).
    auto *seekRow = new QHBoxLayout();
    seekRow->addWidget(rowLabel(QStringLiteral("T")));
    m_sceneTimeSlider = new QSlider(Qt::Horizontal, w);
    m_sceneTimeSlider->setRange(0, 100);
    m_sceneTimeSlider->setSingleStep(1);
    m_sceneTimeSlider->setPageStep(10);
    seekRow->addWidget(m_sceneTimeSlider, 1);
    m_sceneTimeValue = new QLabel(QStringLiteral("0.0 s"), w);
    m_sceneTimeValue->setMinimumWidth(m_sceneTimeValue->fontMetrics().horizontalAdvance(QStringLiteral("0000.0 s")));
    m_sceneTimeValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    seekRow->addWidget(m_sceneTimeValue);
    lay->addLayout(seekRow);
    connect(m_sceneTimeSlider, &QSlider::valueChanged, this, [this](int v) {
        if (!m_refreshingObjects) seekSceneTime(float(v) / 10.0f);
    });

    for (QDoubleSpinBox *box : { m_objectWindowIn, m_objectWindowOut }) {
        connect(box, &QDoubleSpinBox::valueChanged, this, [this](double) {
            if (m_refreshingObjects) return;
            ui->glWidget->setObjectWindow(ui->glWidget->selectedObject(),
                                          float(m_objectWindowIn->value()),
                                          float(m_objectWindowOut->value()));
            m_sceneObjectsChanged = true;
            refreshObjectsUi();
            updateMasterButtonState();
        });
    }
    connect(m_objectScale, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_refreshingObjects) return;
        ui->glWidget->setObjectScale(float(v));
        m_sceneObjectsChanged = true;
    });
    connect(ui->glWidget, &GLWidget::objectScaleChangedByUser, this, [this](float s) {
        m_sceneObjectsChanged = true;
        if (!m_objectScale) return;
        m_refreshingObjects = true;
        m_objectScale->setValue(s);
        m_refreshingObjects = false;
    });

    auto *btnRow = new QHBoxLayout();
    m_btnObjectDuplicate = new QPushButton(QStringLiteral("Duplicate"), w);
    m_btnObjectRemove = new QPushButton(QStringLiteral("Remove"), w);
    btnRow->addWidget(m_btnObjectDuplicate);
    btnRow->addWidget(m_btnObjectRemove);
    lay->addLayout(btnRow);
    if (m_dockTabs) {
        // Dentro la barra di scorrimento (addScrollToDock su mobile) la lista
        // prende la sua altezza preferita, ~190px, e i campi finivano sotto il
        // bordo del telefono: bastano cinque righe, poi scorre lei. Su tablet e
        // desktop resta libera e prende lo spazio che avanza (con un tetto
        // anche li', l'avanzo finiva fra le righe: T isolata in mezzo al dock).
        m_objectList->setMaximumHeight(m_objectList->fontMetrics().lineSpacing() * 5
                                       + 2 * m_objectList->frameWidth());
    }
    // Colonna delle etichette larga quanto la piu' lunga, misurata con lo
    // stile applicato: col font del testo d'aiuto "Scale" sforava di 10px e
    // la sua riga restava sfalsata.
    if (fieldPerRow) {
        int labelWidth = 0;
        for (QLabel *label : std::as_const(rowLabels)) {
            label->ensurePolished();
            labelWidth = qMax(labelWidth, label->sizeHint().width());
        }
        for (QLabel *label : std::as_const(rowLabels)) label->setMinimumWidth(labelWidth);
    }
    // Dopo che i campi sono nelle loro righe: il tasto sostituisce lo spinbox
    // nel suo layout. No-op su desktop.
    for (QAbstractSpinBox *box : std::initializer_list<QAbstractSpinBox*>{
             m_objectPos[0], m_objectPos[1], m_objectPos[2],
             m_objectScale, m_objectWindowIn, m_objectWindowOut })
        UiStyleManager::installMobileSpinButtons(box, 0);
    m_dockObjects->setWidget(w);

    connect(m_objectList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!m_refreshingObjects && row >= 0) selectSceneObjectFromUi(row);
    });
    connect(m_btnObjectDuplicate, &QPushButton::clicked, this, [this]() {
        duplicateSceneObject(ui->glWidget->selectedObject());
    });
    connect(m_btnObjectRemove, &QPushButton::clicked, this, [this]() {
        removeSceneObject(ui->glWidget->selectedObject());
    });
}

void MainWindow::refreshObjectsUi()
{
    if (!m_objectList || !ui->glWidget) return;
    GLWidget *gl = ui->glWidget;
    const int n = gl->objectCount();
    const int selected = gl->selectedObject();
    auto nameOf = [this](int k) {
        const QString name = k < int(m_objectSessions.size()) ? m_objectSessions[k].name : QString();
        return k == 0 ? QStringLiteral("Environment") + (name.isEmpty() ? QString() : QStringLiteral(" — ") + name)
                      : (name.isEmpty() ? QStringLiteral("Object %1").arg(k) : name);
    };
    // Nell'elenco anche la finestra della timeline, se c'e'.
    auto listText = [this, gl, &nameOf](int k) {
        QString text = nameOf(k);
        const float in = gl->objectWindowIn(k), out = gl->objectWindowOut(k);
        if (k > 0 && (in > 0.0f || out > 0.0f)) {
            text += in > 0.0f && out > 0.0f ? QStringLiteral("   [%1 s - %2 s]").arg(in).arg(out)
                  : in > 0.0f              ? QStringLiteral("   [from %1 s]").arg(in)
                                           : QStringLiteral("   [until %1 s]").arg(out);
        }
        return text;
    };

    m_refreshingObjects = true;
    m_objectList->clear();
    for (int k = 0; k < n; ++k) m_objectList->addItem(listText(k));
    m_objectList->setCurrentRow(selected);
    const QVector3D p = gl->objectPosition();
    for (int i = 0; i < 3; ++i) {
        m_objectPos[i]->setValue(i == 0 ? p.x() : i == 1 ? p.y() : p.z());
        m_objectPos[i]->setEnabled(selected > 0);
    }
    m_objectScale->setValue(gl->objectScale());
    m_objectScale->setEnabled(selected > 0);
    {
        const QStringList path = gl->objectPath(selected);
        for (int i = 0; i < 4; ++i) {
            m_objectPath[i]->setText(selected > 0 ? path.value(i) : QString());
            m_objectPath[i]->setEnabled(selected > 0);
        }
    }
    m_objectWindowIn->setValue(gl->objectWindowIn(selected));
    m_objectWindowOut->setValue(gl->objectWindowOut(selected));
    m_btnObjectDuplicate->setEnabled(selected > 0);
    m_btnObjectRemove->setEnabled(selected > 0);
    if (m_btnObjectSelector) {
        m_btnObjectSelector->setVisible(n > 1);
        const QString name = QFontMetrics(m_btnObjectSelector->font())
                                 .elidedText(nameOf(selected), Qt::ElideRight, 110);
        // Niente simboli fuori dal font (era "▾"): il glifo di ripiego e' piu'
        // alto, e la barra di stato cresceva di un pixel togliendolo alla grafica.
        m_btnObjectSelector->setText(QStringLiteral("%1/%2 %3").arg(selected + 1).arg(n).arg(name));
    }
    m_refreshingObjects = false;
    refreshObjectPathControls();
    // Il numero di oggetti o il selezionato possono essere cambiati: le
    // rotazioni dell'ambiente si spengono o si riaccendono.
    updateEnvironmentRotationGate();
    refreshSceneTimeLabel();
}

// INVIO su un campo del path: la regola dei path dell'ambiente, col path
// FERMO che aspetta il suo GO (richiesta dell'utente, 2026-10-24: prima ogni
// Invio, e ogni uscita dal campo, applicava e avviava anche un path scritto a
// meta'). In corsa la modifica entra subito; svuotati tutti i campi il path
// si toglie, e con lui il moto.
void MainWindow::applyObjectPathFromDock()
{
    GLWidget *gl = ui->glWidget;
    if (!gl->isObjectPathRunning(gl->selectedObject())) return;
    if (commitObjectPathFields()) {
        updateMasterButtonState();
        refreshSceneTimeLabel();
    }
}

// GO: applica i campi e avvia il path, dal tempo a cui era (come Departure);
// a path in corsa lo ferma. Vale anche a scena ferma dal master: e' il
// comando del solo path, come GO per le rotazioni.
void MainWindow::onObjectPathGoClicked()
{
    GLWidget *gl = ui->glWidget;
    const int selected = gl->selectedObject();
    if (selected <= 0) return;
    if (gl->isObjectPathRunning(selected)) {
        gl->setObjectPathRunning(selected, false);
    } else {
        if (!commitObjectPathFields()) return;
        gl->setObjectPathRunning(selected, true);
    }
    updateMasterButtonState();
    refreshSceneTimeLabel();
}

// Il path scritto nel dock, al selezionato. Senza la sostituzione virgola ->
// punto dei path della camera: romperebbe mod(a,b), atan2(y,x) e simili.
bool MainWindow::commitObjectPathFields()
{
    // Un secondo Invio mentre il popup d'errore e' aperto.
    if (m_refreshingObjects || m_applyingObjectPath) return false;
    GLWidget *gl = ui->glWidget;
    const int selected = gl->selectedObject();
    if (selected <= 0) return false;
    QString text[4];
    for (int i = 0; i < 4; ++i) text[i] = m_objectPath[i]->text().trimmed();
    const QStringList current = gl->objectPath(selected);
    if (text[0] == current.value(0) && text[1] == current.value(1) && text[2] == current.value(2)
        && text[3] == current.value(3))
        return true;
    m_applyingObjectPath = true;
    bool ok = true;
    for (int i = 0; i < 4 && ok; ++i)
        ok = InputValidator::validateExpressionSyntax(this, text[i], QLatin1String("XYZP").mid(i, 1) + "(t)");
    if (ok && !gl->setObjectPath(selected, text[0], text[1], text[2], text[3])) {
        ok = false;
        QMessageBox::warning(this, QStringLiteral("Error"),
                             QStringLiteral("Object path compilation error.\nCheck the syntax: "
                                            "t and the constants A-F and s."));
    }
    m_applyingObjectPath = false;
    if (!ok) return false;   // il testo resta, per correggerlo; il path e' quello di prima
    m_sceneObjectsChanged = true;
    refreshObjectPathControls();
    return true;
}

void MainWindow::refreshObjectPathControls()
{
    if (!m_btnObjectPathGo || !ui->glWidget) return;
    GLWidget *gl = ui->glWidget;
    const int selected = gl->selectedObject();
    const bool running = gl->isObjectPathRunning(selected);
    bool written = false;
    for (QLineEdit *field : m_objectPath) written = written || !field->text().trimmed().isEmpty();
    m_btnObjectPathGo->setText(running ? QStringLiteral("STOP") : QStringLiteral("GO"));
    // GO avvia cio' che e' scritto: senza campi non c'e' niente da avviare.
    m_btnObjectPathGo->setEnabled(selected > 0 && (running || written));
    // P(t) solo nelle scene 4D e sugli oggetti parametrici (il ray marching non
    // passa dalla proiezione 4D). Una P gia' scritta resta modificabile, per
    // poterla togliere se l'ambiente non e' piu' 4D.
    m_objectPath[3]->setEnabled(selected > 0
                                && ((sceneIs4D() && !implicitMode())
                                    || !m_objectPath[3]->text().trimmed().isEmpty()));
}

// T NELLA BARRA DI STATO (in una scena con la camera su un path) e il
// cursore del dock: aggiornati 5 volte al secondo, sempre (il path parte e si
// ferma da tanti punti).
void MainWindow::refreshSceneTimeLabel()
{
    if (!m_sceneTimeLabel || !ui->glWidget) return;
    const bool show = ui->glWidget->objectCount() > 1 && cameraPath().has_value();
    m_sceneTimeLabel->setVisible(show);
    if (show)
        m_sceneTimeLabel->setText(QStringLiteral("T %1 s").arg(double(ui->glWidget->sceneTime()), 0, 'f', 1));
    refreshSceneTimeSlider();
    if (!m_sceneTimeLabelTimer) {
        m_sceneTimeLabelTimer = new QTimer(this);
        m_sceneTimeLabelTimer->setInterval(200);
        connect(m_sceneTimeLabelTimer, &QTimer::timeout, this, [this]() { refreshSceneTimeLabel(); });
        m_sceneTimeLabelTimer->start();
    }
}

void MainWindow::selectSceneObjectFromUi(int index)
{
    selectSceneObject(index);
    refreshObjectsUi();
    // Il suo messaggio (quali slider usa); l'ambiente quello della scena intera.
    if (index == 0) showComposedObjectsHint();
    else if (!m_currentHintText.isEmpty()) showSceneHint(m_currentHintText, m_currentHintSeconds);
}

void MainWindow::showObjectSelectorMenu()
{
    QMenu menu(this);
    GLWidget *gl = ui->glWidget;
    for (int k = 0; k < gl->objectCount(); ++k) {
        QAction *a = menu.addAction(m_objectList ? m_objectList->item(k)->text() : QString::number(k));
        a->setCheckable(true);
        a->setChecked(k == gl->selectedObject());
        connect(a, &QAction::triggered, this, [this, k]() { selectSceneObjectFromUi(k); });
    }
    menu.exec(m_btnObjectSelector->mapToGlobal(QPoint(0, 0)) - QPoint(0, menu.sizeHint().height()));
}

void MainWindow::duplicateSceneObject(int index)
{
    GLWidget *gl = ui->glWidget;
    if (index <= 0 || index >= gl->objectCount()) return;
    selectSceneObject(index);
    const ObjectSession source = m_objectSessions[index];
    // Come un record: texture, fasce e orientamento compresi.
    const QJsonObject shape = m_presetSerializer->buildMotionJson(source.name, PresetSerializer::MotionRunState{},
                                                                  /*includeSound=*/false);
    const QVector3D pos = gl->objectPosition();
    const float scale = gl->objectScale();
    const float windowIn = gl->objectWindowIn(index), windowOut = gl->objectWindowOut(index);
    const QStringList path = gl->objectPath(index);
    const bool pathRunning = gl->isObjectPathRunning(index);
    const QJsonObject motion = objectMotionJson();
    LibraryManager lm;
    LibraryItem copy = lm.parseJsonObject(shape, LibraryType::Motion, QString());
    copy.hintText = m_currentHintText;       // l'oggetto e' quello selezionato
    copy.hintSeconds = m_currentHintSeconds;
    // Il contatore della sorgente si toglie prima di contare: la copia di
    // "Mobius 2 (2)" e' "Mobius 2 (3)", non "Mobius 2 (2) (2)". La copia nuova
    // diventa il selezionato, quindi Duplicate ripetuto copia sempre una copia.
    static const QRegularExpression counter(QStringLiteral(" \\(\\d+\\)$"));
    QString stem = source.name;
    stem.remove(counter);
    addPresetAsObject(copy, uniqueObjectName(stem));
    gl->setObjectPosition(pos + QVector3D(0.5f, 0.5f, 0.0f));
    gl->setObjectScale(scale);
    gl->setObjectWindow(gl->selectedObject(), windowIn, windowOut);
    gl->setObjectPath(gl->selectedObject(), path.value(0), path.value(1), path.value(2), path.value(3));
    gl->setObjectPathRunning(gl->selectedObject(), pathRunning);
    applyObjectMotion(motion);
    captureObjectStartPose();
    m_sceneObjectsChanged = true;
    refreshObjectsUi();
}

QJsonObject MainWindow::objectMotionJson() const
{
    const GLWidget *gl = ui->glWidget;
    QJsonObject speeds;
    speeds["nutation"] = double(gl->getNutationSpeed());
    speeds["precession"] = double(gl->getPrecessionSpeed());
    speeds["spin"] = double(gl->getSpinSpeed());
    speeds["omega"] = double(gl->getOmegaSpeed());
    speeds["phi"] = double(gl->getPhiSpeed());
    speeds["psi"] = double(gl->getPsiSpeed());
    QJsonObject motion;
    motion["speeds"] = speeds;
    // Il GO, anche a tick fermo: un Save dal menu della Library avviene coi
    // moti tenuti fermi (GLWidget::holdObjectMotion).
    motion["rotating"] = gl->isGoOn();
    return motion;
}

void MainWindow::applyObjectMotion(const QJsonObject &motion)
{
    GLWidget *gl = ui->glWidget;
    const QJsonObject s = motion.value(QStringLiteral("speeds")).toObject();
    gl->setNutationSpeed(float(s.value(QStringLiteral("nutation")).toDouble()));
    gl->setPrecessionSpeed(float(s.value(QStringLiteral("precession")).toDouble()));
    gl->setSpinSpeed(float(s.value(QStringLiteral("spin")).toDouble()));
    gl->setOmegaSpeed(float(s.value(QStringLiteral("omega")).toDouble()));
    gl->setPhiSpeed(float(s.value(QStringLiteral("phi")).toDouble()));
    gl->setPsiSpeed(float(s.value(QStringLiteral("psi")).toDouble()));
    // Il GO solo con qualcosa da far girare, come il tasto (onStopClicked).
    const bool rotating = motion.value(QStringLiteral("rotating")).toBool() && hasAnyRotationSpeed();
    if (rotating) gl->resumeMotion();
    else gl->pauseMotion();
    refreshRotationSpeedLabels();
    if (ui->btnStart_2) ui->btnStart_2->setText(gl->isAnimating() ? "STOP" : "GO");
}

QString MainWindow::composedObjectsHint() const
{
    // Il messaggio dell'oggetto selezionato e' in m_currentHintText: la sua
    // sessione si aggiorna solo quando lo si lascia.
    // Ogni messaggio UNA volta: due oggetti uguali (o un oggetto uguale
    // all'ambiente) hanno lo stesso testo, e ripeterlo non dice niente di piu'.
    // Davanti al testo i nomi di tutti gli oggetti che lo condividono.
    const int selected = ui->glWidget->selectedObject();
    QString envText;
    QStringList texts;                 // nell'ordine degli oggetti
    QHash<QString, QStringList> names; // testo -> oggetti che lo hanno
    for (int k = 0; k < int(m_objectSessions.size()); ++k) {
        const QString text = (k == selected ? m_currentHintText : m_objectSessions[k].hintText).trimmed();
        if (text.isEmpty()) continue;
        if (k == 0) { envText = text; continue; }
        if (text == envText) continue;   // lo dice gia' l'ambiente
        if (!names.contains(text)) texts << text;
        names[text] << m_objectSessions[k].name;
    }
    QStringList parts;
    if (!envText.isEmpty()) parts << envText;
    for (const QString &text : texts)
        parts << names.value(text).join(QStringLiteral(", ")) + QStringLiteral(": ") + text;
    return parts.join(QStringLiteral("\n\n"));
}

// Il messaggio composto si mostra e basta: showSceneHint lo scriverebbe in
// m_currentHintText, e al primo Save diventerebbe il messaggio dell'ambiente.
void MainWindow::showComposedObjectsHint()
{
    const QString text = composedObjectsHint();
    if (text.isEmpty() || !ui->glWidget) return;
    if (!m_hintOverlay) m_hintOverlay = new SceneHintOverlay(ui->glWidget, this);
    const float seconds = ui->glWidget->selectedObject() == 0 ? m_currentHintSeconds
                                                               : m_objectSessions[0].hintSeconds;
    m_hintOverlay->show(text, seconds);
}
