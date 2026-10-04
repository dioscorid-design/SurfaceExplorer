// mainwindow_state.cpp - MainWindow: le sedi dello stato della scena e i loro setter --
// testi di equazioni, campi RM, costanti, limiti e path, Steps, modalita',
// sotto-tab, bersaglio, ambito mesh, slot del dock Script.
// Parte della classe MainWindow divisa per argomento (mainwindow_p.h).
#include "mainwindow_p.h"


// Equazione implicita del sotto-tab ATTIVO. I due sotto-tab hanno editor
// separati (lineEquation / lineEquationCrossSection) ma un solo motore: ogni
// punto che chiede "qual e' l'equazione a schermo?" deve passare da qui.
// Leggere ui->lineEquation fisso era il difetto che faceva applicare le texture
// RM alla sfera del 3D mentre si era in Cross Section, e che rendeva invisibile
// al rilevamento dell'animazione una 't' scritta nell'equazione 4D.
QString MainWindow::activeImplicitEquationText() const
{
    const bool crossSectionActive = crossSectionTab();
    return crossSectionActive ? m_scene.rm.crossSection : m_scene.rm.equation;
}

void MainWindow::showSurfaceTarget()
{
    setEditTarget(EditTarget::Surface);
    ui->radioSurface->setEnabled(true);
    refreshTextureCheckbox();
}

void MainWindow::setImplicitMode(bool on)
{
    m_scene.implicitMode = on;
    if (!ui->tabModeSelector) return;
    // A segnali bloccati: il gestore currentChanged fa il reset della scena.
    const QSignalBlocker blocker(ui->tabModeSelector);
    ui->tabModeSelector->setCurrentIndex(on ? 1 : 0);
}

void MainWindow::setMeshScopeAll(bool all)
{
    m_scene.meshScopeAll = all;
    if (!ui->radioMeshAll || !ui->radioMeshOne) return;
    // Si accende solo il radio voluto: l'altro lo spegne il QButtonGroup
    // esclusivo, e lo spegnimento emette comunque toggled(false): segnali
    // bloccati su ENTRAMBI. Il loro toggled e' il clic dell'utente.
    QSignalBlocker b1(ui->radioMeshAll), b2(ui->radioMeshOne);
    (all ? ui->radioMeshAll : ui->radioMeshOne)->setChecked(true);
}

void MainWindow::setCrossSectionTab(bool on)
{
    m_scene.crossSectionTab = on;
    if (!ui->subTabImplicit) return;
    // A segnali bloccati: il gestore currentChanged fa il reset del clic
    // (superficie di default), che qui non va fatto.
    const QSignalBlocker blocker(ui->subTabImplicit);
    ui->subTabImplicit->setCurrentIndex(on ? 1 : 0);
}

void MainWindow::setEditTarget(EditTarget target)
{
    m_editTarget = target;
    const bool bg = (target == EditTarget::Background);
    // I radio sono esclusivi, ma si toccano a segnali bloccati: il gestore
    // toggled di radioBackground fa la transizione del dock, che qui non va
    // fatta (la fa chi chiama). Dal gestore del clic sono gia' giusti.
    QAbstractButton *shown = bg ? static_cast<QAbstractButton*>(ui->radioBackground)
                                : static_cast<QAbstractButton*>(ui->radioSurface);
    if (!shown->isChecked()) {
        const QSignalBlocker bBg(ui->radioBackground), bSurf(ui->radioSurface);
        shown->setChecked(true);
    }
    // Su cosa agiscono zoom, pan e rotazione 2D (mouse in vista 2D, Save
    // Texture, inquadratura data dai load). Prima la scriveva solo il gestore
    // del clic: dopo un load dal bersaglio Background il dock tornava su
    // Surface e la vista 2D restava sullo sfondo, e quattro punti la
    // forzavano a mano sulla superficie prima di scrivere l'inquadratura.
    if (ui->glWidget) ui->glWidget->setFlatViewTarget(bg ? 1 : 0);
}

MainWindow::ScriptSlot MainWindow::targetTextureSlot() const
{
    if (editingBackground()) return SlotBackgroundTexture;
    if (ui->glWidget && ui->glWidget->activeMeshPart() >= 0) return SlotMeshTexture;
    return SlotSurfaceTexture;
}

MainWindow::ScriptSlot MainWindow::shownScriptSlot() const
{
    switch (m_currentScriptMode) {
    case ScriptModeSurface: return SlotSurface;
    case ScriptModeSound:   return SlotSound;
    case ScriptModeTexture: {
        const ScriptSlot target = targetTextureSlot();
        // In Ray Marching la texture di superficie si scrive nel dock Equations
        // (lineTexture): il dock Script non la mostra e non la fa scrivere.
        if (target == SlotSurfaceTexture && implicitMode())
            return SlotNone;
        return target;
    }
    }
    return SlotNone;
}

void MainWindow::syncMeshTextureSlot() const
{
    // La texture della fascia sta nella MeshPart. Questo slot e' il testo in
    // lavorazione per la fascia selezionata: riparte dalla texture EFFICACE
    // della fascia (propria E accesa -- in wireframe o a checkbox tolto lo
    // script resta conservato ma non si mostra) ogni volta che cambia la fascia
    // o cio' che disegna; finche' restano quelle, tiene cio' che l'utente scrive.
    const int part = ui->glWidget ? ui->glWidget->activeMeshPart() : -1;
    const QString engineCode = part >= 0 ? ui->glWidget->activeMeshEffectiveTextureCode() : QString();
    if (part == m_meshTextureScriptPart && engineCode == m_meshTextureScriptBase) return;
    m_meshTextureScriptPart = part;
    m_meshTextureScriptBase = engineCode;
    m_meshTextureScriptText = engineCode;
}

QString MainWindow::scriptText(ScriptSlot slot) const
{
    switch (slot) {
    case SlotSurface:           return m_scene.surfaceScriptText;
    case SlotSurfaceTexture:    return m_scene.surfaceTextureScriptText;
    case SlotMeshTexture:       syncMeshTextureSlot(); return m_meshTextureScriptText;
    case SlotBackgroundTexture: return m_scene.bgTextureScriptText;
    case SlotSound:             return m_scene.soundScriptText;
    case SlotNone:              break;
    }
    return QString();
}

QString MainWindow::appliedScriptText(ScriptSlot slot) const
{
    switch (slot) {
    case SlotSurface:           return m_scene.surfaceScriptApplied;
    case SlotSurfaceTexture:    return m_scene.surfaceTextureCode;
    case SlotMeshTexture:       syncMeshTextureSlot(); return m_meshTextureScriptBase;
    case SlotBackgroundTexture: return m_scene.bgTextureCode;
    // Il suono non ha un "applicato" distinto: si suona cio' che e' scritto.
    case SlotSound:             return m_scene.soundScriptText;
    case SlotNone:              break;
    }
    return QString();
}

void MainWindow::setScriptText(ScriptSlot slot, const QString &text)
{
    switch (slot) {
    case SlotSurface:           m_scene.surfaceScriptText = text; break;
    case SlotSurfaceTexture:    m_scene.surfaceTextureScriptText = text; break;
    case SlotMeshTexture:       syncMeshTextureSlot(); m_meshTextureScriptText = text; break;
    case SlotBackgroundTexture: m_scene.bgTextureScriptText = text; break;
    case SlotSound:             m_scene.soundScriptText = text; break;
    case SlotNone:              return;
    }
    if (slot == shownScriptSlot()) refreshScriptEditor();
}

void MainWindow::refreshScriptEditor()
{
    if (!ui->txtScriptEditor) return;
    const QString text = scriptText(shownScriptSlot());
    if (text == m_scriptEditorText) return;
    m_scriptEditorText = text;
    // A SEGNALI BLOCCATI: e' la vista che si allinea allo stato, non una
    // digitazione (textChanged riscriverebbe lo slot, segnerebbe la texture
    // come modificata a mano e riaccenderebbe i tasti Run).
    const QSignalBlocker blocker(ui->txtScriptEditor);
    ui->txtScriptEditor->setPlainText(text);
}

void MainWindow::clearSurfaceScript()
{
    setScriptText(SlotSurface, QString());
    m_scene.surfaceScriptApplied.clear();
}

// Moto GO (rotazioni superficie/4D) davvero in corsa: il timer attivo non
// basta, perche' fermando il moto il timer puo' restare vivo col tasto
// tornato su "GO". Quello stop oggi e' codificato SOLO nel testo del bottone:
// questo predicato e' l'unico punto autorizzato a leggerlo.
// Il MODULO EQUAZIONI e' in moto: la geometria e' animata da 't' con il suo
// orologio acceso, oppure il flusso geodetico sta integrando. E' esattamente il
// criterio con cui il tasto Run del dock diventa "Stop" -- e NON e' il master
// button, che va su STOP anche quando si muovono solo rotazioni, path, texture
// o audio, con la geometria ferma.
// Sede unica: la usano updateMasterButtonState (testo e abilitazione dei Run) e
// i filtri tastiera (l'Invio applica al volo solo a modulo in moto).
bool MainWindow::mapEquationsMatchSnapshot() const
{
    // Nessuno snapshot: non c'e' un "gia' applicato" con cui confrontarsi.
    if (!m_eqApplied.has_value()) return false;

    // ...composizione e vincoli compresi: sono nello snapshot come X/Y/Z/P.
    const EquationTexts &a = *m_eqApplied;
    return a.x == m_scene.eq.x && a.y == m_scene.eq.y && a.z == m_scene.eq.z && a.p == m_scene.eq.p
        && a.u == m_scene.eq.u && a.v == m_scene.eq.v && a.w == m_scene.eq.w
        && a.explicitU == m_scene.eq.explicitU && a.explicitV == m_scene.eq.explicitV
        && a.explicitW == m_scene.eq.explicitW;
}


 // --- Geometry & Geodesic Flow ---

QPlainTextEdit *MainWindow::equationFieldEdit(EqField f) const
{
    if (f == &EquationTexts::x) return ui->lineX;
    if (f == &EquationTexts::y) return ui->lineY;
    if (f == &EquationTexts::z) return ui->lineZ;
    if (f == &EquationTexts::p) return ui->lineP;
    if (f == &EquationTexts::u) return ui->lineU;
    if (f == &EquationTexts::v) return ui->lineV;
    if (f == &EquationTexts::w) return ui->lineW;
    if (f == &EquationTexts::explicitU) return ui->lineExplicitU;
    if (f == &EquationTexts::explicitV) return ui->lineExplicitV;
    if (f == &EquationTexts::explicitW) return ui->lineExplicitW;
    if (f == &EquationTexts::geoU)  return ui->lnU;
    if (f == &EquationTexts::geoV)  return ui->lnV;
    if (f == &EquationTexts::geoW)  return ui->lnW;
    if (f == &EquationTexts::geoDU) return ui->lndU;
    if (f == &EquationTexts::geoDV) return ui->lndV;
    if (f == &EquationTexts::geoDW) return ui->lndW;
    if (f == &EquationTexts::conform) return ui->lineConform;
    return nullptr;
}

void MainWindow::bindEquationFields()
{
    const EqField fields[] = {
        &EquationTexts::x, &EquationTexts::y, &EquationTexts::z, &EquationTexts::p,
        &EquationTexts::u, &EquationTexts::v, &EquationTexts::w,
        &EquationTexts::explicitU, &EquationTexts::explicitV, &EquationTexts::explicitW,
        &EquationTexts::geoU, &EquationTexts::geoV, &EquationTexts::geoW,
        &EquationTexts::geoDU, &EquationTexts::geoDV, &EquationTexts::geoDW,
        &EquationTexts::conform,
    };
    for (EqField f : fields) {
        QPlainTextEdit *edit = equationFieldEdit(f);
        if (!edit) continue;
        auto sync = [this, f, edit] { m_scene.eq.*f = edit->toPlainText(); };
        // DUE agganci, stesso gestore. textChanged: connesso qui per primo,
        // gira prima di ogni altro gestore del campo (che m_scene.eq lo legge).
        // contentsChanged del DOCUMENTO: arriva anche quando il campo e'
        // scritto a segnali bloccati, perche' il documento e' un altro oggetto.
        connect(edit, &QPlainTextEdit::textChanged, this, sync);
        connect(edit->document(), &QTextDocument::contentsChanged, this, sync);
        sync();
    }
    // I campi Ray Marching, allo stesso modo, in m_scene.rm.
    for (RmField f : { &ImplicitTexts::equation, &ImplicitTexts::crossSection,
                       &ImplicitTexts::texture, &ImplicitTexts::displacement }) {
        QPlainTextEdit *edit = implicitFieldEdit(f);
        if (!edit) continue;
        auto sync = [this, f, edit] { m_scene.rm.*f = edit->toPlainText(); };
        connect(edit, &QPlainTextEdit::textChanged, this, sync);
        connect(edit->document(), &QTextDocument::contentsChanged, this, sync);
        sync();
    }
}

QPlainTextEdit *MainWindow::implicitFieldEdit(RmField f) const
{
    if (f == &ImplicitTexts::equation)     return ui->lineEquation;
    if (f == &ImplicitTexts::crossSection) return ui->lineEquationCrossSection;
    if (f == &ImplicitTexts::texture)      return ui->lineTexture;
    if (f == &ImplicitTexts::displacement) return ui->lineVariations;
    return nullptr;
}

void MainWindow::setRmText(RmField field, const QString &text)
{
    QPlainTextEdit *edit = implicitFieldEdit(field);
    if (!edit) { m_scene.rm.*field = text; return; }
    // A segnali bloccati: e' il programma che scrive, non l'utente. m_scene.rm segue
    // dal documento (bindEquationFields).
    const QSignalBlocker blocker(edit);
    edit->setPlainText(text);
}

void MainWindow::setEqText(EqField field, const QString &text)
{
    QPlainTextEdit *edit = equationFieldEdit(field);
    if (!edit) { m_scene.eq.*field = text; return; }
    // A segnali bloccati: e' il programma che scrive, non l'utente. m_scene.eq segue
    // dal documento (bindEquationFields).
    const QSignalBlocker blocker(edit);
    edit->setPlainText(text);
}

const std::array<MainWindow::ConstField, 7> &MainWindow::constantFields()
{
    static const std::array<ConstField, 7> fields = {
        &ConstantTexts::a, &ConstantTexts::b, &ConstantTexts::c, &ConstantTexts::d,
        &ConstantTexts::e, &ConstantTexts::f, &ConstantTexts::s,
    };
    return fields;
}

QString MainWindow::constantName(ConstField field)
{
    static const char *const names[7] = { "A", "B", "C", "D", "E", "F", "S" };
    const auto &fields = constantFields();
    for (int i = 0; i < 7; ++i)
        if (fields[i] == field) return QLatin1String(names[i]);
    return QString();
}

QLineEdit *MainWindow::constantFieldEdit(ConstField field) const
{
    if (field == &ConstantTexts::a) return ui->lineA;
    if (field == &ConstantTexts::b) return ui->lineB;
    if (field == &ConstantTexts::c) return ui->lineC;
    if (field == &ConstantTexts::d) return ui->lineD;
    if (field == &ConstantTexts::e) return ui->lineE;
    if (field == &ConstantTexts::f) return ui->lineF;
    if (field == &ConstantTexts::s) return ui->lineS;
    return nullptr;
}

QSlider *MainWindow::constantSlider(ConstField field) const
{
    if (field == &ConstantTexts::a) return ui->aSlider;
    if (field == &ConstantTexts::b) return ui->bSlider;
    if (field == &ConstantTexts::c) return ui->cSlider;
    if (field == &ConstantTexts::d) return ui->dSlider;
    if (field == &ConstantTexts::e) return ui->eSlider;
    if (field == &ConstantTexts::f) return ui->fSlider;
    if (field == &ConstantTexts::s) return ui->sSlider;
    return nullptr;
}

void MainWindow::bindConstantFields()
{
    for (ConstField f : constantFields()) {
        QLineEdit *edit = constantFieldEdit(f);
        if (!edit) continue;
        // Connesso qui per primo: gira prima di ogni altro gestore del campo,
        // che m_scene.constants lo legge. Le scritture a segnali bloccati non arrivano:
        // passano da setConstText. Anche su textEdited, che precede
        // textChanged (vedi bindLineFields).
        const auto write = [this, f](const QString &t) { m_scene.constants.*f = t; };
        connect(edit, &QLineEdit::textEdited, this, write);
        connect(edit, &QLineEdit::textChanged, this, write);
        m_scene.constants.*f = edit->text();
    }
    m_scene.steps = ui->stepSlider->value();
}

QList<QPair<QLineEdit *, QString *>> MainWindow::lineFieldTable()
{
    return {
        { ui->uMinEdit, &m_scene.lim.uMin }, { ui->uMaxEdit, &m_scene.lim.uMax },
        { ui->vMinEdit, &m_scene.lim.vMin }, { ui->vMaxEdit, &m_scene.lim.vMax },
        { ui->wMinEdit, &m_scene.lim.wMin }, { ui->wMaxEdit, &m_scene.lim.wMax },
        { ui->lineXMin, &m_scene.lim.xMin }, { ui->lineXMax, &m_scene.lim.xMax },
        { ui->lineYMin, &m_scene.lim.yMin }, { ui->lineYMax, &m_scene.lim.yMax },
        { ui->lineZMin, &m_scene.lim.zMin }, { ui->lineZMax, &m_scene.lim.zMax },
        { ui->lineX_P, &m_scene.path.x }, { ui->lineY_P, &m_scene.path.y },
        { ui->lineZ_P, &m_scene.path.z }, { ui->lineP_P, &m_scene.path.p },
        { ui->lineAlpha_P, &m_scene.path.alpha }, { ui->lineBeta_P, &m_scene.path.beta },
        { ui->lineGamma_P, &m_scene.path.gamma },
        { ui->lineX_P3D, &m_scene.path.x3D }, { ui->lineY_P3D, &m_scene.path.y3D },
        { ui->lineZ_P3D, &m_scene.path.z3D }, { ui->lineR_P3D, &m_scene.path.roll3D },
    };
}

void MainWindow::bindLineFields()
{
    for (const auto &f : lineFieldTable()) {
        QLineEdit *edit = f.first;
        QString *state = f.second;
        if (!edit) continue;
        // Connesso qui per primo: gira prima di ogni altro gestore del campo.
        // Anche su textEdited, che Qt emette PRIMA di textChanged: i gestori
        // della digitazione (Run one-shot dei limiti, completezza del dominio)
        // leggono lo stato, e col solo textChanged lo trovavano indietro di un
        // tasto -- u max svuotato accendeva il Run, riscritto lo spegneva.
        const auto write = [state](const QString &t) { *state = t; };
        connect(edit, &QLineEdit::textEdited, this, write);
        connect(edit, &QLineEdit::textChanged, this, write);
        *state = edit->text();
    }
}

QString MainWindow::lineText(const QLineEdit *edit) const
{
    for (const auto &f : const_cast<MainWindow *>(this)->lineFieldTable())
        if (f.first == edit) return *f.second;
    return edit ? edit->text() : QString();
}

void MainWindow::setLineText(QString &state, const QString &text)
{
    state = text;
    for (const auto &f : lineFieldTable()) {
        if (f.second != &state) continue;
        if (QLineEdit *edit = f.first) {
            // A segnali bloccati: e' il programma che scrive, non l'utente.
            const QSignalBlocker blocker(edit);
            edit->setText(text);
        }
        return;
    }
}

void MainWindow::setConstText(ConstField field, const QString &text)
{
    m_scene.constants.*field = text;
    if (QLineEdit *edit = constantFieldEdit(field)) {
        // A segnali bloccati: e' il programma che scrive, non l'utente.
        const QSignalBlocker blocker(edit);
        edit->setText(text);
    }
}

void MainWindow::setConstValue(ConstField field, double value)
{
    setConstText(field, QString::number(value, 'g', 6));
}

void MainWindow::refreshConstantSliders()
{
    syncConstantSliders(resolveCascadeConstants(/*restoreTextOnNegative=*/false));
}

void MainWindow::setSteps(int steps)
{
    m_scene.steps = steps;
    // Vista: slider (il suo range si allarga per contenere il valore, o
    // setValue lo taglierebbe in silenzio) e campo, a segnali bloccati.
    const QSignalBlocker bs(ui->stepSlider), bl(ui->lineSteps);
    if (ui->stepSlider->maximum() < steps) ui->stepSlider->setMaximum(std::max(1000, steps));
    if (ui->stepSlider->minimum() > steps) ui->stepSlider->setMinimum(steps);
    ui->stepSlider->setValue(steps);
    ui->lineSteps->setText(QString::number(steps));
}

QString MainWindow::activeEquationsText() const
{
    if (!m_eqApplied) return QString();
    const EquationTexts &a = *m_eqApplied;
    return QStringList{ a.x, a.y, a.z, a.p, a.u, a.v, a.w,
                        a.explicitU, a.explicitV, a.explicitW,
                        a.geoU, a.geoV, a.geoW, a.geoDU, a.geoDV, a.geoDW,
                        a.conform }.join(QLatin1Char(' '));
}

void MainWindow::snapshotActiveEquations() {
    // Composizione (U, V, W in funzione di u, v, w) e vincoli espliciti fanno
    // parte di cio' che e' a schermo quanto X/Y/Z/P, e il flusso geodetico pure:
    // fuori dallo snapshot, il commit di servizio li prendeva dai campi e
    // applicava una composizione o un vincolo ancora in corso di scrittura
    // sulle equazioni di prima.
    m_eqApplied = m_scene.eq;
}
