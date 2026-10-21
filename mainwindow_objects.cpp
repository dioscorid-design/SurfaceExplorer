// mainwindow_objects.cpp - Gli OGGETTI DELLA SCENA lato MainWindow (2.0).
// Ogni oggetto del GLWidget ha qui la sua sessione: la parte dei dock che e'
// sua (ObjectState) e lo stato di lavoro che segue il selezionato. La sessione
// del selezionato e' aperta nei membri di MainWindow (m_scene & co.); cambiare
// oggetto la chiude, apre quella dell'altro e ridisegna i dock. Il motore non
// si tocca: ogni oggetto ha gia' il suo stato nel GLWidget.

#include "mainwindow_p.h"
#include "scenehintoverlay.h"

#include <QDoubleSpinBox>
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
    // Il primo oggetto fa dell'ambiente l'ambiente di una scena, che non ruota:
    // le sue rotazioni si fermano, e non sono piu' il moto da riavviare.
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
    ui->glWidget->setSceneTimeRunning(true);
}

void MainWindow::setEnvironmentName(const QString &name)
{
    if (m_objectSessions.empty()) return;
    m_objectSessions[0].name = name;
    refreshObjectsUi();
}

QString MainWindow::uniqueObjectName(const QString &base) const
{
    auto taken = [this](const QString &n) {
        for (size_t k = 1; k < m_objectSessions.size(); ++k)
            if (m_objectSessions[k].name == n) return true;
        return false;
    };
    if (!taken(base)) return base;
    for (int i = 2;; ++i) {
        const QString n = QStringLiteral("%1 %2").arg(base).arg(i);
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
    applyMotionExample(env);   // torna al solo ambiente e lo carica
    // La timeline da capo.
    ui->glWidget->setSceneTime(0.0f);
    ui->glWidget->setSceneTimeRunning(true);

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
        applyObjectMotion(o.value(QStringLiteral("motion")).toObject());
        captureObjectStartPose();
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
    auto *w = new QWidget(m_dockObjects);
    auto *lay = new QVBoxLayout(w);

    auto *help = new QLabel(QStringLiteral("The environment is what you open from the Library "
                                           "(a Shape or a Record), or nothing after NEW. "
                                           "Add objects to it: right-click a Shape, then Add to Scene."), w);
    help->setWordWrap(true);
    lay->addWidget(help);

    m_objectList = new QListWidget(w);
    lay->addWidget(m_objectList, 1);

    // POSIZIONE del selezionato (l'ambiente resta nell'origine: i path della
    // camera sono suoi).
    auto *posRow = new QHBoxLayout();
    const char *axes[3] = { "X", "Y", "Z" };
    for (int i = 0; i < 3; ++i) {
        posRow->addWidget(new QLabel(QLatin1String(axes[i]), w));
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
    lay->addLayout(posRow);

    // SCALA del selezionato: la stessa che cambiano rotella, pinch e
    // Back/Forward con l'oggetto selezionato (GLWidget::zoomCamera).
    auto *scaleRow = new QHBoxLayout();
    scaleRow->addWidget(new QLabel(QStringLiteral("Scale"), w));
    m_objectScale = new QDoubleSpinBox(w);
    m_objectScale->setRange(0.01, 100.0);
    m_objectScale->setSingleStep(0.1);
    m_objectScale->setDecimals(2);
    m_objectScale->setLocale(QLocale::c());
    scaleRow->addWidget(m_objectScale, 1);
    lay->addLayout(scaleRow);

    // TIMELINE: la finestra di comparsa del selezionato, in secondi
    // dell'orologio di scena (0 = dall'inizio / fino alla fine).
    auto *windowRow = new QHBoxLayout();
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
    windowRow->addWidget(new QLabel(QStringLiteral("In"), w));
    m_objectWindowIn = makeWindowBox(QStringLiteral("start"));
    windowRow->addWidget(m_objectWindowIn, 1);
    windowRow->addWidget(new QLabel(QStringLiteral("Out"), w));
    m_objectWindowOut = makeWindowBox(QStringLiteral("end"));
    windowRow->addWidget(m_objectWindowOut, 1);
    lay->addLayout(windowRow);
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
    m_objectWindowIn->setValue(gl->objectWindowIn(selected));
    m_objectWindowOut->setValue(gl->objectWindowOut(selected));
    m_objectWindowIn->setEnabled(selected > 0);
    m_objectWindowOut->setEnabled(selected > 0);
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
    // Il numero di oggetti o il selezionato possono essere cambiati: le
    // rotazioni dell'ambiente si spengono o si riaccendono.
    updateEnvironmentRotationGate();
    refreshSceneTimeLabel();
}

// T NELLA BARRA DI STATO, solo con una timeline: aggiornato 5 volte al secondo.
void MainWindow::refreshSceneTimeLabel()
{
    if (!m_sceneTimeLabel || !ui->glWidget) return;
    const bool show = ui->glWidget->objectCount() > 1 && ui->glWidget->hasTimeline();
    m_sceneTimeLabel->setVisible(show);
    if (!show) {
        if (m_sceneTimeLabelTimer) m_sceneTimeLabelTimer->stop();
        return;
    }
    m_sceneTimeLabel->setText(QStringLiteral("T %1 s").arg(double(ui->glWidget->sceneTime()), 0, 'f', 1));
    if (!m_sceneTimeLabelTimer) {
        m_sceneTimeLabelTimer = new QTimer(this);
        m_sceneTimeLabelTimer->setInterval(200);
        connect(m_sceneTimeLabelTimer, &QTimer::timeout, this, [this]() {
            if (m_sceneTimeLabel && ui->glWidget)
                m_sceneTimeLabel->setText(QStringLiteral("T %1 s").arg(double(ui->glWidget->sceneTime()), 0, 'f', 1));
        });
    }
    if (!m_sceneTimeLabelTimer->isActive()) m_sceneTimeLabelTimer->start();
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
    const QJsonObject motion = objectMotionJson();
    LibraryManager lm;
    LibraryItem copy = lm.parseJsonObject(shape, LibraryType::Motion, QString());
    copy.hintText = m_currentHintText;       // l'oggetto e' quello selezionato
    copy.hintSeconds = m_currentHintSeconds;
    addPresetAsObject(copy, uniqueObjectName(source.name));
    gl->setObjectPosition(pos + QVector3D(0.5f, 0.5f, 0.0f));
    gl->setObjectScale(scale);
    gl->setObjectWindow(gl->selectedObject(), windowIn, windowOut);
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
