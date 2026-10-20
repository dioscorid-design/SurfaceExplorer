// mainwindow_objects.cpp - Gli OGGETTI DELLA SCENA lato MainWindow (2.0).
// Ogni oggetto del GLWidget ha qui la sua sessione: la parte dei dock che e'
// sua (ObjectState) e lo stato di lavoro che segue il selezionato. La sessione
// del selezionato e' aperta nei membri di MainWindow (m_scene & co.); cambiare
// oggetto la chiude, apre quella dell'altro e ridisegna i dock. Il motore non
// si tocca: ogni oggetto ha gia' il suo stato nel GLWidget.

#include "mainwindow_p.h"

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
    gl->selectObject(index);
    openObjectSession(m_objectSessions[index]);
    showSelectedObject();
}

int MainWindow::addSceneObject(const QString &name)
{
    const int index = ui->glWidget->addObject();
    m_objectSessions.resize(index + 1);
    m_objectSessions[index].name = name;
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
    applySurfaceExample(d, /*asObject=*/true);
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
    gl->removeObject(index);
    m_objectSessions.erase(m_objectSessions.begin() + index);
    m_sceneObjectsChanged = true;
}

// ADD TO SCENE (menu delle Shapes): la Shape come oggetto della scena a
// schermo, accanto agli altri lungo x. La posizione si regola dal dock Objects.
void MainWindow::addShapeToScene(const QString &shapePath)
{
    const LibraryItem *d = m_libraryManager.getSurfaceByPath(shapePath);
    if (!d || d->name.isEmpty()) return;
    // FLUSSO GEODETICO: per ora solo come ambiente. Il suo tempo e il suo timer
    // stanno ancora in MainWindow (m_geoTime, m_geoAnimTimer), uno per tutti.
    static const QRegularExpression kMetricReturnRe(R"(\breturn\s+mat3\s*\()");
    const bool geodesic = !d->geoU0.trimmed().isEmpty() || !d->geoDU.trimmed().isEmpty()
                          || d->scriptCode.contains(kMetricReturnRe);
    if (geodesic) {
        QMessageBox::information(this, QStringLiteral("Add to Scene"),
            QStringLiteral("Geodesic flows can be the environment of a scene (open them from the "
                           "Library), but they cannot be added as objects yet."));
        return;
    }
    const int index = addPresetAsObject(*d, d->name);
    ui->glWidget->setObjectPosition(QVector3D(2.5f * float(index), 0.0f, 0.0f));
    m_sceneObjectsChanged = true;
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

    GLWidget *gl = ui->glWidget;
    for (const QJsonValue &v : root.value(QStringLiteral("objects")).toArray()) {
        const QJsonObject o = v.toObject();
        const LibraryItem shape = lm.parseJsonObject(o.value(QStringLiteral("shape")).toObject(),
                                                     LibraryType::Surface, path);
        if (shape.name.isEmpty()) continue;
        addPresetAsObject(shape, o.value(QStringLiteral("name")).toString(shape.name));
        const QJsonArray p = o.value(QStringLiteral("position")).toArray();
        gl->setObjectPosition(QVector3D(float(p.at(0).toDouble()), float(p.at(1).toDouble()),
                                        float(p.at(2).toDouble())));
    }
    selectSceneObject(0);
    // Il momento pulito: la scena e' quella del file (come a fine load).
    markWholeSceneClean();
    captureStartPose();
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
