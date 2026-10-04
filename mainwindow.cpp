// mainwindow.cpp - MainWindow: costruttore (cablaggio di dock, segnali e filtri
// d'input), distruttore ed eventi della finestra.
// Parte della classe MainWindow divisa per argomento (mainwindow_p.h).
#include "mainwindow_p.h"


class DesktopInputFilter : public QObject {
public:
    DesktopInputFilter(QObject* parent = nullptr) : QObject(parent) {}

protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);

            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
                const QString on = obj->objectName();
                if (on == "txtScriptEditor" || on == "lineVariations" || on == "lineTexture") {
                    return false;
                }

                // Campi del modulo equazioni (principali, composizioni, vincoli,
                // flusso geodetico). Due regimi, ed e' la stessa regola per
                // TUTTI i campi del modulo, limiti u/v/w compresi:
                //  - superficie FERMA: l'Invio non esegue nulla, si aspetta il
                //    Run. Applicare a meta' digitazione mostrerebbe una forma
                //    che non corrisponde a cio' che si sta scrivendo (X
                //    aggiornata, Y e Z ancora vecchie).
                //  - superficie IN MOTO: il modulo e' gia' avviato e il tasto
                //    e' "Stop", quindi un Run da premere non c'e'; l'Invio
                //    applica al volo (commitUiFieldsDuringMotion), senza il
                //    giro Stop+Run che spezzerebbe l'animazione.
                if (isDeferredEquationField(on)) {
                    if (QWidget* w = qobject_cast<QWidget*>(obj)) w->clearFocus();
                    if (MainWindow* mainWin = qobject_cast<MainWindow*>(parent())) {
                        if (mainWin->isEquationModuleMoving())
                            mainWin->commitUiFieldsDuringMotion();
                    }
                    return true;
                }

                // LIMITI U/V/W: hanno un ramo proprio ma seguono la stessa
                // regola dei campi qui sopra -- a superficie ferma il dominio
                // aspetta il Run, in moto entra subito. Il ramo separato serve
                // perche' commitLimitFieldOnEnter, oltre ad applicare, VALIDA il
                // numero (popup su valore illeggibile o min>=max) e registra il
                // dominio nell'engine anche quando non si ridisegna.
                // NON si passa da commitFieldsOnEnter: quella strada rifa' un
                // Run intero, e committerebbe anche equazioni modificate ma non
                // ancora confermate. Qui si tocca il solo dominio.
                if (on == "uMinEdit" || on == "uMaxEdit" ||
                    on == "vMinEdit" || on == "vMaxEdit" ||
                    on == "wMinEdit" || on == "wMaxEdit") {
                    if (QWidget* w = qobject_cast<QWidget*>(obj)) w->clearFocus();
                    if (MainWindow* mainWin = qobject_cast<MainWindow*>(parent())) {
                        mainWin->commitLimitFieldOnEnter(on);
                    }
                    return true;
                }

                // Limiti PER-MESH: ramo proprio per la stessa ragione dei
                // globali qui sopra (il commit valida e avvisa), ma con la
                // funzione gemella -- questi scrivono il dominio della sola
                // parte selezionata, non quello della superficie.
                if (on == "meshUMinEdit" || on == "meshUMaxEdit" ||
                    on == "meshVMinEdit" || on == "meshVMaxEdit") {
                    if (QWidget* w = qobject_cast<QWidget*>(obj)) w->clearFocus();
                    if (MainWindow* mainWin = qobject_cast<MainWindow*>(parent())) {
                        mainWin->commitMeshLimitFieldOnEnter(on);
                    }
                    return true;
                }

                // Campi path camera: l'Invio AVVIA il path se e' fermo e i campi
                // lo definiscono, lo ricompila al volo se e' gia' in corsa, e lo
                // ferma se i campi sono stati svuotati (vedi
                // commitPathFieldOnEnter, che tiene i tre casi in un punto solo).
                if (isPathEquationField(on)) {
                    if (QWidget* w = qobject_cast<QWidget*>(obj)) w->clearFocus();
                    if (MainWindow* mainWin = qobject_cast<MainWindow*>(parent())) {
                        mainWin->commitPathFieldOnEnter(on);
                    }
                    return true;
                }

                if (QWidget* w = qobject_cast<QWidget*>(obj)) {
                    MainWindow* mainWin = qobject_cast<MainWindow*>(parent());

                    // UN SOLO COMMIT PER INVIO. clearFocus() emette subito
                    // editingFinished, e per i campi COSTANTE quell'handler
                    // chiama gia' commitFieldsOnEnter. Chiamandolo di nuovo qui
                    // sotto, onStartClicked girava due volte e ogni validatore
                    // al suo interno mostrava il suo popup due volte di fila
                    // (equazioni incomplete, sintassi, parentesi, uso di W...):
                    // leggono i campi dal vivo, quindi la seconda passata
                    // ritrovava lo stesso testo sbagliato. Si confronta il
                    // contatore attorno al clearFocus(): se e' cambiato, il
                    // commit e' gia' avvenuto e qui non si ripete.
                    const quint64 before = mainWin ? mainWin->commitOnEnterCount() : 0;
                    w->clearFocus();

                    // Applica i campi modificati (sia in moto che a superficie ferma)
                    if (mainWin && mainWin->commitOnEnterCount() == before) {
                        mainWin->commitFieldsOnEnter();
                    }
                    return true;
                }
            }
        }
        return QObject::eventFilter(obj, event);
    }
};

// ==========================================
// Filtro per Mobile: Tastiera e Navigazione
// ==========================================
#if defined(Q_OS_IOS)
// Ultimo editor la cui selezione è stata modificata coi pallini: quando il
// trascinamento entra nella tastiera, il gesto del plugin la chiude azzerando
// il focus (focusWidget() diventa nullo) — questo puntatore permette comunque
// di ripresentare il menu sulla selezione superstite (vedi visibleChanged).
static QPointer<QWidget> s_lastSelectionEditor;

// Autoscroll durante la selezione coi pallini. Trascinando il pallino fino
// al bordo del campo visibile il testo non scorre (Qt non scrolla finché il
// tocco resta nel viewport) e la selezione muore lì. Il trigger è
// QPlainTextEdit::selectionChanged (connesso nel setup iOS): il drag dei
// pallini non genera eventi mouse e il canale QInputMethodEvent non è un
// flusso continuo (entrambi provati su device), mentre il segnale scatta a
// ogni variazione qualunque sia il canale. Quando l'estremo MOBILE della
// selezione entra nella fascia al bordo del viewport (in basso tagliata
// dalla tastiera), un timer scrolla un passo per tick; il plugin ri-mappa
// il dito fermo sul testo che gli scorre sotto ed estende da solo la
// selezione (è il meccanismo osservato nella "valanga" del
// keyboard-avoidance — qui però a passo fisso, vincolato alla fascia).
// La tastiera NON è un requisito: il gesto nascondi-tastiera del plugin
// la uccide spesso a metà drag, ma pallini e drag sopravvivono.
class SelectionAutoScroller : public QObject {
public:
    static SelectionAutoScroller* instance() {
        static SelectionAutoScroller s;
        return &s;
    }

    // Chiamata su ogni selectionChanged. La valutazione è accodata per
    // lavorare a stato del widget assestato (il segnale può arrivare nel
    // mezzo dell'applicazione di un evento).
    void onSelectionEvent(QPlainTextEdit* editor) {
        QMetaObject::invokeMethod(this, [this, ed = QPointer<QPlainTextEdit>(editor)]() {
            if (ed)
                evaluate(ed.data());
        }, Qt::QueuedConnection);
    }

private:
    static constexpr int kBandPx = 60;   // fascia di attivazione al bordo
    static constexpr int kTickMs = 90;   // ~11 righe/secondo

    SelectionAutoScroller() {
        m_timer.setInterval(kTickMs);
        connect(&m_timer, &QTimer::timeout, this, [this]() { tick(); });
    }

    void evaluate(QPlainTextEdit* editor) {
        const QTextCursor c = editor->textCursor();
        if (!c.hasSelection()) {
            stop();
            return;
        }
        if (editor != m_editor) {
            m_editor = editor;
            m_lastAnchor = m_lastPosition = -1;
            m_movingEndIsPosition = true;
        }
        // L'estremo mobile è quello cambiato rispetto all'evento precedente.
        if (c.position() != m_lastPosition)
            m_movingEndIsPosition = true;
        else if (c.anchor() != m_lastAnchor)
            m_movingEndIsPosition = false;
        m_lastAnchor = c.anchor();
        m_lastPosition = c.position();

        if (bandDirection(editor) != 0) {
            // start() su timer attivo lo RIAZZERA e durante il drag i
            // selectionChanged arrivano di continuo: il tick non
            // arriverebbe mai.
            if (!m_timer.isActive()) {
                m_stallTicks = 0;
                m_tickAnchor = m_tickPosition = -1;
                m_timer.start();
            }
        } else {
            stop();
        }
    }

    // Direzione di scroll dalla posizione dell'estremo mobile della
    // selezione rispetto alle fasce sul bordo visibile del viewport (quello
    // inferiore tagliato dalla tastiera). Si usa l'estremo mobile, non il
    // dito: durante il drag dei pallini non esiste alcun evento con la
    // posizione del tocco, ma il pallino sta comunque sul carattere
    // dell'estremo che trascina.
    int bandDirection(QPlainTextEdit* editor) const {
        QWidget* viewport = editor->viewport();
        QRect vis = viewport->visibleRegion().boundingRect();
        if (vis.isEmpty())
            vis = viewport->rect();
        int bottomEdge = vis.bottom();
        const QRectF kb = QGuiApplication::inputMethod()->keyboardRectangle();
        if (!kb.isEmpty()) {
            const int kbTop = viewport->mapFrom(viewport->window(), kb.topLeft().toPoint()).y();
            if (kbTop > vis.top())
                bottomEdge = qMin(bottomEdge, kbTop);
        }
        QTextCursor moving = editor->textCursor();
        if (!m_movingEndIsPosition)
            moving.setPosition(moving.anchor());
        const QRect r = editor->cursorRect(moving);
        if (r.bottom() >= bottomEdge - kBandPx)
            return +1;
        if (r.top() <= vis.top() + kBandPx)
            return -1;
        return 0;
    }

    void tick() {
        // NESSUN vincolo sulla visibilità della tastiera: il gesto
        // nascondi-tastiera del plugin la uccide spesso proprio durante il
        // drag del pallino (il tocco entra nel keyboardEndRect), ma pallini
        // e drag sopravvivono e l'autoscroll deve continuare (visto su
        // device: selezione che cresceva con la tastiera già morta).
        QPlainTextEdit* editor = m_editor.data();
        if (!editor || !editor->textCursor().hasSelection()) {
            stop();
            return;
        }
        const int dir = bandDirection(editor);
        if (dir == 0) {
            stop();
            return;
        }
        // Criterio di vita = il feedback scroll→estensione: se il drag è
        // attivo, il plugin ri-mappa il dito sul testo scrollato e la
        // selezione avanza tra un tick e l'altro. Ferma per 2 tick = drag
        // finito o cambio programmatico (es. Select All): fermarsi qui
        // evita di scrollare da soli fino a fine documento.
        const QTextCursor cur = editor->textCursor();
        if (cur.anchor() == m_tickAnchor && cur.position() == m_tickPosition) {
            if (++m_stallTicks >= 2) {
                stop();
                return;
            }
        } else {
            m_stallTicks = 0;
            m_tickAnchor = cur.anchor();
            m_tickPosition = cur.position();
        }

        // Prima la scrollbar dell'editor (in RIGHE), ma su mobile l'editor
        // spesso mostra tutto il testo ed è la QScrollArea del dock a
        // scorrere (in pixel). A fine corsa non c'è nulla da fare: la
        // selezione smette di avanzare e il criterio qui sopra ferma tutto.
        QScrollBar* bar = editor->verticalScrollBar();
        if (bar && bar->maximum() > bar->minimum()) {
            bar->setValue(bar->value() + dir);
        } else {
            const int stepPx = qMax(1, editor->fontMetrics().lineSpacing());
            for (QWidget* w = editor->parentWidget(); w; w = w->parentWidget()) {
                if (auto* area = qobject_cast<QAbstractScrollArea*>(w)) {
                    QScrollBar* outer = area->verticalScrollBar();
                    if (outer && outer->maximum() > outer->minimum()) {
                        outer->setValue(outer->value() + dir * stepPx);
                        break;
                    }
                }
            }
        }
    }

    void stop() { m_timer.stop(); }

    QPointer<QPlainTextEdit> m_editor;
    int m_lastAnchor = -1;
    int m_lastPosition = -1;
    bool m_movingEndIsPosition = true;
    int m_tickAnchor = -1;
    int m_tickPosition = -1;
    int m_stallTicks = 0;
    QTimer m_timer;
};
#endif

class MobileInputFilter : public QObject {
    bool m_isProcessingQuery = false; // Evita loop infiniti durante l'intercettazione
public:
    MobileInputFilter(QObject* parent = nullptr) : QObject(parent) {}

protected:
    bool eventFilter(QObject* obj, QEvent* event) override {

        // 1. MENU CONTESTUALI
        if (event->type() == QEvent::ContextMenu) {
#if defined(Q_OS_ANDROID)
            // Mouse esterno / DeX: stesso menu completo, senza selezione parola.
            QContextMenuEvent* cme = static_cast<QContextMenuEvent*>(event);
            showEditMenu(obj, cme->globalPos(), false);
            return true;
#elif defined(Q_OS_IOS)
            // Su iOS il menu lo presentiamo noi con UIEditMenuInteraction
            // (ioseditmenu.mm): i percorsi Qt sono entrambi morti sugli iOS
            // recenti (UIMenuController e' un no-op da iOS 17, il QMenu
            // widget non viene renderizzato). L'evento arriva a fine lente
            // (long press) e al tap sul cursore; le voci si adattano alla
            // selezione (Cut/Copy solo se esiste, senno' Paste/Select All/
            // Undo/Redo). Il tap secco altrove resta solo tastiera: per
            // quello il plugin non genera alcun evento.
            if (qobject_cast<QPlainTextEdit*>(obj) || qobject_cast<QLineEdit*>(obj)) {
                iosPresentEditMenu(qobject_cast<QWidget*>(obj),
                    static_cast<QContextMenuEvent*>(event)->globalPos());
                return true; // consumato: niente QMenu Qt ne' fallback del plugin
            }
            return false;
#endif
        }

        // NB (iOS): il doppio tap seleziona la parola (comportamento Qt
        // nativo) ma NON presenta più il menu, per scelta: il menu copriva
        // la zona dei pallini e intralciava l'estensione della selezione.
        // Il menu arriva al rilascio dei pallini, a fine lente (long press)
        // e — via visibleChanged — se la tastiera muore con una selezione.

#if defined(Q_OS_ANDROID)
        // 1b. LONG-PRESS: il mini-popup di sistema (lato Java) è disattivato con
        // QT_QPA_NO_TEXT_HANDLES in main(). Riproduciamo qui il comportamento
        // nativo: selezione della parola sotto il dito + menu completo di Qt
        // (undo, redo, taglia, copia, incolla, elimina, seleziona tutto).
        if (event->type() == QEvent::Gesture) {
            QGestureEvent* ge = static_cast<QGestureEvent*>(event);
            if (QGesture* g = ge->gesture(Qt::TapAndHoldGesture)) {
                if (g->state() == Qt::GestureFinished) {
                    // position() del TapAndHold è in coordinate globali (schermo)
                    showEditMenu(obj, static_cast<QTapAndHoldGesture*>(g)->position().toPoint(), true);
                }
                ge->accept(g);
                return true;
            }
        }
#endif

#if defined(Q_OS_IOS)
        // 1c. Il menu di modifica presentato (UIEditMenuInteraction) non si
        // chiude da solo quando si digita: lo congediamo al primo input di
        // tastiera. Sulla tastiera virtuale il testo arriva come evento
        // input-method, ma conta SOLO se porta testo (commit o preedit): il
        // trascinamento dei pallini genera QInputMethodEvent di sola
        // selezione, che non devono chiudere il menu a fine trascinamento.
        // Su quegli eventi di sola-selezione ricordiamo invece l'editor: se
        // il trascinamento entra nella tastiera e la uccide (gesto del
        // plugin, resign -> focus perso), il connect su visibleChanged usa
        // questo puntatore per ripresentare il menu sulla selezione rimasta.
        if (event->type() == QEvent::KeyPress) {
            iosDismissEditMenu();
        } else if (event->type() == QEvent::InputMethod) {
            auto* ime = static_cast<QInputMethodEvent*>(event);
            if (!ime->commitString().isEmpty() || !ime->preeditString().isEmpty()) {
                iosDismissEditMenu();
            } else if (auto* w = qobject_cast<QWidget*>(obj)) {
                s_lastSelectionEditor = w;
            }
        }
#endif

        // 2. GESTIONE TASTI INVIO E TAB
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);

            // Intercettiamo Return, Enter e Tab
            if (keyEvent->key() == Qt::Key_Return ||
                keyEvent->key() == Qt::Key_Enter ||
                keyEvent->key() == Qt::Key_Tab) {

                if (obj->objectName() == "txtScriptEditor") {
                    return false;
                }

                // Campi del modulo equazioni: stessi due regimi del filtro
                // desktop -- fermo aspettano il Run, in moto l'Invio applica al
                // volo. Il ramo generico qui sotto chiamerebbe comunque
                // commitUiFieldsDuringMotion, ma senza distinguere i due casi.
                if (isDeferredEquationField(obj->objectName())) {
                    if (QWidget* w = qobject_cast<QWidget*>(obj)) w->clearFocus();
                    if (MainWindow* mainWin = qobject_cast<MainWindow*>(parent())) {
                        if (mainWin->isEquationModuleMoving())
                            mainWin->commitUiFieldsDuringMotion();
                    }
                    QGuiApplication::inputMethod()->hide();
                    return true;
                }

                // Campi path camera: avvia / ricompila / ferma secondo lo stato,
                // come nel filtro desktop.
                if (isPathEquationField(obj->objectName())) {
                    if (QWidget* w = qobject_cast<QWidget*>(obj)) w->clearFocus();
                    if (MainWindow* mainWin = qobject_cast<MainWindow*>(parent())) {
                        mainWin->commitPathFieldOnEnter(obj->objectName());
                    }
                    QGuiApplication::inputMethod()->hide();
                    return true;
                }

                // Limiti U/V/W: stessa regola e stesso ramo del filtro desktop
                // (validazione + registrazione del dominio; a schermo subito
                // solo se la superficie e' in moto). Senza questo ramo
                // cadrebbero nel generico qui sotto (commitUiFieldsDuringMotion),
                // che e' un'altra cosa.
                const QString limitName = obj->objectName();
                if (limitName == "uMinEdit" || limitName == "uMaxEdit" ||
                    limitName == "vMinEdit" || limitName == "vMaxEdit" ||
                    limitName == "wMinEdit" || limitName == "wMaxEdit") {
                    if (QWidget* w = qobject_cast<QWidget*>(obj)) w->clearFocus();
                    if (MainWindow* mainWin = qobject_cast<MainWindow*>(parent())) {
                        mainWin->commitLimitFieldOnEnter(limitName);
                    }
                    QGuiApplication::inputMethod()->hide();
                    return true;
                }

                // Limiti PER-MESH: stesso ramo del filtro desktop.
                if (limitName == "meshUMinEdit" || limitName == "meshUMaxEdit" ||
                    limitName == "meshVMinEdit" || limitName == "meshVMaxEdit") {
                    if (QWidget* w = qobject_cast<QWidget*>(obj)) w->clearFocus();
                    if (MainWindow* mainWin = qobject_cast<MainWindow*>(parent())) {
                        mainWin->commitMeshLimitFieldOnEnter(limitName);
                    }
                    QGuiApplication::inputMethod()->hide();
                    return true;
                }

                if (QWidget* w = qobject_cast<QWidget*>(obj)) {
                    MainWindow* mainWin = qobject_cast<MainWindow*>(parent());

                    // UN SOLO COMMIT PER INVIO, come nel filtro desktop:
                    // clearFocus() emette editingFinished, che per i campi
                    // costante commtta gia'. Vedi commitOnEnterCount.
                    const quint64 before = mainWin ? mainWin->commitOnEnterCount() : 0;
                    w->clearFocus();

                    // Notifica a MainWindow di applicare i campi modificati
                    if (mainWin && mainWin->commitOnEnterCount() == before) {
                        mainWin->commitUiFieldsDuringMotion();
                    }

                    QGuiApplication::inputMethod()->hide();
                    return true;
                }
            }
        }

        return QObject::eventFilter(obj, event);
    }

#if defined(Q_OS_ANDROID)
private:
    // Menu di modifica completo, lo stesso che Qt mostra su desktop.
    // selectWordUnderFinger: su long-press emula il comportamento nativo
    // selezionando la parola sotto il dito (se non c'è già una selezione).
    void showEditMenu(QObject* obj, const QPoint& globalPos, bool selectWordUnderFinger) {
        QMenu* menu = nullptr;

        if (auto* pte = qobject_cast<QPlainTextEdit*>(obj)) {
            if (selectWordUnderFinger && !pte->textCursor().hasSelection()) {
                QTextCursor c = pte->cursorForPosition(pte->viewport()->mapFromGlobal(globalPos));
                c.select(QTextCursor::WordUnderCursor);
                if (c.hasSelection()) pte->setTextCursor(c);
            }
            menu = pte->createStandardContextMenu();
        } else if (auto* le = qobject_cast<QLineEdit*>(obj)) {
            if (selectWordUnderFinger && !le->hasSelectedText()) {
                const QString t = le->text();
                const int pos = le->cursorPositionAt(le->mapFromGlobal(globalPos));
                auto isWordChar = [](QChar ch){ return ch.isLetterOrNumber() || ch == '_'; };
                int s = pos, e = pos;
                while (s > 0 && isWordChar(t.at(s - 1))) --s;
                while (e < t.size() && isWordChar(t.at(e))) ++e;
                if (e > s) le->setSelection(s, e - s);
            }
            menu = le->createStandardContextMenu();
        }

        if (!menu) return;
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(globalPos);
    }
#endif
};

#if defined(Q_OS_IOS)
// ==========================================
// Filtro iOS: tap-vs-scroll sugli editor
// ==========================================
// Su iOS la tastiera si apre al focus-in, e il focus arriva già col PRESS:
// bastava toccare un QPlainTextEdit per iniziare uno scroll e la tastiera
// compariva senza alcuna intenzione di digitare. Il press su un editor NON
// focalizzato viene trattenuto: se il dito si muove è uno scroll (muoviamo
// noi il contenuto dell'editor, o la QScrollArea che lo contiene se l'editor
// non ha nulla da scrollare), se resta fermo è un tap e il focus — quindi la
// tastiera — scatta solo al rilascio. Un editor già focalizzato mantiene il
// comportamento nativo (posizionamento cursore, selezione).
class TapVsScrollFilter : public QObject {
public:
    TapVsScrollFilter(QObject* parent = nullptr) : QObject(parent) {}

protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        auto* viewport = qobject_cast<QWidget*>(obj);
        auto* editor = viewport ? qobject_cast<QPlainTextEdit*>(viewport->parentWidget()) : nullptr;
        if (!editor)
            return QObject::eventFilter(obj, event);

        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() != Qt::LeftButton || editor->hasFocus())
                return false;
            m_pending = true;
            m_dragging = false;
            m_pressPos = m_lastPos = me->globalPosition();
            m_scrollRemainder = 0.0;
            return true; // niente focus al press = niente tastiera
        }
        case QEvent::MouseMove: {
            auto* me = static_cast<QMouseEvent*>(event);
            if (!m_pending)
                return false;
            const QPointF pos = me->globalPosition();
            if (!m_dragging && (pos - m_pressPos).manhattanLength() > kDragThreshold)
                m_dragging = true;
            if (m_dragging) {
                scrollBy(editor, m_lastPos.y() - pos.y());
                m_lastPos = pos;
            }
            return true;
        }
        case QEvent::MouseButtonRelease: {
            if (!m_pending) return false;
            m_pending = false;
            if (!m_dragging) {
                auto* me = static_cast<QMouseEvent*>(event);
                editor->setFocus(Qt::MouseFocusReason);
                editor->setTextCursor(editor->cursorForPosition(me->position().toPoint()));
            }
            return true;
        }
        default:
            return QObject::eventFilter(obj, event);
        }
    }

private:
    static constexpr int kDragThreshold = 12; // px: oltre = scroll, sotto = tap

    void scrollBy(QPlainTextEdit* editor, qreal dyPixels) {
        QScrollBar* bar = editor->verticalScrollBar();
        if (bar && bar->maximum() > bar->minimum()) {
            // La scrollbar verticale del QPlainTextEdit lavora in righe, non in pixel
            m_scrollRemainder += dyPixels / qMax(1, editor->fontMetrics().lineSpacing());
            const int lines = int(m_scrollRemainder);
            if (lines != 0) {
                bar->setValue(bar->value() + lines);
                m_scrollRemainder -= lines;
            }
            return;
        }
        for (QWidget* w = editor->parentWidget(); w; w = w->parentWidget()) {
            if (auto* area = qobject_cast<QAbstractScrollArea*>(w)) {
                QScrollBar* outer = area->verticalScrollBar();
                if (outer && outer->maximum() > outer->minimum()) {
                    outer->setValue(outer->value() + qRound(dyPixels));
                    return;
                }
            }
        }
    }

    bool m_pending = false;
    bool m_dragging = false;
    QPointF m_pressPos;
    QPointF m_lastPos;
    qreal m_scrollRemainder = 0.0;
};
#endif // Q_OS_IOS

class EnterApplyFilter : public QObject {
public:
    std::function<void()> onEnter;
    EnterApplyFilter(QObject* parent = nullptr) : QObject(parent) {}
protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
                if (!(keyEvent->modifiers() & Qt::ShiftModifier)) {
                    if (onEnter) onEnter();
                    return true;
                }
            }
        }
        return QObject::eventFilter(obj, event);
    }
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    // =========================================================================
    // 1. CORE INITIALIZATION
    // =========================================================================
    ui->setupUi(this);
    bindEquationFields();   // prima di ogni altro connect sui campi delle equazioni
    bindConstantFields();   // ...e su quelli delle costanti e lo slider Steps
    bindLineFields();       // ...e su limiti e path

    // FULL IMMERSION
    if (this->centralWidget() && this->centralWidget()->layout()) {
        this->centralWidget()->layout()->setContentsMargins(0, 0, 0, 0);
        this->centralWidget()->layout()->setSpacing(0);
    }

    //QSettings().clear(); // Primo avvio dell'applicazione

    setWindowTitle("Surface Explorer");
    setAttribute(Qt::WA_AcceptTouchEvents);

    m_blockTextureGen = false;
    m_currentTexturePresetPath = "";
    m_surfaceTextureState = false;

#if defined(Q_OS_ANDROID)
    // 1. Controlla prima l'API Level (Deve essere >= 30 per questa funzione)
    if (QNativeInterface::QAndroidApplication::sdkVersion() >= 30) {

        // 2. Ora è sicuro chiamare isExternalStorageManager
        bool isStorageManager = QJniObject::callStaticMethod<jboolean>("android/os/Environment", "isExternalStorageManager");

        if (!isStorageManager) {
            QJniObject intent("android/content/Intent", "(Ljava/lang/String;)V",
                              QJniObject::fromString("android.settings.MANAGE_APP_ALL_FILES_ACCESS_PERMISSION").object<jstring>());

            // 3. Recupera dinamicamente il VERO nome del pacchetto dell'app, niente hardcoding!
            QJniObject context = QNativeInterface::QAndroidApplication::context();
            QJniObject packageName = context.callObjectMethod("getPackageName", "()Ljava/lang/String;");
            QString uriString = "package:" + packageName.toString();

            QJniObject uri = QJniObject::callStaticObjectMethod("android/net/Uri", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
                                                                QJniObject::fromString(uriString).object<jstring>());

            intent.callObjectMethod("setData", "(Landroid/net/Uri;)Landroid/content/Intent;", uri.object());
            intent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;", 0x10000000); // FLAG_ACTIVITY_NEW_TASK

            if (context.isValid()) {
                context.callMethod<void>("startActivity", "(Landroid/content/Intent;)V", intent.object());
            }
        }
    }

    connect(QGuiApplication::inputMethod(), &QInputMethod::keyboardRectangleChanged,
            this, [this]() {

        QRectF kbdRect = QGuiApplication::inputMethod()->keyboardRectangle();
        int kbdHeight = kbdRect.height();

        // 1. Assicuriamoci che il central widget (e la vista 3D) rimangano
        // intatti a schermo intero senza innescare la Status Bar.
        if (ui->centralwidget) {
            ui->centralwidget->setContentsMargins(0, 0, 0, 0);
        }

        // 2. Applichiamo la compensazione SOLO ai pannelli laterali
        QList<QDockWidget*> docks = {
            ui->dockEquations, ui->dockScripts, ui->dockRenders,
            ui->dock3D, ui->dock4D, ui->dockSurfaces
        };

        for (QDockWidget* dock : docks) {
            if (!dock->widget()) continue;
            // Il margine destro e' quello che tiene i controlli fuori dalla
            // scrollbar (vedi addScrollToDock): tocchiamo SOLO il bottom.
            QMargins m = dock->widget()->contentsMargins();
            if (kbdHeight > 0) {
                // Comprime solo il dock realmente visibile (quello su cui si digita).
                if (!dock->isVisible()) continue;
                m.setBottom(kbdHeight);
                dock->widget()->setContentsMargins(m);

                // Trova il campo di testo su cui stiamo digitando
                QWidget* fw = this->focusWidget();
                if (fw) {
                    // Un piccolo timer permette al layout di aggiornarsi prima di scorrere
                    QTimer::singleShot(100, fw, [fw]() {
                        QWidget* p = fw->parentWidget();
                        while (p) {
                            if (QScrollArea* sa = qobject_cast<QScrollArea*>(p)) {
                                // Forza lo scorrimento verso il campo, lasciando 50px di margine
                                sa->ensureWidgetVisible(fw, 0, 50);
                                break;
                            }
                            p = p->parentWidget();
                        }
                    });
                }
            } else {
                // Tastiera chiusa: ripristina SEMPRE il margine, anche se il dock non
                // e' visibile in questo istante (vedi ramo iOS): condizionarlo a
                // isVisible() lascia il dock dimezzato dopo un MobileSaveDialog.
                m.setBottom(0);
                dock->widget()->setContentsMargins(m);
            }
        }
    });

#endif

    // NOTA (2026-07-01): su iPhone NON comprimiamo i dock all'apparizione della
    // tastiera. Si era ipotizzato che il dock allungato dipendesse dalla tastiera,
    // ma iOS porta gia' i campi in-dock sopra la tastiera da solo e il dock si rompe
    // anche senza tastiera. Bug del dock ancora IRRISOLTO: vedi DOCK_BUG_REPORT.md.

    // --- SBLOCCO DEI CAMPI COSTANTI (Permette lettere, 'pi', formule) ---
    ui->lineA->setValidator(nullptr);
    ui->lineB->setValidator(nullptr);
    ui->lineC->setValidator(nullptr);
    ui->lineD->setValidator(nullptr);
    ui->lineE->setValidator(nullptr);
    ui->lineF->setValidator(nullptr);
    ui->lineS->setValidator(nullptr);

    // =========================================================================
    // 2. STYLING & FONTS
    // =========================================================================
    UiStyleManager::applyPlatformStyle(this);
    UiStyleManager::applyDarkTheme(this);

    QList<QWidget*> mainInputFields = {
        ui->lineX, ui->lineY, ui->lineZ, ui->lineP,
        ui->lineX_P3D, ui->lineY_P3D, ui->lineZ_P3D, ui->lineR_P3D,
        ui->lineX_P, ui->lineY_P, ui->lineZ_P, ui->lineP_P,
        ui->lineAlpha_P, ui->lineBeta_P, ui->lineGamma_P
    };
    UiStyleManager::applyInputFieldsStyle(mainInputFields);

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    // ==========================================
    // 1. COMPATTAZIONE INTERFACCIA
    // ==========================================
    QList<QWidget*> dockContents = {
        ui->dockEquations->widget(),
        ui->dockRenders->widget(),
        //ui->dockScripts->widget(),
        ui->dock3D->widget(),
        ui->dock4D->widget(),
        ui->dockSurfaces->widget()
    };

    // CHIAMATA UNICA CENTRALIZZATA
    UiStyleManager::compactForMobile(dockContents);

    // ==========================================
    // FONT E LENTE IOS (VERSIONE CORRETTA VIA LAYOUT)
    // ==========================================
    QString mobileInputStyle = "QLineEdit, QPlainTextEdit, QTextEdit { font-size: 14px; }";

    if (ui->dockEquations->widget()) {
        QString currentEqStyle = ui->dockEquations->widget()->styleSheet();
        ui->dockEquations->widget()->setStyleSheet(currentEqStyle + " " + mobileInputStyle);
    }

    if (ui->dockScripts->widget()) {
        QWidget* originalWidget = ui->dockScripts->widget();

        // Applichiamo la dimensione del font pulita al widget originale
        QString currentScriptStyle = originalWidget->styleSheet();
        originalWidget->setStyleSheet(currentScriptStyle + " " + mobileInputStyle);

        // --- SOLUZIONE: INIEZIONE DI UNA QSCROLLAREA PER L'INTERO DOCK ---
        // Creiamo la scroll area che conterrà l'intero blocco del pannello script
        QScrollArea* dockScrollArea = new QScrollArea(ui->dockScripts);
        dockScrollArea->setWidgetResizable(true);
        dockScrollArea->setFrameShape(QFrame::NoFrame); // Rimuove bordi interni antiestetici di Qt
        dockScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        dockScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

        // FORZATURA ALTEZZA: Garantisce che l'editor abbia sempre uno spazio di digitazione
        // umano (es. 300px) e non venga mai schiacciato a zero pixel dalla tastiera.
        ui->txtScriptEditor->setMinimumHeight(300);

        // Niente scroll cinetico a dito: sui pannelli con controlli interattivi
        // il TouchGesture selezionava/attivava i figli durante il trascinamento.
        // Come gli altri dock, si scrolla solo con la scroll bar.

        // Sostituiamo il widget principale del dock inserendovi la ScrollArea,
        // e mettiamo il vecchio widget all'interno della scroll area.
        dockScrollArea->setWidget(originalWidget);
        ui->dockScripts->setWidget(dockScrollArea);

    }


    // ==========================================
    // 2. GESTIONE TASTIERA E NAVIGAZIONE CURSORE
    // ==========================================
    MobileInputFilter* mobileFilter = new MobileInputFilter(this);
    QList<QWidget*> allTextInputs;

    // Riempiamo la lista iterando sui risultati per permettere il cast a QWidget*
    for(auto* textEdit : this->findChildren<QPlainTextEdit*>()) {
        allTextInputs.append(textEdit);
    }
    for(auto* lineEdit : this->findChildren<QLineEdit*>()) {
        allTextInputs.append(lineEdit);
    }

    for (QWidget* input : allTextInputs) {
        input->installEventFilter(mobileFilter);

#if defined(Q_OS_ANDROID)
        // Long-press -> menu di modifica completo (vedi MobileInputFilter):
        // il gesto parte anche se il tocco avviene sul viewport dei QPlainTextEdit,
        // perché il gesture manager risale la gerarchia fino al widget che ha il grab.
        input->grabGesture(Qt::TapAndHoldGesture);
#endif

        // Hint tastiera: niente predizione né maiuscole automatiche.
        // MultiLine SOLO per lo script editor: sugli altri campi la tastiera
        // mostra il tasto "Fatto/Vai" che innesca la chiusura via Invio.
        Qt::InputMethodHints hints = Qt::ImhSensitiveData | Qt::ImhNoPredictiveText | Qt::ImhNoAutoUppercase;
        if (input->objectName() == "txtScriptEditor")
            hints |= Qt::ImhMultiLine;
        input->setInputMethodHints(hints);
    }

#if defined(Q_OS_IOS)
    // Tap-vs-scroll (vedi TapVsScrollFilter): va installato sul viewport,
    // è lì che arrivano i mouse event dei QPlainTextEdit.
    // NoFocus è essenziale: su iOS setFocusOnTouchRelease=true e il focus da
    // click viene assegnato al RILASCIO a livello di QWidgetWindow, PRIMA che
    // l'evento raggiunga i filtri sul viewport e senza controllare se il dito
    // ha scrollato. Con NoFocus quel percorso è neutralizzato e il focus lo dà
    // solo il filtro (setFocus programmatico ignora la policy) sul tap pulito.
    auto* tapVsScroll = new TapVsScrollFilter(this);
    for (auto* textEdit : this->findChildren<QPlainTextEdit*>()) {
        textEdit->setFocusPolicy(Qt::NoFocus);
        textEdit->viewport()->installEventFilter(tapVsScroll);
        // Trigger dell'autoscroll di selezione: scatta a ogni variazione
        // della selezione qualunque sia il canale con cui il plugin la
        // applica (il drag dei pallini non genera eventi mouse e il canale
        // IM non è un flusso continuo, provato su device).
        connect(textEdit, &QPlainTextEdit::selectionChanged, this, [textEdit]() {
            SelectionAutoScroller::instance()->onSelectionEvent(textEdit);
        });
    }

    // Se la tastiera sparisce mentre c'è una selezione attiva (trascinando
    // il pallino dentro l'area della tastiera scatta il gesto del plugin,
    // che fa resign E azzera il focus: pallini e menu muoiono), la selezione
    // superstite resterebbe inutilizzabile — qualsiasi tap per riaprire il
    // menu la cancella. Ripresentiamo quindi noi il menu: sul widget ancora
    // focalizzato se c'è, altrimenti sull'ultimo editor di cui i pallini
    // stavano modificando la selezione (il focus a quel punto è già nullo).
    // Cut/Copy/Paste agiscono sul widget e funzionano anche senza focus;
    // UIEditMenuInteraction vive anche senza tastiera.
    connect(QGuiApplication::inputMethod(), &QInputMethod::visibleChanged, this, []() {
        if (QGuiApplication::inputMethod()->isVisible())
            return;
        auto hasSelection = [](QWidget* w) {
            if (auto* pte = qobject_cast<QPlainTextEdit*>(w))
                return pte->textCursor().hasSelection();
            if (auto* le = qobject_cast<QLineEdit*>(w))
                return le->hasSelectedText();
            return false;
        };
        QWidget* editor = QApplication::focusWidget();
        if (!hasSelection(editor)) {
            editor = s_lastSelectionEditor.data();
            if (!hasSelection(editor))
                return;
        }
        // Campi che hanno dichiarato "niente menu di modifica" (proprieta'
        // noEditMenu): questo connect e' GLOBALE all'app e ripresenterebbe il
        // menu anche sui campi numerici dei dialoghi, dove il NoEditMenuFilter
        // blocca il QContextMenuEvent ma non questo percorso — che non passa da
        // li'. E' la via per cui il menu ricompariva sui campi del recorder.
        if (editor && editor->property("noEditMenu").toBool())
            return;
        const QRect r = editor->inputMethodQuery(Qt::ImCursorRectangle).toRect();
        const QPoint gp = editor->mapToGlobal(r.center());
        QTimer::singleShot(0, editor, [editor, gp]() { iosPresentEditMenu(editor, gp); });
    });
#endif

    UiStyleManager::setupRaymarchTabMobile(ui->dockEquations->widget());
#endif

    // =========================================================================
    // 3. DOCK WIDGETS & TAB LAYOUT
    // =========================================================================
    // --- RIORDINO SCHEDE LIBRARY (FIX DESIGNER) ---
    ui->tabWidget->clear();
    ui->tabWidget->addTab(ui->Surface, "Surfaces");
    ui->tabWidget->addTab(ui->Texture, "Textures");
    ui->tabWidget->addTab(ui->Sounds, "Sounds");
    ui->tabWidget->addTab(ui->Motions, "Records");
    ui->tabWidget->setCurrentIndex(0);
    // Niente frecce di scorrimento sui tab Library: il cambio si fa cliccando
    // la linguetta.
    if (ui->tabWidget->tabBar())
        ui->tabWidget->tabBar()->setUsesScrollButtons(false);
    // Tab a piena larghezza: 4 tab su un dock bloccato a 400px. min-width fissa
    // (come il dockEquations, che funziona) tarata sui 400: ~76px/tab riempie
    // la barra. Col CSS globale setExpanding e' ignorato, la min-width no.
    ui->tabWidget->setStyleSheet(
        "QTabBar::tab { min-width: 76px; padding: 5px 10px; }");

    UiStyleManager::setupDockScroll(ui->dockEquations, true);
    UiStyleManager::setupDockScroll(ui->dockRenders, true);
    UiStyleManager::setupDockScroll(ui->dock3D, true);
    UiStyleManager::setupDockScroll(ui->dock4D, true);
    UiStyleManager::setupDockScroll(ui->dockScripts, true);
    UiStyleManager::setupDockScroll(ui->dockSurfaces, true);

    if (ui->dockEquations) UiStyleManager::addScrollToDock(ui->dockEquations);
    if (ui->dockRenders) UiStyleManager::addScrollToDock(ui->dockRenders);
    if (ui->dock3D) UiStyleManager::addScrollToDock(ui->dock3D);
    if (ui->dock4D) UiStyleManager::addScrollToDock(ui->dock4D);
    if (ui->dockScripts) UiStyleManager::addScrollToDock(ui->dockScripts);

    // --- BLOCCO SPOSTAMENTO DOCK ---
    auto lockDock = [](QDockWidget* dock) {
        dock->setFeatures(QDockWidget::DockWidgetClosable);
    };

    lockDock(ui->dockEquations);
    lockDock(ui->dockRenders);
    lockDock(ui->dock3D);
    lockDock(ui->dock4D);
    lockDock(ui->dockScripts);
    lockDock(ui->dockSurfaces);
    // --------------------------------

    // Diciamo a Qt di mettere TUTTI i pannelli a DESTRA (RightDockWidgetArea)
    addDockWidget(Qt::RightDockWidgetArea, ui->dockEquations);
    addDockWidget(Qt::RightDockWidgetArea, ui->dockRenders);
    addDockWidget(Qt::RightDockWidgetArea, ui->dock3D);
    addDockWidget(Qt::RightDockWidgetArea, ui->dock4D);
    addDockWidget(Qt::RightDockWidgetArea, ui->dockScripts);
    addDockWidget(Qt::RightDockWidgetArea, ui->dockSurfaces);

    // IMPILIAMO i pannelli uno sopra l'altro (Tabify)
    tabifyDockWidget(ui->dockEquations, ui->dockRenders);
    tabifyDockWidget(ui->dockRenders, ui->dock3D);
    tabifyDockWidget(ui->dock3D, ui->dock4D);
    tabifyDockWidget(ui->dock4D, ui->dockScripts);
    tabifyDockWidget(ui->dockScripts, ui->dockSurfaces);

    // INVISIBILITÀ INIZIALE
    ui->dockEquations->close();
    ui->dockRenders->close();
    ui->dock3D->close();
    ui->dock4D->close();
    ui->dockScripts->close();
    ui->dockSurfaces->close();

    // =========================================================================
    // 4. GLOBAL ACTIONS & MACOS MENUS
    // =========================================================================
    connect(ui->actionSave, &QAction::triggered, this, [this](){ saveSurfaceToFile(); });
    connect(ui->actionDelete, &QAction::triggered, this, &MainWindow::deleteSelectedExample);
    // Il comando e' GLOBALE: non dipende dalla linguetta aperta. Qui si ricavava
    // un LibraryType dal tab corrente per passarlo a onAddRepositoryClicked, che
    // lo ignorava -- vedi la voce "Change Library Folder..." in
    // librarymenucontroller, dove c'era lo stesso calcolo inutile.
    connect(ui->actionSelectFolder, &QAction::triggered, this, [this](){
        onAddRepositoryClicked();
    });

#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    ui->actionSelectFolder->setVisible(false);
#endif

    connect(ui->actionCut, &QAction::triggered, this, [this](){ performCut(nullptr); });
    connect(ui->actionPaste, &QAction::triggered, this, [this](){
        if (!m_cutFilePaths.isEmpty()) onPasteExample();
        else if (!m_cutTexturePaths.isEmpty()) onPasteTexture();
    });

    connect(ui->actionUndoDelete, &QAction::triggered, this, &MainWindow::onUndoDelete);

    // MACOS MENU
    ui->actionQuit->setMenuRole(QAction::QuitRole);
    connect(ui->actionQuit, &QAction::triggered, this, &MainWindow::close);

    ui->actionAbout->setMenuRole(QAction::AboutRole);
    connect(ui->actionAbout, &QAction::triggered, this, [this](){
        // La versione arriva dal build (APP_VERSION, da project() nel
        // CMakeLists): qui NON va mai scritta a mano, o resta indietro a ogni
        // bump come e' successo fino alla 1.1.
        QMessageBox::about(this, "About Surface Explorer",
                           "<b>Surface Explorer</b><br>"
                           "Version " APP_VERSION "<br><br>"
                           "Developed by: <b>Gaetano Moschetti</b><br>"
                           "License: <b>GNU GPL v3</b><br>"
                           // L'URL e' scritto per esteso anche come testo del link:
                           // se il message box non apre i link esterni resta
                           // comunque leggibile e selezionabile.
                           "Website: <a href=\"https://dioscorid-design.github.io/SurfaceExplorer/\">"
                           "dioscorid-design.github.io/SurfaceExplorer</a><br><br>"
                           "This is free software: you are free to change and redistribute it "
                           "under the terms of the GNU General Public License as published by "
                           "the Free Software Foundation.<br><br>"
                           "<i>Exploring the beauty of four-dimensional geometry.</i>");
    });

    ui->actionDocumentation->setMenuRole(QAction::NoRole);
    connect(ui->actionDocumentation, &QAction::triggered, this, [this](){

        QDialog* docDialog = new QDialog(this);
        docDialog->setWindowTitle("Documentation");

        QVBoxLayout* layout = new QVBoxLayout(docDialog);

        QTextBrowser* browser = new QTextBrowser(docDialog);
        browser->setOpenExternalLinks(true);
        // Il motore rich text di Qt ignora il grosso del CSS del file html:
        // la dimensione del testo va imposta qui, PRIMA di caricare la pagina.
        // div copre i riquadri "note", td/th le tabelle; i titoli restano
        // leggermente più grandi del testo per mantenere la gerarchia.
        browser->document()->setDefaultStyleSheet(
            "p, li, div, td, th { font-size: 17px; }"
            " h2 { font-size: 22px; }"
            " h3 { font-size: 19px; }"
            " h4 { font-size: 18px; }");
        browser->setSource(QUrl("qrc:/docs/documentation.html"));

        // --- RICERCA NELL'INTERO MANUALE ---
        // Campo SEMPRE VISIBILE in cima al dialogo. Il manuale e' spezzato in
        // 17 pagine, quindi il Ctrl+F del browser (che vede solo quella aperta)
        // non basta: qui si cerca in tutte e si presenta l'elenco delle pagine
        // che contengono il termine.
        QLineEdit* searchEdit = new QLineEdit(docDialog);
        searchEdit->setPlaceholderText("Search the manual\xE2\x80\xA6");
        searchEdit->setClearButtonEnabled(true);
#ifdef Q_OS_IOS
        // Stesse guardie degli altri campi su iOS: senza, il long-press apre il
        // menu di modifica e poi blocca la digitazione.
        searchEdit->setInputMethodHints(searchEdit->inputMethodHints() | Qt::ImhNoEditMenu);
        searchEdit->setProperty("noEditMenu", true);
#endif

        // I risultati stanno in un widget SEPARATO che si alterna al browser,
        // invece di essere disegnati dentro la pagina: cosi' la cronologia del
        // QTextBrowser (e quindi il tasto Back) non si sporca di pagine
        // sintetiche che non esistono nel manuale.
        // Pagina da cui e' partita la ricerca: uscendo dai risultati si torna
        // LI', non sulla pagina che un risultato aveva aperto. Senza, il
        // browser restava dov'era e serviva un secondo Back per l'indice --
        // che per l'utente e' un passo indietro di troppo, per giunta verso una
        // pagina appena abbandonata.
        // QString* e non QString: le lambda che lo usano sono copiate per
        // valore e devono condividere lo stesso stato.
        QString* originPage = new QString(QStringLiteral("qrc:/docs/documentation.html"));
        // Pagina aperta DA un risultato: serve a riconoscere se ci si trova
        // ancora li' o se da li' si e' seguito un altro link. Nel secondo caso
        // il passo indietro e' quel link, non l'elenco -- saltarlo perderebbe
        // un passo di navigazione che l'utente ha fatto.
        QString* resultPage = new QString();
        QObject::connect(docDialog, &QObject::destroyed, [originPage, resultPage]() {
            delete originPage; delete resultPage;
        });

        QListWidget* results = new QListWidget(docDialog);
        results->setWordWrap(true);
        // Una riga separa una voce dall'altra: ogni risultato occupa due righe
        // (titolo + contesto) e senza uno stacco visivo il contesto di una voce
        // e il titolo della successiva si leggono come un blocco solo.
        // Il bordo va sull'item, non su voci finte inserite nella lista: quelle
        // sarebbero navigabili con le frecce e romperebbero il conteggio.
        // NB: Qt ignora in silenzio i selettori :first / :last sugli item delle
        // viste (verificato: con tre voci disegna tre bordi), quindi l'ultima
        // riga chiude l'elenco. Cade al fondo dell'ultima voce, non nel vuoto
        // sotto, e fa da chiusura: nessun ritocco necessario.
        results->setStyleSheet(
            "QListWidget { border: none; }"
            " QListWidget::item {"
            "   border-bottom: 1px solid #3a3a3a;"
            "   padding: 10px 6px;"
            " }"
            " QListWidget::item:selected { background-color: #094771; color: white; }");
        results->hide();

        QPushButton* closeBtn = new QPushButton("Close", docDialog);

        // 1. Decidiamo cosa fa il click: chiude la finestra o torna indietro?
        connect(closeBtn, &QPushButton::clicked, docDialog,
                [browser, docDialog, closeBtn, results, searchEdit, originPage, resultPage]() {
            if (closeBtn->text() != "Back") {
                docDialog->accept();     // Chiude la finestra normalmente
                return;
            }
            // Elenco dei risultati a schermo: si torna al manuale svuotando la
            // ricerca. Si agisce SUBITO invece di aspettare che il textChanged
            // faccia scattare il debounce (200 ms): con lo svuotamento affidato
            // al timer il click restava senza effetto visibile per un attimo, e
            // un secondo click nel frattempo cadeva in un ramo sbagliato.
            if (results->isVisible()) {
                searchEdit->clear();
                results->hide();
                browser->show();
                // Si RITORNA alla pagina da cui la ricerca era partita. Senza,
                // il browser restava sulla pagina aperta da un risultato e
                // serviva un secondo Back per uscirne -- verso una pagina che
                // l'utente aveva appena lasciato.
                //
                // Con backward() e non setSource(): il primo CONSUMA la
                // cronologia, il secondo ci aggiunge una voce. Usando setSource
                // il Back seguente ripercorreva le pagine viste durante la
                // ricerca, riportando di nuovo su quella del risultato -- lo
                // stesso giro di troppo, spostato un passo piu' in la'.
                // Il ciclo ha un tetto: una cronologia inattesa non deve poter
                // bloccare il dialogo in un giro infinito.
                for (int guard = 0; guard < 32; ++guard) {
                    if (browser->source().toString() == *originPage) break;
                    if (!browser->isBackwardAvailable()) break;
                    browser->backward();
                }
                const bool isIndex = browser->source().toString()
                                        .endsWith("documentation.html", Qt::CaseInsensitive);
                closeBtn->setText(isIndex ? "Close" : "Back");
                return;
            }
            // SI E' ANCORA sulla pagina aperta dal risultato: il passo indietro
            // naturale e' l'elenco, non la pagina precedente della cronologia --
            // che qui sarebbe quella da cui si era partiti prima di cercare.
            // Il confronto con resultPage e' necessario: senza, bastava che una
            // ricerca fosse attiva e ogni Back saltava all'elenco anche dopo
            // aver seguito altri link da quella pagina, perdendoli.
            if (!searchEdit->text().trimmed().isEmpty() && results->count() > 0
                    && browser->source().toString() == *resultPage) {
                browser->hide();
                results->show();
                return;
            }
            browser->backward();         // Torna alla pagina precedente della cronologia
        });

        // 2. Cambiamo automaticamente il testo del bottone leggendo la pagina corrente.
        // La regola e' posizionale, non un elenco di pagine: l'INDICE
        // (documentation.html) e' l'unica pagina senza un "indietro" possibile,
        // quindi li' il tasto chiude; ovunque altro torna alla pagina precedente.
        // Con l'elenco per nome ogni capitolo aggiunto al manuale nasceva con
        // "Close" e usciva dalla finestra invece di tornare all'indice.
        connect(browser, &QTextBrowser::sourceChanged, docDialog,
                [closeBtn, searchEdit](const QUrl &src) {
            const QString page = src.toString();
            const bool isIndex = page.endsWith("documentation.html", Qt::CaseInsensitive);
            // Con una ricerca in corso l'indice NON e' un vicolo cieco: da li'
            // si torna comunque all'elenco dei risultati, quindi il tasto resta
            // "Back" anche sulla pagina che di norma chiude.
            const bool searching = !searchEdit->text().trimmed().isEmpty();
            closeBtn->setText((isIndex && !searching) ? "Close" : "Back");
        });

        // 3. La ricerca gira mentre si digita, ma con un debounce: la scansione
        // e' su ~128 KB di testo e a ogni carattere sarebbe lavoro sprecato.
        QTimer* searchDebounce = new QTimer(docDialog);
        searchDebounce->setSingleShot(true);
        searchDebounce->setInterval(200);

        auto runSearch = [this, searchEdit, results, browser, closeBtn, originPage]() {
            const QString term = searchEdit->text().trimmed();

            // Si registra la pagina di partenza al PRIMO carattere, cioe' quando
            // i risultati non sono ancora in vista: dopo, il browser mostra gia'
            // una pagina aperta da un risultato e registrarla la scambierebbe
            // per l'origine.
            if (!results->isVisible() && !browser->source().toString().isEmpty())
                *originPage = browser->source().toString();

            // Campo svuotato: si torna al manuale, esattamente dov'era.
            // Il tasto va RIALLINEATO alla pagina in vista: restando su "Back"
            // dopo che i risultati sono spariti finiva nel ramo della cronologia,
            // e sull'indice -- che una cronologia non ce l'ha -- non faceva
            // nulla. Il tasto sembrava morto.
            if (term.isEmpty()) {
                results->hide();
                browser->show();
                const bool isIndex = browser->source().toString()
                                        .endsWith("documentation.html", Qt::CaseInsensitive);
                closeBtn->setText(isIndex ? "Close" : "Back");
                return;
            }

            results->clear();
            const QVector<DocHit> hits = searchDocumentation(term);

            if (hits.isEmpty()) {
                QListWidgetItem* none = new QListWidgetItem(
                    QString("No match for \u201C%1\u201D").arg(term), results);
                // Voce informativa, non cliccabile.
                none->setFlags(Qt::NoItemFlags);
            } else {
                for (const DocHit &h : hits) {
                    QListWidgetItem* item = new QListWidgetItem(
                        QString("%1  (%2)\n%3").arg(h.title).arg(h.count).arg(h.context),
                        results);
                    item->setData(Qt::UserRole, h.file);
                }
            }

            browser->hide();
            results->show();
            // Con i risultati a schermo il tasto deve riportare al manuale, non
            // chiudere la finestra: la regola posizionale sul solo indice non
            // copre questo stato, che pagina del manuale non e'.
            closeBtn->setText("Back");
        };

        connect(searchDebounce, &QTimer::timeout, docDialog, runSearch);
        connect(searchEdit, &QLineEdit::textChanged, docDialog,
                [searchDebounce](const QString&) { searchDebounce->start(); });
        // Invio: cerca subito, senza aspettare il debounce.
        connect(searchEdit, &QLineEdit::returnPressed, docDialog, [searchDebounce, runSearch]() {
            searchDebounce->stop();
            runSearch();
        });

        // 4. Click su un risultato: apre la pagina ed EVIDENZIA il termine.
        // L'evidenziazione passa da ExtraSelection e non da tag iniettati nel
        // documento: il motore rich text di Qt ignora gran parte del CSS (per
        // questo il font e' forzato con setDefaultStyleSheet qui sopra) e
        // manipolare l'HTML sporcherebbe la pagina caricata dal .qrc.
        connect(results, &QListWidget::itemClicked, docDialog,
                [browser, results, searchEdit, resultPage](QListWidgetItem* item) {
            if (!item) return;
            const QString file = item->data(Qt::UserRole).toString();
            if (file.isEmpty()) return;   // riga "nessun risultato"

            results->hide();
            browser->show();
            browser->setSource(QUrl(file));
            *resultPage = file;   // da qui il Back torna all'elenco

            const QString term = searchEdit->text().trimmed();
            if (term.isEmpty()) return;

            QList<QTextEdit::ExtraSelection> sels;
            QTextCursor cur(browser->document());
            QTextCharFormat fmt;
            fmt.setBackground(QColor("#7a5c00"));   // ocra: leggibile sul tema scuro
            fmt.setForeground(QColor("#ffffff"));
            while (true) {
                cur = browser->document()->find(term, cur, QTextDocument::FindCaseSensitively
                                                            & QTextDocument::FindFlags());
                if (cur.isNull()) break;
                QTextEdit::ExtraSelection sel;
                sel.cursor = cur;
                sel.format = fmt;
                sels.append(sel);
            }
            browser->setExtraSelections(sels);

            // Porta la vista sulla prima occorrenza, o la pagina si aprirebbe
            // in cima lasciando all'utente il compito di cercarla a occhio.
            if (!sels.isEmpty()) {
                browser->setTextCursor(sels.first().cursor);
                browser->ensureCursorVisible();
            }
        });

        layout->addWidget(searchEdit);
        layout->addWidget(browser);
        layout->addWidget(results);
        layout->addWidget(closeBtn);

        // Deleghiamo tutta l'estetica a UiStyleManager!
        UiStyleManager::setupDocumentationDialog(docDialog, layout, browser, closeBtn, searchEdit);

        // 1. Trova il bottone PRIMA di aprire il dialog e salva la sua posizione reale
        QPoint originalPos;
        QPushButton* menuBtn = this->findChild<QPushButton*>("mobileMenuBtn");
        if (menuBtn) {
            originalPos = menuBtn->pos();
        }

        // 2. Apri la documentazione (il codice si ferma qui finché non chiudi la finestra)
        docDialog->exec();

        // 3. Ripristina il bottone
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        // Passiamo 'originalPos' dentro la lambda per ricordarci dove stava
        QTimer::singleShot(50, this, [this, originalPos]() {
            QPushButton* btn = this->findChild<QPushButton*>("mobileMenuBtn");
            if (btn) {
                btn->raise();
                // Proviamo a rimetterlo dove era "nativamente"
                if (!originalPos.isNull()) {
                    btn->move(originalPos);
                } else {
                    // Fallback unificato: ora che la barra è nascosta, 10,10 è perfetto per tutti
                    btn->move(10, 10);
                }
                btn->show();
            }
        });
#endif
    });

    // =========================================================================
    // 5. STATUS BAR & BOTTOM CONTROLS
    // =========================================================================
    m_btnStart = new QPushButton("START", this);
    m_btnStart->setFlat(true);
    QFont fontBold = m_btnStart->font();
    fontBold.setBold(true);
    m_btnStart->setFont(fontBold);
    connect(m_btnStart, &QPushButton::clicked, this, &MainWindow::onStartClicked);

    // NEW sta nella status bar, fra START e RESET: e' la sua posizione naturale,
    // accanto agli altri comandi che agiscono sulla scena intera, e non
    // appartiene a nessuna delle due modalita' - come la scena vuota che
    // produce. Ci era gia' stato in passato, ma con undici tasti in fila gli
    // ultimi (Library per ultimo) finivano FUORI SCHERMO sui telefoni, e da li'
    // era passato prima in fondo al dock Equations, poi sopra le linguette.
    // Ora la barra scorre col dito su mobile (StatusBarScroller, in fondo a
    // questa sezione), quindi il vincolo che lo aveva esiliato non c'e' piu'.
    m_btnNew = new QPushButton("NEW", this);
    m_btnNew->setFlat(true);
    m_btnNew->setFont(fontBold);
    connect(m_btnNew, &QPushButton::clicked, this, &MainWindow::onNewSceneClicked);

    m_btnResetView = new QPushButton("RESET", this);
    m_btnResetView->setFlat(true);
    m_btnResetView->setFont(fontBold);
    connect(m_btnResetView, &QPushButton::clicked, this, &MainWindow::onResetViewClicked);

    m_btnProjection = new QPushButton("Perspective", this);
    m_btnProjection->setFlat(true);
    m_btnProjection->setFont(fontBold);
    connect(m_btnProjection, &QPushButton::clicked, this, &MainWindow::toggleProjection);

    m_btnRec = new QPushButton("REC", this);
    m_btnRec->setFlat(true);
    m_btnRec->setFont(fontBold);
    UiStyleManager::applyRecordButtonStyle(m_btnRec);
    m_videoRecorder = new VideoRecorder(this, this);
    connect(m_btnRec, &QPushButton::clicked, m_videoRecorder, &VideoRecorder::toggleRecord);

    m_statusLabel = new QLabel("", this);
    m_renderProgress = new QProgressBar(this);
    m_renderProgress->setRange(0, 100);
    m_renderProgress->setValue(0);
    m_renderProgress->setTextVisible(true);
    m_renderProgress->setVisible(false);
    m_renderProgress->setFixedWidth(150);

    ui->statusbar->addWidget(m_btnStart);
    ui->statusbar->addWidget(m_btnNew);
    ui->statusbar->addWidget(m_btnResetView);
    ui->statusbar->addWidget(m_btnProjection);
    ui->statusbar->addWidget(m_btnRec);
    ui->statusbar->addWidget(m_renderProgress);
    // m_statusLabel SENZA fattore di stretch: con stretch 1 rivendicava tutto
    // lo spazio residuo della barra, e i tasti dock (permanent widget, che il
    // layout non comprime) venivano spinti fuori schermo sui telefoni. Il
    // label non mostra mai testo: e' solo il bersaglio di clear() e
    // setStyleSheet(), quindi non gli serve larghezza propria.
    ui->statusbar->addWidget(m_statusLabel);

    // Lambda helper apertura dock
    auto closeAllDocks = [this](){
        ui->dockEquations->close();
        ui->dockRenders->close();
        ui->dock3D->close();
        ui->dock4D->close();
        ui->dockScripts->close();
        ui->dockSurfaces->close();
    };

    auto safeOpenDock = [this, closeAllDocks](QDockWidget* dock) {

#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
        static qint64 lastToggleTime = 0;
        qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
        if (currentTime - lastToggleTime < 400) return;
        lastToggleTime = currentTime;

        if (dock->isVisible()) {
            dock->close();
            return;
        }

        if (dock->isFloating()) dock->setFloating(false);

        // 1. CONGELIAMO LO SCHERMO (Elimina il flash visivo)
        this->setUpdatesEnabled(false);

        // 2. FORZIAMO LA LARGHEZZA (Evita il collasso a 0 pixel e il "millimetro" di splitter)
        dock->setFixedWidth(400);

        // 3. AGGANCIAMO E MOSTRIAMO
        this->addDockWidget(Qt::RightDockWidgetArea, dock);
        dock->show();
        dock->raise();

        // 4. CHIUDIAMO I VECCHI IN BACKGROUND
        if (dock != ui->dockEquations) ui->dockEquations->close();
        if (dock != ui->dockRenders) ui->dockRenders->close();
        if (dock != ui->dock3D) ui->dock3D->close();
        if (dock != ui->dock4D) ui->dock4D->close();
        if (dock != ui->dockScripts) ui->dockScripts->close();
        if (dock != ui->dockSurfaces) ui->dockSurfaces->close();

        // 5. SCONGELIAMO LO SCHERMO
        this->setUpdatesEnabled(true);

        QTimer::singleShot(100, dock, [this, dock]() {
            dock->setMinimumWidth(400);

            // dockEquations e dockSurfaces (Library) bloccati a 400: non serve
            // che si allarghino oltre. Cosi' i tab Library hanno larghezza nota.
            if (dock == ui->dockEquations || dock == ui->dockSurfaces) {
                dock->setMaximumWidth(400);
            } else {
                dock->setMaximumWidth(16777215);
            }

            // 6. SBLOCCHIAMO LA LARGHEZZA DOPO L'APERTURA
            QTimer::singleShot(100, dock, [this, dock]() {
                dock->setMinimumWidth(400);

                if (dock == ui->dockEquations || dock == ui->dockSurfaces) {
                    // Blocca l'espansione massima per Equations e Library
                    dock->setMaximumWidth(400);
                } else {
                    // Lascia gli altri liberi
                    dock->setMaximumWidth(16777215);
                }
            });
        });

#else
        // =========================================================
        // RAMO DESKTOP
        // =========================================================
        if (dock->isVisible()) {
            dock->close();
            return;
        }

        closeAllDocks();

        if (dock->isFloating()) dock->setFloating(false);
        this->addDockWidget(Qt::RightDockWidgetArea, dock);
        dock->show();
#endif
    };

    // Tasti Docks nella Statusbar
    QPushButton* btnEq = new QPushButton("Equations", this); btnEq->setFlat(true);
    connect(btnEq, &QPushButton::clicked, this, [this, safeOpenDock](){ safeOpenDock(ui->dockEquations); });
    ui->statusbar->addPermanentWidget(btnEq);

    QPushButton* btnRen = new QPushButton("Renderer", this); btnRen->setFlat(true);
    connect(btnRen, &QPushButton::clicked, this, [this, safeOpenDock](){ safeOpenDock(ui->dockRenders); });
    ui->statusbar->addPermanentWidget(btnRen);

    QPushButton* btn3D = new QPushButton("3D View", this); btn3D->setFlat(true);
    connect(btn3D, &QPushButton::clicked, this, [this, safeOpenDock](){ safeOpenDock(ui->dock3D); });
    ui->statusbar->addPermanentWidget(btn3D);

    QPushButton* btn4D = new QPushButton("4D View", this); btn4D->setObjectName("btnDock4D"); btn4D->setFlat(true);
    connect(btn4D, &QPushButton::clicked, this, [this, safeOpenDock](){ safeOpenDock(ui->dock4D); });
    ui->statusbar->addPermanentWidget(btn4D);

    QPushButton* btnScript = new QPushButton("Script", this); btnScript->setFlat(true);
    connect(btnScript, &QPushButton::clicked, this, [this, safeOpenDock](){ safeOpenDock(ui->dockScripts); });
    ui->statusbar->addPermanentWidget(btnScript);

    QPushButton* btnEx = new QPushButton("Library", this); btnEx->setFlat(true);
    connect(btnEx, &QPushButton::clicked, this, [this, safeOpenDock](){
        QSettings settings;
        QString root = settings.value("libraryRootPath").toString();
        if (root.isEmpty() || !SecurityBookmark::isAccessible(root)) setupDefaultFolders();
        safeOpenDock(ui->dockSurfaces);
    });
    ui->statusbar->addPermanentWidget(btnEx);

    // MOBILE: da qui in poi la barra scorre col dito. Va chiamata DOPO l'ultimo
    // addPermanentWidget - riparenta i widget gia' presenti, quindi qualsiasi
    // tasto aggiunto piu' avanti finirebbe fuori dal nastro. btnEq segna dove
    // iniziano i tasti dock: li' va lo stacco che prima nasceva dalla differenza
    // fra addWidget e addPermanentWidget.
    // m_renderProgress e m_statusLabel sono passati come INDICATORI: entrano nel
    // nastro subito dopo REC, il tasto a cui appartengono. Tenuti fuori dal
    // nastro finivano in coda alla barra - visivamente all'estrema destra, dopo
    // Library - perche' il nastro ha stretch 1 e si prende tutto lo spazio
    // residuo. No-op su desktop.
    StatusBarScroller::install(ui->statusbar, btnEq,
                               { m_renderProgress, m_statusLabel });

    // =========================================================================
    // 6. EQUATIONS, CONSTANTS & PARAMETERS
    // =========================================================================

    // 1. INIZIALIZZA LA MEMORIA
    m_lastParametricSteps = 100;
    m_lastImplicitSteps = 400;

    // 2. PREPARA L'INTERFACCIA E LO SLIDER AL LORO STATO INIZIALE (Senza lanciare segnali!)
    // Tab Parametric/Implicit a piena larghezza: due sole voci, ~meta' del dock
    // ciascuna (il dock Equations ha larghezza fissa 400). min-width via
    // stylesheet perche' col CSS globale setExpanding e' ignorato. Niente frecce
    // di scorrimento: il cambio si fa cliccando la linguetta.
    if (ui->tabModeSelector->tabBar())
        ui->tabModeSelector->tabBar()->setUsesScrollButtons(false);
    ui->tabModeSelector->setStyleSheet(
        "QTabBar::tab { min-width: 175px; padding: 6px 0px; }");
    setImplicitMode(false);
    // Sotto-tab Constraints/Composition/Geodesic Flow (panelImplicit): stessa
    // logica dei Parametric/Implicit, ma sono TRE voci sullo stesso dock da
    // 400px, quindi min-width piu' piccola (~un terzo) per riempire la barra
    // senza sforare e riattivare le frecce di scorrimento. min-width via
    // stylesheet perche' col CSS globale setExpanding e' ignorato.
    if (ui->panelImplicit->tabBar())
        ui->panelImplicit->tabBar()->setUsesScrollButtons(false);
    ui->panelImplicit->setStyleSheet(
        "QTabBar::tab { min-width: 110px; padding: 6px 0px; }");
    ui->glWidget->setEngineMode(GLWidget::ModeParametric);

    ui->stepSlider->setRange(10, 1000);
    setSteps(m_lastParametricSteps);
    ui->glWidget->setResolution(m_lastParametricSteps);

    ui->lblSteps->setText("Steps=");

    // 3. SOLO ORA COLLEGA IL SEGNALE DEL CAMBIO TAB
    // 3. SOLO ORA COLLEGA IL SEGNALE DEL CAMBIO TAB
    // Il clic (o un cambio a segnali vivi del programma, che vuole il reset):
    // prima lo STATO, che il reset e tutto il resto leggono.
    connect(ui->tabModeSelector, &QTabWidget::currentChanged,
            this, [this](int index) {
        m_implicitMode = (index == 1);
        applyModeTabReset(index);
    });

    // Cambio di sotto-tab dentro Implicit (3D <-> Cross Section). Il pannello
    // Run/Variations/Texture e i LIMITI X/Y/Z sono una sola istanza fisica
    // condivisa; equazione e radio Shell/Solid sono separati. Aprendo un
    // sotto-tab si mostra la sua superficie di DEFAULT, quindi i controlli
    // condivisi vanno riportati al default insieme a lei: restando addosso i
    // valori dell'altro sotto-tab, i limiti la taglierebbero e le costanti la
    // deformerebbero (vedi resetImplicitSharedFields).
    // Nessun reset di camera/texture/animazione: le superfici di default non ne
    // hanno (vedi loadCrossSectionDefaultSurface).
    if (ui->subTabImplicit) {
        // LAVORO NON SALVATO, come per il cambio di modalita' (tabModeSelector
        // qui sopra). Cambiare sotto-tab carica la superficie di DEFAULT e
        // riporta ai default i controlli condivisi: e' una distruzione di scena
        // quanto il passaggio Parametric <-> Implicit, e finora non chiedeva
        // nulla -- un tocco sulla linguetta sbagliata buttava via il lavoro.
        // Stesso schema, per le stesse ragioni: la conferma va su tabBarClicked
        // perche' currentChanged scatta a linguetta GIA' cambiata e da li' non
        // si potrebbe piu' rifiutare; tabBarClicked non permette di annullare il
        // cambio (Qt lo esegue subito dopo), quindi su Cancel si lascia cambiare
        // la linguetta, si dice all'handler di NON resettare e si riporta il
        // sotto-tab dov'era.
        if (ui->subTabImplicit->tabBar()) {
            connect(ui->subTabImplicit->tabBar(), &QTabBar::tabBarClicked,
                    this, [this](int index) {
                if (index < 0) return;

                if (index == (crossSectionTab() ? 1 : 0)) {
                    // RICLIC SULLA LINGUETTA GIA' ATTIVA = "ricomincia da capo",
                    // esattamente come per il tab principale. currentChanged non
                    // scatta se l'indice non cambia, quindi senza questo ramo
                    // ricliccare il sotto-tab in cui sei gia' non faceva nulla:
                    // dopo un New lo schermo restava vuoto e la superficie di
                    // default compariva solo premendo la linguetta NON attiva --
                    // mentre Parametric/Implicit la ricaricano in entrambi i casi.
                    // Su Cancel non si resetta (qui la linguetta non cambia,
                    // basta uscire).
                    if (!confirmDiscardUnsaved(ScopeScene)) return;
                    applyImplicitSubTabReset(index);
                    return;
                }

                if (!confirmDiscardUnsaved(ScopeScene)) {
                    const bool back = crossSectionTab();
                    m_suppressNextSubTabReset = true;
                    QTimer::singleShot(0, this, [this, back]() {
                        setCrossSectionTab(back);
                        m_suppressNextSubTabReset = false;
                    });
                }
            });
        }

        // Il clic: prima lo STATO, che il reset legge (quale superficie di
        // default caricare). Arriva solo dai clic: il programma scrive la
        // linguetta a segnali bloccati (setCrossSectionTab).
        connect(ui->subTabImplicit, &QTabWidget::currentChanged,
                this, [this](int index) {
            m_crossSectionTab = (index == 1);
            applyImplicitSubTabReset(index);
        });
    }

    // RESET SULLA LINGUETTA GIA' ATTIVA. currentChanged non scatta se l'indice
    // non cambia, quindi ricliccare il tab in cui sei gia' non faceva nulla.
    // Ora vale come "ricomincia da capo in questa modalita'": la stessa pulizia
    // totale del cambio tab (superficie di default, script/texture/path azzerati,
    // camera, zoom e limiti di spazio ripristinati) senza dover passare
    // dall'altra modalita' e tornare indietro. Riusa lo STESSO percorso, non una
    // copia: e' quello gia' validato dall'uso quotidiano, e i residui della
    // superficie precedente sono la causa storica delle deformazioni.
    if (ui->tabModeSelector->tabBar()) {
        connect(ui->tabModeSelector->tabBar(), &QTabBar::tabBarClicked,
                this, [this](int index) {
            if (index < 0) return;

            if (index == (implicitMode() ? 1 : 0)) {
                // Riclic sulla linguetta attiva = "ricomincia da capo".
                // Lavoro non salvato: si chiede prima di buttarlo via, per OGNI
                // modulo sporco (il reset azzera anche texture e suono). Su
                // Cancel non si resetta (qui il tab non cambia, basta uscire).
                if (!confirmDiscardUnsaved(ScopeScene)) return;
                // "Ricomincia da capo": a differenza del cambio di modalita',
                // qui la texture non sopravvive, quindi il suo script non va
                // ripristinato nell'editor. Vedi m_sameTabRestart.
                m_sameTabRestart = true;
                applyModeTabReset(index);
                m_sameTabRestart = false;
                return;
            }

            // CAMBIO DI MODALITA' VERO. Anche questo distrugge la scena, via
            // currentChanged -> applyModeTabReset, e finora non chiedeva nulla:
            // un tocco sulla linguetta sbagliata buttava via il lavoro. La
            // conferma va CHIESTA QUI, perche' currentChanged scatta a tab gia'
            // cambiato e da li' non si potrebbe piu' rifiutare.
            // tabBarClicked non permette di annullare il cambio (Qt lo esegue
            // subito dopo), quindi su Cancel si lascia cambiare la linguetta e
            // si dice ad applyModeTabReset di NON resettare, riportando poi il
            // tab dov'era: la scena resta intatta.
            if (!confirmDiscardUnsaved(ScopeScene)) {
                const bool back = implicitMode();
                m_suppressNextModeTabReset = true;
                QTimer::singleShot(0, this, [this, back]() {
                    setImplicitMode(back);
                    m_suppressNextModeTabReset = false;
                });
            }
        });
    }

    setEqText(&EquationTexts::x, QStringLiteral("(0.8 + 0.3*cos(v))*cos(u)"));
    setEqText(&EquationTexts::y, QStringLiteral("(0.8 + 0.3*cos(v))*sin(u)"));
    setEqText(&EquationTexts::z, QStringLiteral("0.3*sin(v)"));
    setEqText(&EquationTexts::p, QStringLiteral("0.0"));

    ui->glWidget->setParametricEquations(m_eq.x, m_eq.y, m_eq.z, m_eq.p);

    ui->uMinEdit->setText(QString::number(uMin, 'g', 12));
    ui->uMaxEdit->setText(QString::number(uMax, 'g', 12));
    ui->vMinEdit->setText(QString::number(vMin, 'g', 12));
    ui->vMaxEdit->setText(QString::number(vMax, 'g', 12));
    ui->wMinEdit->setText(QString::number(wMin, 'g', 12));
    ui->wMaxEdit->setText(QString::number(wMax, 'g', 12));
    ui->wMinEdit->setEnabled(false);
    ui->wMaxEdit->setEnabled(false);

    updateULimits();
    updateVLimits();

    ui->aSlider->setRange(0, 1000); ui->aSlider->setValue(100);
    ui->bSlider->setRange(0, 1000); ui->bSlider->setValue(100);
    ui->cSlider->setRange(0, 1000); ui->cSlider->setValue(100);
    ui->dSlider->setRange(0, 1000); ui->dSlider->setValue(100);
    ui->eSlider->setRange(0, 1000); ui->eSlider->setValue(100);
    ui->fSlider->setRange(0, 1000); ui->fSlider->setValue(100);
    ui->sSlider->setRange(-1000, 1000); ui->sSlider->setValue(0);

    // --- Inizializzazione Testi di Default ---
    if (ui->lineA->text().isEmpty()) ui->lineA->setText("1");
    if (ui->lineB->text().isEmpty()) ui->lineB->setText("1");
    if (ui->lineC->text().isEmpty()) ui->lineC->setText("1");
    if (ui->lineD->text().isEmpty()) ui->lineD->setText("1");
    if (ui->lineE->text().isEmpty()) ui->lineE->setText("1");
    if (ui->lineF->text().isEmpty()) ui->lineF->setText("1");
    if (ui->lineS->text().isEmpty() || ui->lineS->text() == "0.4") {
        ui->lineS->setText("0");
    }

    if (!m_meshDebounce) {
        m_meshDebounce = new QTimer(this);
        m_meshDebounce->setSingleShot(true);
        m_meshDebounce->setInterval(120);
        connect(m_meshDebounce, &QTimer::timeout, this, [this]() {
            if (ui->glWidget && !implicitMode()) {
                ui->glWidget->setResolution(m_steps);
            }
            // Input nuovo (costanti/steps): un errore geodetico precedente non
            // deve congelare il ricalcolo, altrimenti riportare una costante
            // al valore buono lascia la mesh bloccata sull'ultimo stato.
            m_geodesicErrorPending = false;
            // useAppliedEquations: questo debounce lo fanno partire lo slider
            // Steps e le costanti, che NON sono un Run. Senza il flag il ramo
            // geodetico rileggeva X/Y/Z/P dai campi e applicava equazioni
            // modificate e non confermate -- lo stesso difetto che avevano i
            // limiti: bastava muovere Steps per committarle di straforo.
            checkAndTriggerMeshUpdate(/*useAppliedEquations=*/true);

            // Il Run NON si spegne qui, in nessuno dei due rami. Questo percorso
            // (slider costanti, slider Steps) non applica equazioni: le congela
            // tutte sullo snapshot -- la mappa X/Y/Z/P e, dal disallineamento
            // corretto, anche i 7 campi del flusso geodetico. Se l'utente ha
            // scritto qualcosa in quei campi la modifica e' ancora in attesa, e
            // il Run e' l'unico modo di applicarla: spegnerlo la renderebbe
            // irraggiungibile. Solo un Run vero rialza m_parametricApplied.
            updateMasterButtonState();
        });
    }

    // --- MOTORE COSTANTI A CASCATA --- (vedi MainWindow::evaluateCascade)
    auto connectSlider = [this](ConstField field) {
        QSlider *slider = constantSlider(field);
        QLineEdit *line = constantFieldEdit(field);
        connect(slider, &QSlider::valueChanged, this, [this, field, line](int val) {
            if (!line->hasFocus()) {
                setConstText(field, QString::number(val / 100.0f, 'g', 6));
                evaluateCascade(); // Aggiorna le altre caselle che dipendono da questo!
            }
        });
        // Snap delle costanti discrete ("A := int(1,6)") al RILASCIO, non durante
        // il trascinamento: agganciarlo a valueChanged farebbe scattare il cursore
        // sotto il dito a ogni tacca, e rigenererebbe la mesh a ogni scatto.
        connect(slider, &QSlider::sliderReleased, this, [this]() {
            if (applyDiscreteConstants()) evaluateCascade();
        });
    };

    for (ConstField f : constantFields()) connectSlider(f);

    auto connectLineEdit = [this](QLineEdit* line) {
        connect(line, &QLineEdit::editingFinished, this, [this]() {
            // Prima lo snap delle costanti discrete: cosi' la cascata sotto parte
            // gia' dal valore intero e non ricalcola due volte.
            applyDiscreteConstants();
            evaluateCascade();                           // clamp + cascata + slider + push costanti
            if (m_meshDebounce) m_meshDebounce->stop();  // evita il doppio ridisegno asincrono
            SE_GEO_PROBE("costante editingFinished -> commitFieldsOnEnter");
            const bool okConst = commitFieldsOnEnter();  // valida: se ok ridisegna, altrimenti vecchia immagine + popup
            SE_GEO_PROBE("costante esito applied=%d", int(okConst));
        });
    };

    connectLineEdit(ui->lineA); connectLineEdit(ui->lineB);
    connectLineEdit(ui->lineC); connectLineEdit(ui->lineD);
    connectLineEdit(ui->lineE); connectLineEdit(ui->lineF);
    connectLineEdit(ui->lineS);

    evaluateCascade();

    ui->stepSlider->setRange(10, 1000);
    int initialSteps = 100;
    setSteps(initialSteps);
    ui->glWidget->setResolution(initialSteps);
    ui->lblSteps->setText(QString("Steps="));

    // (1) valueChanged: aggiorna testo + avvia debounce
    connect(ui->stepSlider, &QSlider::valueChanged, this, [this](int val) {
        // Il gesto (o un setValue a segnali vivi, come la riduzione del
        // massimo in checkParametricDependency): lo stato per primo.
        m_steps = val;
        ui->lineSteps->setText(QString::number(val));
        if (!ui->glWidget) return;
        if (implicitMode()) {
            ui->glWidget->setRaySteps(val);
            ui->glWidget->update();
        } else {
            m_meshDebounce->start();
        }
    });

    // (2) sliderPressed: sospende il rendering durante il trascinamento
    connect(ui->stepSlider, &QSlider::sliderPressed, this, [this]() {
        if (ui->glWidget) ui->glWidget->setUpdatesEnabled(false);
    });

    // (3) sliderReleased: riattiva il rendering e rigenera al rilascio
    connect(ui->stepSlider, &QSlider::sliderReleased, this, [this]() {
        if (ui->glWidget) {
            ui->glWidget->setUpdatesEnabled(true);
            ui->glWidget->update();
        }
        m_meshDebounce->start();
    });

    auto applyStepsFromLine = [this](bool notify) {
        const QString txt = ui->lineSteps->text().trimmed();
        if (txt.isEmpty()) return;   // vuoto durante la digitazione: ignora
        bool ok = false;
        int val = txt.toInt(&ok);
        if (!ok) {
            // notify=true solo al commit (Enter/uscita campo): niente popup mentre si digita
            if (!ok) {
                if (notify && !m_constantPopupActive) {
                    m_constantPopupActive = true;
                    InputValidator::showInvalidStepsError(this, txt);
                    setSteps(m_steps);   // il campo torna a mostrare lo stato
                    ui->lineSteps->selectAll();
                    // Reset RIMANDATO a fine ciclo di eventi, come gli altri
                    // popup di questo modulo. Oggi qui si arriva una volta sola
                    // (i filtri tastiera consumano il Return, quindi dei due
                    // trigger collegati -- editingFinished e returnPressed --
                    // ne scatta uno), ma il reset sincrono rende il doppione
                    // dipendente da quel dettaglio: basterebbe un percorso che
                    // lascia passare il Return per vedere due box in fila.
                    QTimer::singleShot(0, this, [this]{ m_constantPopupActive = false; });
                }
                return;
            }
        }
        val = std::clamp(val, ui->stepSlider->minimum(), ui->stepSlider->maximum());
        if (val != m_steps)
            ui->stepSlider->setValue(val);   // emette valueChanged -> stato, glWidget e testo
    };

    // Trigger "forti": al commit notifichiamo (notify=true)
    connect(ui->lineSteps, &QLineEdit::editingFinished, this, [applyStepsFromLine]() { applyStepsFromLine(true); });
    connect(ui->lineSteps, &QLineEdit::returnPressed,   this, [applyStepsFromLine]() { applyStepsFromLine(true); });

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    m_stepsDebounce = new QTimer(this);
    m_stepsDebounce->setSingleShot(true);
    m_stepsDebounce->setInterval(350);
    connect(m_stepsDebounce, &QTimer::timeout, this, [applyStepsFromLine]() { applyStepsFromLine(false); });

    connect(ui->lineSteps, &QLineEdit::textEdited, this, [this](const QString&) {
        m_stepsDebounce->start();
    });
#endif

    // 1. Dipendenze delle equazioni principali
    connect(ui->lineX, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
    connect(ui->lineY, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
    connect(ui->lineZ, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
    connect(ui->lineP, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);

    auto markUserEdit = [this]() {
        // Lavoro dell'utente da proteggere: il reset chiedera' se salvarlo.
        // Il campo va riportato QUAL E': questa lambda serve 13 widget diversi e
        // passava sempre lineX, cosi' un edit alle composizioni (U/V/W) o ai
        // vincoli espliciti veniva contato come "sto scrivendo le equazioni".
        noteSceneEdited(qobject_cast<QWidget*>(sender()));
        // Le equazioni sono cambiate: il Run parametrico "one-shot" (senza 't')
        // torna eseguibile. updateMasterButtonState() riabilita il tasto.
        m_parametricApplied = false;
        updateMasterButtonState();

        // L'evidenziazione del preset in libreria RESTA anche dopo aver
        // modificato le equazioni. Prima cadeva al primo carattere, e solo sul
        // ramo Superfici -- texture, suoni e record non l'hanno mai fatto:
        // un'asimmetria nata dall'aggiunta fatta da un lato solo. Serve a
        // ricordare su quale preset si sta lavorando, che e' la ragione per cui
        // la si guarda. Effetto collaterale utile: annullando il caricamento di
        // un altro preset (Cancel sul popup del lavoro non salvato, ~10407)
        // c'e' sempre un item da rievidenziare, e l'albero non resta spoglio.
    };
    connect(ui->lineX, &QPlainTextEdit::textChanged, this, markUserEdit);
    connect(ui->lineY, &QPlainTextEdit::textChanged, this, markUserEdit);
    connect(ui->lineZ, &QPlainTextEdit::textChanged, this, markUserEdit);
    connect(ui->lineP, &QPlainTextEdit::textChanged, this, markUserEdit);

    // Anche composizioni (U/V/W) e vincoli espliciti cambiano la superficie:
    // un loro edit deve riabilitare il Run parametrico one-shot.
    connect(ui->lineU, &QPlainTextEdit::textChanged, this, markUserEdit);
    connect(ui->lineV, &QPlainTextEdit::textChanged, this, markUserEdit);
    connect(ui->lineW, &QPlainTextEdit::textChanged, this, markUserEdit);
    connect(ui->lineExplicitU, &QPlainTextEdit::textChanged, this, markUserEdit);
    connect(ui->lineExplicitV, &QPlainTextEdit::textChanged, this, markUserEdit);
    connect(ui->lineExplicitW, &QPlainTextEdit::textChanged, this, markUserEdit);

    auto markTextureModified = [this]() { this->setProperty("isTextureModified", true); };
    connect(ui->txtScriptEditor, &QPlainTextEdit::textChanged, this, markTextureModified);
    connect(ui->lineTexture, &QPlainTextEdit::textChanged, this, markTextureModified);
    connect(ui->lineVariations, &QPlainTextEdit::textChanged, this, markTextureModified);

    // Run "one-shot" della texture Ray Marching: modificare lo script di colore
    // (lineTexture) o di displacement (lineVariations) lo riabilita.
    auto markRmTextureEdited = [this]() {
        m_rmTextureApplied = false;
        // NIENTE noteSceneEdited: lineTexture/lineVariations sono campi del
        // modulo TEXTURE, non della geometria (il loro lavoro non salvato e'
        // del modulo: PartTexture nell'impronta della scena, non la SCENA --
        // su un reset uscivano DUE popup di fila, prima "scena" poi "texture",
        // per un solo lavoro). Passando lineTexture non produceva
        // nemmeno l'avviso cross-dock: cade nel ramo "campo che non definisce
        // la superficie" e torna subito.
        //
        // Lavoro non salvato del MODULO texture: lo dice il confronto dello
        // stato (textureModuleDirty) e lo elenca confirmDiscardUnsaved.
        //
        // IL NOME (libName) NON SI AZZERA QUI.
        // Prima si azzerava, col ragionamento "il codice non e' piu' quello
        // della voce, quindi il nome punterebbe a una texture diversa da
        // quella a schermo". Ma libName non vuol dire "il codice e' identico
        // a quella voce": vuol dire **da quale voce questa texture VIENE**.
        // Sono due cose diverse, e la prima si ricalcola quando serve
        // confrontando i codici -- e' esattamente cio' che fa
        // focusedTextureLibraryItem.
        //
        // Azzerandolo si distruggeva il legame proprio nel caso per cui e'
        // stato introdotto. Sequenza misurata: Sync (allinea), poi ritocco a
        // mano della densita' nell'editor -> qui il nome si azzerava -> il
        // salvataggio omette libName (lo scrive solo se non vuoto,
        // presetserializer ~1516) -> il record ricaricato non ha piu'
        // l'ancora, e "Sync Focused Texture" resta GRIGIO per sempre benche'
        // il disallineamento ci sia (record 6.0 contro libreria 12.0).
        // Cioe': la voce si spegneva appena si creava il lavoro che le
        // compete.
        //
        // Tenerlo non fa danni: un codice divergente lo vede il gate, che
        // confronta i due testi e abilita la voce; e se davvero non c'entra
        // piu' nulla, il focus nell'albero ricade sul match per CODICE, che
        // ha sempre la precedenza sul nome (selectTextureTreeItemFor: due
        // passate, prima il codice).
        updateMasterButtonState();
    };
    connect(ui->lineTexture, &QPlainTextEdit::textChanged, this, markRmTextureEdited);
    connect(ui->lineVariations, &QPlainTextEdit::textChanged, this, markRmTextureEdited);

    connect(ui->txtScriptEditor, &QPlainTextEdit::textChanged, this, [this](){
        // L'editor e' la VISTA dello slot mostrato: cio' che l'utente scrive va
        // li', subito. (Le scritture del programma passano da setScriptText e
        // arrivano qui a segnali bloccati.)
        const ScriptSlot shown = shownScriptSlot();
        const QString typed = ui->txtScriptEditor->toPlainText();
        m_scriptEditorText = typed;
        switch (shown) {
        case SlotSurface:           m_surfaceScriptText = typed; break;
        case SlotSurfaceTexture:    m_surfaceTextureScriptText = typed; break;
        case SlotMeshTexture:       syncMeshTextureSlot(); m_meshTextureScriptText = typed; break;
        case SlotBackgroundTexture: m_bgTextureScriptText = typed; break;
        case SlotSound:             m_soundScriptText = typed; break;
        case SlotNone:              break;
        }
        updateScriptButtonText();
        updateConstantsUIState();
        // txtScriptEditor e' UN widget per tre moduli: il lavoro appartiene a
        // quello che sta mostrando, e va marcato SOLO li'. Marcare anche la
        // scena (noteSceneEdited) mentre si scrive una texture o un suono
        // faceva uscire due popup di fila su un reset, per un lavoro solo.
        if (m_currentScriptMode == ScriptModeSurface)
            noteSceneEdited(ui->txtScriptEditor);
        // Mantiene allineato il tasto Save texture (hasSavableTexture legge l'editor
        // in modalità script texture parametrico/sfondo).
        updateMasterButtonState();
    });

    // 2. Mutua esclusione dei vincoli (con blocco segnali per evitare loop a catena!)
    connect(ui->lineExplicitU, &QPlainTextEdit::textChanged, this, [this](){
        if(!m_eq.explicitU.isEmpty()) {
            setEqText(&EquationTexts::explicitV, QString());
            setEqText(&EquationTexts::explicitW, QString());
        }
    });
    connect(ui->lineExplicitV, &QPlainTextEdit::textChanged, this, [this](){
        if(!m_eq.explicitV.isEmpty()) {
            setEqText(&EquationTexts::explicitU, QString());
            setEqText(&EquationTexts::explicitW, QString());
        }
    });
    connect(ui->lineExplicitW, &QPlainTextEdit::textChanged, this, [this](){
        if(!m_eq.explicitW.isEmpty()) {
            setEqText(&EquationTexts::explicitU, QString());
            setEqText(&EquationTexts::explicitV, QString());
        }
    });

    // 3. Dipendenze dei vincoli (chiamano checkParametricDependency, che a sua volta chiamerà updateConstraintState)
    connect(ui->lineExplicitU, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
    connect(ui->lineExplicitV, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
    connect(ui->lineExplicitW, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);

    // 4. Dipendenze delle composizioni
    connect(ui->lineU, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
    connect(ui->lineV, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
    connect(ui->lineW, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);

    connect(ui->lineEquation, &QPlainTextEdit::textChanged, this, &MainWindow::updateConstantsUIState);
    connect(ui->lineTexture, &QPlainTextEdit::textChanged, this, &MainWindow::updateConstantsUIState);
    connect(ui->lineVariations, &QPlainTextEdit::textChanged, this, &MainWindow::updateConstantsUIState);
    connect(ui->lineEquationCrossSection, &QPlainTextEdit::textChanged, this, &MainWindow::updateConstantsUIState);

    // Run "one-shot" del tab Ray Marching: modificare l'EQUAZIONE implicita lo
    // riabilita. Solo lineEquation -> texture e displacement sono del modulo
    // texture, non della geometria (coerente con geomAnimated in onStartClicked).
    connect(ui->lineEquation, &QPlainTextEdit::textChanged, this, [this]() {
        m_implicitApplied = false;
        noteSceneEdited(ui->lineEquation);
        updateMasterButtonState();

        // Come nel ramo parametrico qui sopra: l'evidenziazione resta.
    });

    // Stessa cosa per il sotto-tab Cross Section (equazione a 4 variabili):
    // editor distinto, ma stesso contratto Run "one-shot" del tab 3D.
    connect(ui->lineEquationCrossSection, &QPlainTextEdit::textChanged, this, [this]() {
        m_implicitApplied = false;
        noteSceneEdited(ui->lineEquationCrossSection);
        updateMasterButtonState();
    });

    if (ui->lnU) {
        connect(ui->lnU,    &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
        connect(ui->lnV,    &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
        connect(ui->lnW,    &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
        connect(ui->lndU,   &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
        connect(ui->lndV,   &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
        connect(ui->lndW,   &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);
        connect(ui->lineConform, &QPlainTextEdit::textChanged, this, &MainWindow::checkParametricDependency);

        connect(ui->lineConform, &QPlainTextEdit::textChanged, this, markUserEdit);
        connect(ui->lnU, &QPlainTextEdit::textChanged, this, markUserEdit);
        connect(ui->lnV, &QPlainTextEdit::textChanged, this, markUserEdit);
        connect(ui->lnW, &QPlainTextEdit::textChanged, this, markUserEdit);

        connect(ui->lndU, &QPlainTextEdit::textChanged, this, markUserEdit);
        connect(ui->lndV, &QPlainTextEdit::textChanged, this, markUserEdit);
        connect(ui->lndW, &QPlainTextEdit::textChanged, this, markUserEdit);
    }

    checkParametricDependency();
    updateConstraintState();

    // ----------------------------------------------------
    // IMPLICIT MODE SETTINGS (Equazioni e Limiti Spaziali)
    // ---------------------------------------------------

    // --- 0. Impostazioni di Default UI ---
    // DUE GRUPPI ESCLUSIVI SEPARATI. I quattro radio (Shell/Solid e Fast/Precise)
    // sono fratelli nella griglia gridRenderControls, e l'esclusivita' automatica
    // dei QRadioButton vale fra TUTTI i fratelli con lo stesso genitore: senza
    // questi gruppi accendere "Precise" spegneva "Shell". Vanno creati PRIMA dei
    // setChecked e dei connect qui sotto.
    m_shellSolidGroup = new QButtonGroup(this);
    m_shellSolidGroup->setExclusive(true);
    m_shellSolidGroup->addButton(ui->radioShell);
    m_shellSolidGroup->addButton(ui->radioSolid);

    m_marcherGroup = new QButtonGroup(this);
    m_marcherGroup->setExclusive(true);
    m_marcherGroup->addButton(ui->radioMarcherFast);
    m_marcherGroup->addButton(ui->radioMarcherPrecise);

    applyImplicitShellMode(true);   // "Shell" di default: stato, radio e motore (modalita' 1)

    // --- Connessione dei Radio Button (Solid/Shell) ---
    // UNA SOLA COPPIA, in panelRenderControls: il widget comune ai due sotto-tab
    // ("3D" e "Cross Section"), insieme a Thickness e ai radio del marcher.
    //
    // Prima i radio erano DUPLICATI, uno per pannello equazione, perche' i
    // pannelli sono due — ma lo stato Shell/Solid e' sempre stato UNO SOLO (il
    // render mode del motore e' globale, e il preset lo salva in un solo campo).
    // Quella duplicazione ha prodotto una serie di bug: i radio del Cross Section
    // inizialmente non erano nemmeno collegati (cliccarli non faceva nulla), e il
    // serializer leggeva la coppia del 3D anche salvando dal Cross Section.
    // Con un'unica coppia il problema non esiste piu': niente da riallineare.
    auto updateImplicitRenderMode = [this](bool checked) {
        if (!checked) return;   // solo chi si accende, non chi si spegne
        const bool toShell = (sender() == ui->radioShell);
        applyImplicitShellMode(toShell);
        // SOLID: lo spessore non ha piu' un guscio da misurare, quindi torna al
        // minimo (setShellThicknessUI incapsula motore + slider + curva): lo
        // slider spento non mostra un valore che non si applica, e un preset
        // salvato in Solid non si porta dietro un residuo.
        // Il valore di prima pero' si RICORDA, e tornando a Shell si rimette:
        // senza, un giro Shell -> Solid -> Shell perdeva lo spessore del preset
        // (Ding Dong: 0.108 -> 0.005) e la figura cambiava forma -- il collo
        // arrotondato del guscio diventava la punta del cono.
        // SOLO sul click dell'utente, non in applyImplicitShellMode: quella gira
        // anche durante i load, dove il preset puo' avere il proprio spessore
        // salvato e azzerarlo qui lo perderebbe.
        if (!toShell) {
            const float keep = ui->glWidget ? ui->glWidget->shellThickness() : -1.0f;
            setShellThicknessUI(0.005f);          // azzera anche il ricordo...
            m_shellThicknessBeforeSolid = keep;   // ...quindi lo si scrive dopo
        } else if (m_shellThicknessBeforeSolid > 0.0f) {
            setShellThicknessUI(m_shellThicknessBeforeSolid);   // e lo consuma
        }
        // Il pannello Thickness ha senso solo con Shell (in Solid non c'e' guscio
        // di cui regolare la parete): il gate vive in updateRenderState, che va
        // richiamata qui o il pannello resterebbe come era fino al prossimo
        // evento che la fa girare. applyImplicitShellMode non la chiama da se'
        // perche' e' usata anche durante i load, dove gira comunque alla fine.
        updateRenderState();
    };
    connect(ui->radioShell, &QRadioButton::toggled, this, updateImplicitRenderMode);
    connect(ui->radioSolid, &QRadioButton::toggled, this, updateImplicitRenderMode);

    // --- MARCHER: Fast (sphere tracing storico) / Precise (ibrido) ---
    // "Fast" di default: e' il comportamento di sempre, e ogni superficie che non
    // chiede esplicitamente il Precise si disegna come prima. Il T^3 del Cross
    // Section accende Precise da loadCrossSectionDefaultSurface, e i preset se lo
    // portano dietro (chiave "hybridMarcher").
    //
    // UNA SOLA COPPIA, nella zona comune ai due sotto-tab (come il Thickness):
    // la scelta del marcher non dipende dal sotto-tab, quindi non serve il
    // doppione con il riallineamento che Shell/Solid richiede.
    setMarcherUI(false);   // Fast di default: motore e radio
    auto updateMarcherMode = [this](bool checked) {
        if (!checked) return;                 // solo chi si accende
        const bool precise = (sender() == ui->radioMarcherPrecise);
        if (ui->glWidget) ui->glWidget->setHybridMarcher(precise);
        // E' un uniform: nessun rebuildShader, il cambio si vede al frame dopo.
        // Nessun gating: Ray Steps e Step Relax servono con entrambi i marcher
        // (vedi il commento in updateRenderState).
        noteSceneEdited(ui->radioMarcherPrecise);
    };
    if (ui->radioMarcherFast)
        connect(ui->radioMarcherFast, &QRadioButton::toggled, this, updateMarcherMode);
    if (ui->radioMarcherPrecise)
        connect(ui->radioMarcherPrecise, &QRadioButton::toggled, this, updateMarcherMode);

    // --- SFONDO: Fixed / Sphere / Cylinder / Cube (gruppo "Background Controls") ---
    // I quattro radio hanno un contenitore proprio (panelBgLock), quindi si
    // escludono a vicenda senza toccare Base/Phong/WireFrame. Vale per entrambi i
    // modi: anche in Ray Marching lo sfondo e' lo stesso pass.
    // L'indice in bgSkyRadios() E' la modalita' (GLWidget::BgSkyMode): una sola
    // tabella per clic, load, reset e tooltip.
    // Nessun rebuild: la modalita' e' letta dallo shader a ogni frame (vedi
    // GLWidget::render). Si segna la scena come modificata: il record salva la
    // scelta ("background"/"skyMode").
    {
        const QList<QRadioButton*> skyRadios = bgSkyRadios();
        for (int mode = 0; mode < skyRadios.size(); ++mode) {
            if (!skyRadios[mode]) continue;
            connect(skyRadios[mode], &QRadioButton::toggled, this, [this, mode](bool checked) {
                if (!checked) return;         // solo chi si accende
                if (ui->glWidget) ui->glWidget->setBackgroundSkyMode(mode);
            });
        }
    }

    // SPESSORE DEL GUSCIO (Shell). Scala 0..100 -> 0.005..0.30, non lineare:
    // quadratica, cosi' la prima meta' della corsa copre i valori sottili (dove
    // serve precisione) e la seconda arriva ai gusci spessi.
    //
    // Perche' e' un controllo e non una costante: normalizzare il campo col
    // gradiente ha reso lo spessore una LUNGHEZZA VERA, uguale per tutte le
    // equazioni. E' corretto, ma prima valeva 0.01/|grad|, quindi dipendeva da
    // come l'equazione era scritta: le superfici con un fattore di scala davanti
    // avevano gusci molto piu' spessi. Misurato, per tornare all'aspetto storico
    // servono 0.10 a Ding Dong (|grad| 0.096) e 0.26 a Steiner (|grad| 0.039),
    // mentre la sfera e il T^3 del Cross Section stanno bene a 0.005: due ordini
    // di grandezza di differenza, nessun default unico puo' accontentarle tutte.
    // Il tetto 0.30 copre Steiner con margine.
    const double kShellThickMin = 0.005, kShellThickMax = 0.30;
    auto shellThickFromSlider = [=](int v) {
        const double f = v / 100.0;
        return kShellThickMin + (kShellThickMax - kShellThickMin) * f * f;
    };
    if (ui->shellThicknessSlider) {
        ui->shellThicknessSlider->setRange(0, 100);
        ui->shellThicknessSlider->setValue(0);          // 0.005 = comportamento storico
        // Stesso aspetto degli altri slider grandi: l'handle e' 30px con
        // margin -10px, quindi senza questo stile (e senza l'altezza minima nel
        // .ui) il disco viene TAGLIATO dal bordo del widget.
        ui->shellThicknessSlider->setStyleSheet(
            "QSlider::groove:horizontal { border: 1px solid #999; height: 12px;"
            " border-radius: 6px; margin: 2px 0; background: #AAAAAA; }"
            "QSlider::handle:horizontal { background: white; border: 1px solid #5c5c5c;"
            " width: 30px; height: 30px; margin: -10px 0; border-radius: 15px; }");
        ui->shellThicknessSlider->setMinimumHeight(40);
        connect(ui->shellThicknessSlider, &QSlider::valueChanged, this,
                [this, shellThickFromSlider](int v) {
            const double t = shellThickFromSlider(v);
            if (ui->glWidget) ui->glWidget->setShellThickness((float)t);
            noteSceneEdited(ui->shellThicknessSlider);
        });
    }


    // --- 1. Equazioni Implicite di Default (Solo 3D) ---
    setRmText(&ImplicitTexts::equation, QStringLiteral("x^2 + y^2 + z^2 = 1.0"));
    updateConstantsUIState();

    auto updateImplicitEquations = [this]() {
        if(ui->glWidget) {
            QString rawEq = m_rm.equation.trimmed();
            QString implicitEqF;

            // Formatttiamo l'equazione rimuovendo l'uguale per renderla digeribile da GLSL
            if (rawEq.contains("=")) {
                QStringList parts = rawEq.split("=");
                if (parts.size() == 2) {
                    implicitEqF = QString("(%1) - (%2)").arg(parts[0].trimmed(), parts[1].trimmed());
                }
            } else {
                implicitEqF = QString("(%1) - (0.0)").arg(rawEq);
            }

            ui->glWidget->setImplicitEquation(implicitEqF);
        }
    };

    updateImplicitEquations();


    // --- 2. Limiti Spaziali (Facoltativi) ---
    // Partiamo con le caselle vuote = Nessun taglio applicato
    ui->lineXMin->clear(); ui->lineXMax->clear();
    ui->lineYMin->clear(); ui->lineYMax->clear();
    ui->lineZMin->clear(); ui->lineZMax->clear();

    // LIMITI SPAZIALI X/Y/Z: si applicano al RUN, non all'Invio -- stessa regola
    // dei limiti u/v/w e delle equazioni. Sono il taglio della scena in Ray
    // Marching, cioe' parte della definizione di cio' che si vede, e come tali
    // vanno confermati insieme all'equazione implicita. Niente connect su
    // editingFinished qui: l'applicazione passa da applySpaceLimits(), che il
    // ramo Ray Marching di onStartClicked chiama a ogni Run.
    // Le modifiche dell'utente riaccendono il Run one-shot, come per ogni altro
    // campo del modulo (vedi le connect textEdited piu' sotto).
    applySpaceLimits(/*notify=*/false);   // allineamento iniziale, silenzioso

    for (QLineEdit* spaceEdit : { ui->lineXMin, ui->lineXMax,
                                  ui->lineYMin, ui->lineYMax,
                                  ui->lineZMin, ui->lineZMax }) {
        // Come i limiti u/v/w, ammettono A..F/S: scrivere "2*A" sblocca subito
        // slider e casella di A.
        connect(spaceEdit, &QLineEdit::textEdited, this, [this] { m_constantsEditPending = true; });
        connect(spaceEdit, &QLineEdit::textChanged, this, &MainWindow::updateConstantsUIState);
        connect(spaceEdit, &QLineEdit::textEdited, this, [this](const QString&) {
            if (!m_uiReady) return;
            noteSceneEdited(qobject_cast<QWidget*>(sender()));
            // Il tasto Run del tab Ray Marching torna eseguibile: c'e' un taglio
            // nuovo da applicare. E' l'omologo di m_parametricApplied per il
            // ramo implicito.
            m_implicitApplied = false;
            updateMasterButtonState();
        });
    }

    // LIMITI U/V/W: il dominio fa parte della definizione della superficie e
    // segue la stessa regola delle equazioni -- a superficie FERMA aspetta il
    // Run, IN MOTO entra subito (li' un Run da premere non c'e': il tasto e'
    // "Stop"). La conferma del campo -- Invio o uscita -- passa da
    // commitLimitFieldOnEnter, che valida il numero e registra il dominio
    // nell'engine in entrambi i casi; le connect stanno piu' avanti, insieme a
    // quelle che riaccendono il Run. Il Return non arriva mai ai QLineEdit --
    // lo consumano prima i filtri tastiera (desktop e mobile) -- percio'
    // l'Invio passa di li'. onStartClicked rilegge e valida i campi per conto
    // suo, cosi' al Run i limiti entrano in vigore insieme alle equazioni.
    //
    // I LIMITI SPAZIALI X/Y/Z qui sopra restano invece immediati: sono un
    // taglio della VISTA in Ray Marching, non il dominio dei parametri.

    connect(ui->btnTextureCode, &QPushButton::clicked, this, [this]() {
        onRunRaymarchTextureClicked();
    });

    connect(ui->btnSave, &QPushButton::clicked, this, &MainWindow::onSaveTextureClicked);

    // =========================================================================
    // 7. RENDERER, COLORS & LIGHTING
    // =========================================================================
    ui->glWidget->setProjectionMode(1);
    updateProjectionButtonText();

    // Il gruppo esclusivo gestisce SOLO la modalità di rendering della superficie
    // (Base / Phong / Wireframe): sono mutuamente esclusivi perché la superficie
    // viene disegnata in un solo modo. Il Background NON ne fa parte: è una funzione
    // indipendente (texture di sfondo) e va attivata/disattivata senza spegnere la
    // modalità superficie e viceversa.
    m_modeGroup = new QButtonGroup(this);
    m_modeGroup->addButton(ui->radioBasic, 0);
    m_modeGroup->addButton(ui->radioPhong, 1);
    m_modeGroup->addButton(ui->radioWF,    2);
    m_modeGroup->setExclusive(true);

    // TARGET DI EDITING: coppia esclusiva Surface / Background. Sceglie COSA
    // pilotano gli slider/texture/colore: la superficie o lo sfondo. NON tocca la
    // modalità di rendering della superficie (Base/Phong/Wireframe), che resta un
    // asse a sé nel m_modeGroup. Il bersaglio e' STATO (m_editTarget, letto con
    // editingBackground()); i due radio ne sono la vista. radioSurface ha
    // assorbito il vecchio radioEditSurf.
    // I due color slot della texture (radioTexColor1/2) stanno in m_colorGroup a parte;
    // l'esclusività FRA i due gruppi è mantenuta a mano (vedi setColorTargetExclusive).
    m_bgTargetGroup = new QButtonGroup(this);
    m_bgTargetGroup->addButton(ui->radioSurface);
    m_bgTargetGroup->addButton(ui->radioBackground);
    m_bgTargetGroup->setExclusive(true);
    // Default = editing superficie. Impostato PRIMA della connect dell'handler di
    // radioBackground (più sotto) così non scatena logica al boot.
    setEditTarget(EditTarget::Surface);
    // I due gruppi (coppia m_bgTargetGroup e color slot m_colorGroup) sono INDIPENDENTI:
    // la coppia dice DOVE operi (Surface/Background), Color1/2 dicono QUALE tinta
    // della texture editi quando una texture colorata è attiva. Possono essere accesi
    // entrambi (es. Surface + Color 1). Nessuna deselezione incrociata: cliccare un radio
    // della tripla lascia intatta la coppia Color1/2 e viceversa. Qui basta riallineare
    // gli slider. Background porta la sua logica nell'handler toggled dedicato (che chiama
    // già onColorTargetChanged), quindi per Background non rifacciamo nulla.
    connect(m_bgTargetGroup, &QButtonGroup::buttonClicked, this, [this](QAbstractButton *btn){
        if (btn == ui->radioBackground) return;
        onColorTargetChanged();
    });

    connect(m_modeGroup, &QButtonGroup::idClicked, this, [this](int id){
        // Click su un radio della superficie (Base/Phong/Wireframe).
        // NON tocchiamo il Background: è indipendente. Se il dock texture sta
        // attualmente editando lo sfondo (radioBackground acceso) lasciamo invariati
        // editor, checkbox e picker: continuano a riferirsi allo sfondo.
        //
        // m_savedRenderMode e' lo stato GLOBALE della superficie, quello che le
        // mesh senza modalita' propria ereditano e che il preset salva. Quando
        // si sta editando una SINGOLA mesh (spinbox diverso da "All") il click
        // riguarda solo quella parte, non la superficie intera: registrarlo
        // come globale lo faceva riapplicare a TUTTE le mesh al ricarico del
        // preset (sequenza: seleziono 5, Phong, wireframe, ricarico -> tutto
        // wireframe, perche' updateRenderState rileggeva m_savedRenderMode=2 e
        // col bypass del load lo scriveva sul globale).
        const bool editingSingleMesh =
            ui->glWidget && ui->glWidget->activeMeshPart() >= 0;
        if (!editingSingleMesh)
            m_savedRenderMode = id;

        if (!editingBackground()) {
            refreshTextureCheckbox();

            updateTextureUIState(m_surfaceTextureState);
            // LO SPEGNIMENTO PER WIREFRAME RIGUARDA SOLO L'AMBITO "ALL".
            // setGlobalTextureEnabled scrive m_textureEnabled, che e' lo stato GLOBALE
            // della texture di superficie: mettendo in wireframe UNA fascia si
            // spegneva la texture di tutta la superficie, e tornando su "All" il
            // checkbox rileggeva quello stato e la texture risultava persa.
            // Con una fascia selezionata la modalita' e' una proprieta' di
            // QUELLA parte (la scrive onUserRenderModeChosen su MeshPart), e il
            // render decide gia' da se' di non texturizzare una parte in
            // wireframe: non c'e' nulla da spegnere sul globale.
            applySurfaceTextureToEngine();

            // In Wireframe gli slider editano il colore uniforme delle linee: il pallino
            // di lavoro va su "Surface". updateTextureUIState sopra ha già spento Color1/2
            // (surfaceWireframe), qui assicuriamo che la tripla sia su Surface.
            if (id == 2 && ui->radioSurface->isEnabled()) {
                selectSurfaceColorTarget();
            }
            onColorTargetChanged();
        }

        updateFlatPreviewButton();
        updateRenderState();
        syncTextureTreeSelection();
        updateScriptButtonText();
    });

    // BACKGROUND: toggle indipendente. Acceso = il dock texture edita lo sfondo e
    // (se c'è codice) lo sfondo è attivo; spento = il dock torna a editare la
    // superficie. NON spegne né accende i radio Base/Phong/Wireframe: la modalità
    // di rendering della superficie resta quella che era.
    connect(ui->radioBackground, &QRadioButton::toggled, this, [this](bool checked){
        // Il clic: prima lo STATO (e la vista 2D del motore), che tutto il
        // resto del dock legge. Arriva solo dai clic: il programma scrive i
        // radio a segnali bloccati (setEditTarget).
        setEditTarget(checked ? EditTarget::Background : EditTarget::Surface);

        if (checked) {
            // ENTRO in editing sfondo. Lo stato della texture di superficie
            // (m_surfaceTextureState) non si tocca: il checkbox ne era solo la
            // vista, e ora mostra lo sfondo. (Prima lo si ricopiava DAL checkbox
            // per "salvarlo": in ambito Mesh il checkbox mostra la fascia, e la
            // copia accendeva una texture globale che non esisteva.)

            if (m_currentScriptMode == ScriptModeTexture) {
                // Niente da travasare: gli slot sono lo stato, l'editor la loro
                // vista -- updateScriptButtonText lo porta sullo sfondo. (Prima
                // qui l'editor si salvava nello slot della texture di superficie,
                // e in ambito "Mesh" ci finiva lo script della fascia.)
                ui->btnRunCurrentScript->setText("Run Background Texture");
                updateScriptButtonText();
            }

            bool bgTexActive = ui->glWidget->isBackgroundTextureEnabled();
            refreshTextureCheckbox();   // etichetta, abilitazione e spunta dello sfondo

            // Surface resta cliccabile anche in editing sfondo: la coppia Surface/Background
            // è l'asse di scelta della scena, quindi disabilitare Surface impedirebbe di
            // tornare alla superficie. Cliccare Surface esce da Background (esclusività di
            // m_bgTargetGroup) e il ramo "else" di questo handler ripristina il contesto
            // superficie. Niente disabilitazione di radioSurface.

            // Picker Colore attivi solo se lo sfondo è acceso E usa quel colore:
            // ciascuno indipendente (una texture che usa solo u_col1 non abilita col2).
            bool bgCol1 = bgTexActive && m_bgTextureCode.contains("u_col1");
            bool bgCol2 = bgTexActive && m_bgTextureCode.contains("u_col2");
            ui->radioTexColor1->setEnabled(bgCol1);
            ui->radioTexColor2->setEnabled(bgCol2);

            if (bgCol1 || bgCol2) {
                // Accendiamo il picker ABILITATO (col1 se usato, altrimenti col2).
                QRadioButton *target = bgCol1 ? ui->radioTexColor1 : ui->radioTexColor2;
                bool oldBlock2 = target->blockSignals(true);
                target->setChecked(true);
                target->blockSignals(oldBlock2);
            } else {
                // Sfondo senza colori: oltre a disabilitarli, DESELEZIONIAMO i color slot,
                // altrimenti un Color rimasto checked dalla superficie precedente mostra un
                // pallino fantasma (grigio ma acceso). uncheckInExclusiveGroup perché un
                // setChecked(false) diretto sull'unico acceso di un gruppo esclusivo è no-op.
                uncheckInExclusiveGroup(ui->radioTexColor1);
                uncheckInExclusiveGroup(ui->radioTexColor2);
            }

        }
        else {
            // ESCO dall'editing sfondo: il dock torna alla superficie. Lo sfondo
            // resta com'è a video (acceso o spento), spegnere il radio significa
            // solo "non sto più editando lo sfondo".
            if (m_currentScriptMode == ScriptModeTexture) {
                // USCENDO da Background il dock torna a mostrare cio' che il
                // bersaglio comanda: in ambito "Mesh" lo script della FASCIA,
                // altrimenti la texture di superficie (shownScriptSlot).
                ui->btnRunCurrentScript->setText("Run Surface Texture");
                updateScriptButtonText();
            }

            // radioSurface non viene più disabilitato entrando in Background,
            // quindi qui non c'è nulla da riabilitare.

            // Ripristino la checkbox e la sua etichetta.
            // AMBITO "MESH": il checkbox e' il DISPLAY della fascia, non lo
            // stato della texture GLOBALE (entrando in Background la selezione
            // della parte viene lasciata com'era apposta, vedi
            // updateMeshScopeEnabled, quindi uscendo siamo ancora su quella
            // fascia). Ripristinando m_surfaceTextureState si riaccendeva il
            // checkbox su una fascia a cui la texture era appena stata TOLTA.
            // Stesso criterio del ramo di ingresso, che per questo non salva
            // m_surfaceTextureState quando showingMeshTex.
            const bool onMeshScope = ui->glWidget
                                     && ui->glWidget->activeMeshPart() >= 0;
            const bool texOn = onMeshScope
                                   ? ui->glWidget->activeMeshTextureActive()
                                   : m_surfaceTextureState;

            refreshTextureCheckbox();

            updateTextureUIState(texOn);
            // Questo resta sul GLOBALE anche in ambito Mesh: e' lo stato della
            // texture di SUPERFICIE nel motore, che l'ambito non cambia -- la
            // fascia ha il proprio interruttore in MeshPart. Solo il DISPLAY
            // qui sopra segue la parte.
            applySurfaceTextureToEngine();

            // Uscendo da Background torniamo a editare la superficie: updateTextureUIState
            // sopra ha già messo il target colore su Surface (selectSurfaceColorTarget).
            // onColorTargetChanged() finale allinea gli slider.
        }

        onColorTargetChanged();
        updateFlatPreviewButton();
        updateRenderState();
        // Entrando in Background il selettore All/Mesh non ha senso (lo sfondo
        // non ha fasce): si disabilita e l'ambito torna ad "All", cosi' i
        // comandi non restano dirottati su una mesh. Uscendo si riabilita da
        // solo, se la superficie e' multi-mesh.
        updateMeshScopeEnabled();
        syncTextureTreeSelection();
        updateScriptButtonText();
    });

    refreshRenderRadios();

    // I radio sono DISPLAY (mostrano la modalita' della mesh selezionata) e
    // COMANDO (l'utente li clicca). Solo il secondo caso deve scrivere una
    // modalita' PROPRIA sulla parte attiva: quando e' syncAppearanceControls-
    // ToActiveMesh a muoverli, m_syncingMeshControls e' vero e qui non si scrive
    // nulla. Senza questa distinzione ogni updateRenderState (cambio tab,
    // proiezione, load) riapplicava il valore a un destinatario variabile ed era
    // la causa del wireframe che si propagava a tutte le mesh.
    auto onRenderRadioToggled = [this](bool checked){
        if (!checked) return;                 // interessa solo chi si accende
        if (!m_syncingMeshControls) onUserRenderModeChosen();
        updateRenderState();
    };
    connect(ui->radioBasic, &QRadioButton::toggled, this, onRenderRadioToggled);
    connect(ui->radioPhong, &QRadioButton::toggled, this, onRenderRadioToggled);
    connect(ui->radioWF,    &QRadioButton::toggled, this, onRenderRadioToggled);

    // Stato iniziale dei controlli densità Wireframe: al boot la modalità è Base, quindi
    // vanno disabilitati. updateRenderState (che li gestisce) non viene chiamato a tempo
    // di costruzione — solo dagli handler dei radio dopo un click — e setChecked(true) su
    // radioBasic sopra non emette toggled finché le connect non sono in piedi: senza questo
    // restavano attivi (stato di default della .ui) finché non si cliccava un radio.
    ui->uDensity->setEnabled(false);
    ui->vDensity->setEnabled(false);

    // (Qui c'era un secondo gestore di radioWF, che spegneva a mano il
    // checkbox Texture. Il checkbox e' una vista: lo allinea
    // refreshTextureCheckbox, chiamata da updateRenderState qui sopra.)

    ui->alphaSlider->setRange(0, 100);
    ui->alphaSlider->setValue(100);
    alphaValue = 1.0f;
    ui->glWidget->setAlpha(alphaValue);
    ui->lblAlphaVal->setText("1.00");

    connect(ui->alphaSlider, &QSlider::valueChanged, this, [this](int value){
        // Su campo implicito a PRODOTTO ("Chain") la trasparenza fa sparire la superficie.
        // Se e' l'UTENTE ad abbassare lo slider (non un set programmatico di caricamento
        // preset), ripristiniamo l'opacita', mostriamo il popup UNA volta e blocchiamo.
        if (!m_settingAlphaProgrammatic && value < 100) {
            bool isImplicitMode = (implicitMode());
            bool illImplicit = isImplicitMode && ui->glWidget && ui->glWidget->isImplicitIllConditioned();
            if (illImplicit) {
                onAlphaSliderMovedIllCheck(value);
                return;   // onAlphaSliderMovedIllCheck ha gia' rimesso alpha a 100
            }
            // SOLO ANDROID: superficie la cui trasparenza puo' degradare (Gyroid, script
            // RM). NON blocchiamo: mostriamo l'avviso una volta e proseguiamo applicando
            // l'alpha normalmente (lo slider agisce, l'utente vede l'effetto reale).
            bool warnImplicit = isImplicitMode && ui->glWidget && ui->glWidget->implicitTransparencyMayDegrade();
            if (warnImplicit) {
                onAlphaSliderMovedWarnCheck();   // no return: l'alpha si applica sotto
            }
            // CONFERMA MISURATA (tutte le piattaforme): se la GPU e' GIA' sotto
            // carico pesante con la scena opaca (EMA del watchdog, significativa
            // solo ad animazione in corso), il ramo trasparente (~4-12x il costo
            // per pixel) porta quasi certamente al collasso, che il watchdog
            // fermerebbe solo DOPO il magenta. Chiediamo QUI, prima che il primo
            // frame trasparente venga renderizzato. Sulle scene fluide o ferme
            // non scatta mai (renderingUnderHeavyLoad e' false a riposo).
            if (isImplicitMode && !m_alphaHeavyWarnShown &&
                ui->glWidget && ui->glWidget->renderingUnderHeavyLoad()) {
                // Eventi della STESSA presa arrivati col box gia' aperto, o dopo
                // un "Keep it opaque": riassorbiti a 100 senza riaprire nulla,
                // altrimenti il drag ancora in corso impila/riapre il box in
                // loop (finestra che "permane o ricompare"). Una nuova presa
                // riarma via sliderPressed; il cambio superficie via sync.
                if (m_alphaHeavyPopupActive || m_alphaHeavyDeclined) {
                    m_settingAlphaProgrammatic = true;
                    ui->alphaSlider->setValue(100);
                    m_settingAlphaProgrammatic = false;
                    return;
                }
                m_alphaHeavyPopupActive = true;
                // Slider subito a 100 PRIMA del box modale: mentre e' aperto
                // nessun frame trasparente parte (pattern di onAlphaSliderMovedIllCheck).
                m_settingAlphaProgrammatic = true;
                ui->alphaSlider->setValue(100);
                m_settingAlphaProgrammatic = false;

                QMessageBox box(this);
                box.setIcon(QMessageBox::Warning);
                box.setWindowTitle(tr("Transparency on a heavy scene"));
                box.setText(tr("The GPU is already under heavy load with this "
                               "animation."));
                box.setInformativeText(tr("Transparency multiplies the per-pixel "
                                          "cost of ray marching and would very "
                                          "likely make rendering collapse (on "
                                          "some devices it can freeze the "
                                          "application).\n\n"
                                          "You can apply it anyway at your own "
                                          "risk, or keep the surface opaque."));
                QPushButton *applyBtn  = box.addButton(tr("Apply anyway"), QMessageBox::AcceptRole);
                QPushButton *opaqueBtn = box.addButton(tr("Keep it opaque"), QMessageBox::RejectRole);
                box.setDefaultButton(opaqueBtn);
                box.exec();
                if (box.clickedButton() == applyBtn) {
                    // Conferma data: applichiamo il valore richiesto in modo
                    // programmatico (i check di questa invocazione sono gia'
                    // stati fatti) e non richiediamo piu' per questa superficie.
                    m_alphaHeavyWarnShown = true;
                    m_settingAlphaProgrammatic = true;
                    ui->alphaSlider->setValue(value);
                    m_settingAlphaProgrammatic = false;
                } else {
                    // "Keep it opaque": vale per TUTTO il gesto in corso — gli
                    // eventi residui della presa vengono riassorbiti sopra.
                    m_alphaHeavyDeclined = true;
                }
                m_alphaHeavyPopupActive = false;
                return;  // in entrambi i casi il set giusto e' gia' avvenuto sopra
            }
        }
        alphaValue = static_cast<float>(value) / 100.0f;
        ui->lblAlphaVal->setText(QString::number(alphaValue, 'f', 2));
        ui->glWidget->setAlpha(alphaValue);
    });

    // Nuova presa dello slider trasparenza: il "Keep it opaque" dato durante la
    // presa precedente valeva per QUEL gesto, non per sempre — riarma la conferma.
    // Riarma anche il watchdog: se era stato zittito perche' avevamo disattivato
    // la trasparenza (guardTransparencyOnDisplacementApply -> ack), toccare di
    // nuovo lo slider e' l'atto esplicito dopo cui il watchdog deve tornare a
    // vigilare (setAlpha fa solo update(), NON passa da rebuildShader che
    // altrimenti lo riarmerebbe). E' il "a meno che non riporti alpha<1".
    connect(ui->alphaSlider, &QSlider::sliderPressed, this, [this](){
        m_alphaHeavyDeclined = false;
        if (ui->glWidget) ui->glWidget->rearmPerformanceWarning();
    });

    connect(ui->chkBoxTexture, &QCheckBox::toggled, this, [this](bool checked){
        // Il checkbox MOSTRA o NASCONDE una texture: non ne scrive nessuna, e
        // non e' lavoro da proteggere (segnalato dall'utente: l'avviso usciva
        // su una texture mai toccata). Cio' che cambia qui dentro entra quindi
        // anche nei riferimenti puliti. Non durante load e reset, che segnano
        // il loro momento pulito all'uscita.
        std::optional<AbsorbChangesGuard> absorbToggle;
        if (m_uiReady) absorbToggle.emplace(this);
        // ==========================================================
        // TEXTURE DELLA SOLA MESH SELEZIONATA
        // ==========================================================
        // Con l'ambito su "Mesh" e una parte attiva, il checkbox accende o
        // spegne la texture di QUELLA parte, come fanno colore/alpha/luce.
        // Va PRIMA di tutto il resto: il ramo sotto scrive lo stato GLOBALE
        // (setGlobalTextureEnabled + generateTexture), che finisce nell'UBO di ogni
        // parte e texturizzava l'intera superficie -- oltre a far diventare
        // bianche le mesh in wireframe, che venivano disegnate col colore
        // della texture invece che col proprio.
        // In "All" (nessuna parte attiva) si prosegue col percorso di sempre.
        // Escluso il ramo Background (la texture di sfondo non e' per-mesh) e
        // il Ray Marching, che non ha parti di mesh.
        if (!editingBackground()
            && !implicitMode()
            && ui->glWidget && ui->glWidget->activeMeshPart() >= 0) {

            // Codice da usare per QUESTA parte: quello che ha gia' (riaccensione)
            // oppure la scacchiera di default, cosi' accendere il checkbox
            // produce sempre qualcosa di visibile come sulla superficie intera.
            QString code = ui->glWidget->activeMeshTextureCode();
            if (checked && code.trimmed().isEmpty())
                code = defaultMeshTextureCode();

            // COLORI DELLA TEXTURE ALLA GPU. La scacchiera di default e' tutta
            // costruita su u_col1/u_col2 (mix dei due), che arrivano dall'UBO
            // via setGlobalTextureColors: senza questa riga i due slot restano ai
            // valori stantii del contesto precedente e, se coincidono, la
            // scacchiera esce in TINTA UNITA (il caso "tutto bianco").
            // Il ramo globale del checkbox fa lo stesso poco piu' sotto: qui
            // serve perche' quel ramo non viene percorso.
            // I colori di default valgono solo per una texture NUOVA. Se la
            // parte ne aveva già di propri (riaccensione, o rientro su una mesh
            // già texturizzata) vanno RIPRISTINATI i suoi: forzarli a
            // verde/nero qui riscriveva i colori di una mesh già configurata
            // solo perché la si era riselezionata.
            if (checked) {
                const auto &cparts = ui->glWidget->getEngine()->getMeshParts();
                const int ci = ui->glWidget->activeMeshPart();
                const MeshPart *cp = (ci >= 0 && ci < (int)cparts.size()) ? &cparts[ci] : nullptr;
                // I due slot GLOBALI restano quelli della texture di superficie:
                // qui si sta configurando una fascia, e i colori vanno nella
                // parte. setActiveMeshTexture poco sotto li copierebbe dai
                // globali solo a una parte che non ne ha di propri.
                if (!(cp && cp->hasCustomTexColors()))
                    ui->glWidget->setActiveMeshTexColors(QColor::fromRgbF(0.20f, 0.80f, 0.20f), Qt::black);
            }

            // ACCENSIONE: applica lo script alla parte (via di COMANDO).
            // SPEGNIMENTO: si spegne soltanto, CONSERVANDO lo script --
            // setActiveMeshTexture e' la via di comando e riscriverebbe
            // textureCode forzando hasCustomTexture=true, cioe' rimetterebbe la
            // texture "in vita" proprio mentre la si sta spegnendo: l'editor
            // continuava a mostrarne lo script (il display guarda la texture
            // EFFICACE, e quella restava dichiarata).
            if (checked) ui->glWidget->setActiveMeshTexture(code, true);
            else         ui->glWidget->setActiveMeshTextureEnabled(false);

            // OROLOGIO DELLA TEXTURE. Accendere la texture di una mesh e' un
            // avvio esplicito del modulo, come il Run: riarma un eventuale stop
            // manuale e rivaluta se il clock serve. Il conto va fatto DOPO
            // setActiveMeshTexture, perche' allSurfaceTextureCode() legge lo
            // stato appena scritto sulla parte.
            m_userStoppedTexClock = false;
            ui->glWidget->setSurfaceTextureAnimating(
                hasTimeVariable(allSurfaceTextureCode()));

            // OROLOGIO DELLA PARTE. Ogni fascia ha il PROPRIO clock: accendere
            // solo quello globale (riga sopra) non la muove piu'. Senza questa
            // riga, riaccendendo il checkbox la texture tornava FERMA.
            // Solo se il suo script usa il tempo: una texture statica non deve
            // lasciare acceso un orologio a vuoto.
            if (checked) {
                m_userStoppedMeshTexClock = false;   // comando esplicito
                ui->glWidget->setActiveMeshTextureAnimating(hasTimeVariable(code));
            }

            // I picker Color 1/2 servono solo se lo script della parte li usa.
            updateTextureUIState(checked, true);
            updateFlatPreviewButton();

            // EDITOR E TASTI RIALLINEATI SUBITO. Questo ramo esce con return e
            // non passava dal display per-mesh: spegnendo il checkbox l'editor
            // restava con lo script della texture appena spenta, e si svuotava
            // solo al PRIMO EVENTO SUCCESSIVO che risincronizza (il "giro di
            // ritardo": serviva spegnere due volte perche' sparisse).
            // syncAppearanceControlsToActiveMesh e' il punto unico del display
            // per-mesh e decide da solo cosa mostrare, in base alla texture
            // EFFICACE della parte.
            syncAppearanceControlsToActiveMesh();
            updateMasterButtonState();

            ui->glWidget->update();
            return;
        }

        if (editingBackground()) {
            ui->glWidget->setBackgroundTextureEnabled(checked);
            // Picker Colore solo se lo sfondo è acceso E usa quel colore (indipendenti).
            bool bgCol1 = checked && m_bgTextureCode.contains("u_col1");
            bool bgCol2 = checked && m_bgTextureCode.contains("u_col2");
            ui->radioTexColor1->setEnabled(bgCol1);
            ui->radioTexColor2->setEnabled(bgCol2);

            if ((bgCol1 || bgCol2) && !ui->radioTexColor1->isChecked() && !ui->radioTexColor2->isChecked()) {
                QRadioButton *target = bgCol1 ? ui->radioTexColor1 : ui->radioTexColor2;
                bool oldBlock = target->blockSignals(true);
                target->setChecked(true);
                target->blockSignals(oldBlock);
            }

            if (!checked) {
                // Codice, percorso dell'immagine, ancora e GPU insieme: la
                // riaccensione riparte sempre dalla default, e il Save non trova
                // piu' il percorso dell'immagine appena tolta (lo scriveva come
                // //IMG: in un record con lo sfondo spento).
                forgetBackgroundTexture();

                // Lo slot dello sfondo e' vuoto (forgetBackgroundTexture): se
                // il dock lo sta mostrando, editor e tasti lo seguono.
                updateScriptButtonText();
            }

            onColorTargetChanged();
        }
        else {
            // CANCELLAZIONE SCRIPTS CON WARNING (SURFACE TEXTURE)
            if (!checked && !m_blockTextureGen) {
                // 1. Verifichiamo se c'è effettivamente del codice che andrebbe perso
                bool hasCode = false;
                bool isModified = this->property("isTextureModified").toBool();

                if (implicitMode()) { // Ray Marching
                    QString tex = m_rm.texture.trimmed();
                    QString disp = m_rm.displacement.trimmed();

                    // Ignoriamo le texture di DEFAULT generate automaticamente (non sono
                    // codice scritto dall'utente da proteggere): la scacchiera procedurale
                    // attuale e il vecchio Triplanar Mapping (per record/preset salvati prima).
                    bool isAutoDefault = tex.contains("sin(pModel.x * 20.0) * sin(pModel.y * 20.0)")
                                         || tex.contains("vec3 blend = abs(n_model);");
                    if (!tex.isEmpty() && !isAutoDefault) hasCode = true;
                    if (!disp.isEmpty()) hasCode = true;

                } else { // Parametrica
                    QString tex = m_surfaceTextureCode.trimmed();

                    // Se c'è SOLO un tag immagine (es. //IMG:/percorso.png) senza logica a capo, ignoralo
                    bool isOnlyImage = tex.startsWith("//IMG:") && !tex.contains("\n");
                    if (!tex.isEmpty() && !isOnlyImage) hasCode = true;
                }

                // Cancellazione fisica del codice texture in memoria. DEVE avvenire
                // a OGNI spegnimento (non solo quando si modifica a mano), altrimenti
                // riaccendendo il checkbox il rebuildShader/generateTexture riusa il
                // codice ancora in memoria e ricompare l'ULTIMA texture caricata invece
                // della default. Il warning sotto decide solo se CHIEDERE conferma
                // (quando c'è codice scritto a mano che andrebbe perso).
                auto clearTextureMemory = [this]() {
                    if (implicitMode()) {
                        // Svuota i campi della tab Ray Marching
                        setRmText(&ImplicitTexts::texture, QString());
                        setRmText(&ImplicitTexts::displacement, QString());
                        if (ui->glWidget) {
                            ui->glWidget->setTextureCode("");
                            ui->glWidget->setDisplacementCode("");
                            // Anche l'immagine, come nel ramo parametrico: il
                            // campo non la nomina piu', e restava in GPU.
                            ui->glWidget->clearTexture();
                        }
                    } else {
                        // Svuota memoria e variabili parametriche (il motore
                        // torna allo shader standard)
                        commitSurfaceTextureCode(QString());
                        // Lo script con lei: l'editor, se lo mostra, lo segue.
                        setScriptText(SlotSurfaceTexture, QString());

                        if (ui->glWidget) ui->glWidget->clearTexture();
                    }
                    // TRASFORMAZIONE 2D: va azzerata insieme al codice. Zoom/pan/
                    // rotazione sono di MODULO, non della singola texture: restando
                    // in piedi, la scacchiera di default che ricompare alla
                    // riaccensione veniva disegnata attraverso l'inquadratura della
                    // texture precedente. Col record "Dynamic Mobius Band" (che salva
                    // zoom 9.36 per il proprio Shadertoy) la default si vedeva come un
                    // quadrato 2x2 invece che come la griglia 8x8; con uno zoom < 1
                    // salvato altrove appariva invece rimpicciolita. Stesso motivo per
                    // cui qui si azzerano gia' i colori texture nei due rami piu' sotto.
                    // setGlobalTexTransform allinea da se' anche il buffer di lavoro
                    // della vista 2D (in ambito "All"), che e' cio' che lo shader legge.
                    if (ui->glWidget)
                        ui->glWidget->setGlobalTexTransform(1.0f, QVector2D(0.0f, 0.0f), 0.0f);

                    // Codice perso/azzerato: lo stato "modificato" non ha più senso.
                    this->setProperty("isTextureModified", false);
                };

                // MOSTRA IL WARNING SOLO SE C'È VERO CODICE *E* L'UTENTE LO HA MODIFICATO MANUALMENTE
                if (hasCode && isModified) {
                    // Stesso popup di ogni altra uscita senza salvare (Save /
                    // Don't save / Cancel): prima era un Si'/No che poteva solo
                    // buttare via il lavoro. Uniformita' dell'interfaccia, e in
                    // piu' qui si puo' finalmente salvare invece di perdere tutto.
                    if (!confirmDiscardUnsaved(ScopeTexture)) {
                        // Cancel: la texture resta accesa, e il checkbox -- la
                        // sua vista -- torna a dirlo.
                        refreshTextureCheckbox();
                        return; // Interrompe l'operazione
                    }

                    // Save o Don't save: in entrambi i casi si prosegue e si
                    // azzera. Dopo Save il codice e' su disco, quindi non si
                    // perde nulla; dopo Don't save la perdita e' voluta.
                    clearTextureMemory();
                } else {
                    // Nessun codice scritto a mano da proteggere (es. texture solo
                    // CARICATA da preset): niente warning, ma azzeriamo lo stesso così
                    // la riaccensione riparte sempre dalla texture di default.
                    clearTextureMemory();
                }
            }

            m_surfaceTextureState = checked;
            updateTextureUIState(checked);
            applySurfaceTextureToEngine();

            if (!m_blockTextureGen && checked) {
                // --- LOGICA RAY MARCHING (Tab 1) ---
                if (implicitMode()) {
                    QString currentTex = m_rm.texture.trimmed();

                    if (currentTex.isEmpty()) {
                        // Reset dei colori texture alla default: senza questo, dopo
                        // una texture RM con colori custom (u_col1/u_col2), la default
                        // ricompariva con i colori della precedente (l'UBO restava
                        // stantio). Simmetrico al ramo parametrico.
                        if (ui->glWidget)
                            ui->glWidget->setGlobalTextureColors(QColor::fromRgbF(0.20f, 0.80f, 0.20f), Qt::black);

                        // Default RM = scacchiera PROCEDURALE pilotata da u_col1/u_col2
                        // (come il preset "Checkboard"): niente immagine, così i picker
                        // Color 1/2 sono attivi sulla texture di default. Contratto texture
                        // RM: assegnare textureCol usando pModel e ubuf.u_col1/u_col2.
                        QString defaultRM =
                            "float pattern = sin(pModel.x * 20.0) * sin(pModel.y * 20.0);\n"
                            "if (pattern > 0.0) {\n"
                            "    textureCol = ubuf.u_col1;\n"
                            "} else {\n"
                            "    textureCol = ubuf.u_col2;\n"
                            "}";

                        // A segnali bloccati: questa e' la texture DI DEFAULT che
                        // l'accensione della checkbox fa comparire, non codice
                        // scritto dall'utente. Senza il blocco passerebbe da
                        // markRmTextureEdited come una digitazione. (Che non sia
                        // lavoro da salvare lo garantisce AbsorbChangesGuard, in
                        // cima a questo gestore.)
                        {
                            setRmText(&ImplicitTexts::texture, defaultRM);
                        }
                        if (ui->glWidget) ui->glWidget->setTextureCode(defaultRM);

                        // Texture procedurale, non immagine: l'immagine se n'e'
                        // gia' andata dalla GPU allo spegnimento (clearTextureMemory).

                        // Texture colorata appena attivata: il pallino dei Color va su
                        // Color 1 (resetColorTargetToFirst), la tripla resta dov'è
                        // (gruppi indipendenti). updateTextureUIState chiama già
                        // onColorTargetChanged. DEVE stare DOPO aver impostato lineTexture:
                        // activeTextureUsesColors() in RM legge u_col1/u_col2 da lineTexture.
                        updateTextureUIState(true, true);
                    }
                }
                // --- LOGICA PARAMETRICA (Tab 0) ---
                else {
                    // Niente immagine: generateTexture() qui sotto mette la
                    // scacchiera nel sampler al suo posto.

                    // Stacco dello SHADER PROCEDURALE residuo. A differenza dello sfondo
                    // (vedi setBackgroundTexture("background.png") al suo spegnimento), la
                    // texture di superficie parametrica e' uno shader custom applicato via
                    // loadCustomShader: se la superficie PRECEDENTE aveva una texture
                    // procedurale, quello shader resta agganciato -> riappare la vecchia
                    // texture senza animazione / coi colori falsati. Azzeriamo il codice in
                    // memoria e ripristiniamo lo shader standard prima di applicare la
                    // default (applyDefaultCheckerShader piu' sotto).
                    // NB: il bug residuo era SOLO sulle parametriche (le implicite
                    // ricompilano la texture nello shader SDF a ogni Run).
                    commitSurfaceTextureCode(QString());   // torna allo shader standard
                    setScriptText(SlotSurfaceTexture, QString());

                    // Reset dei colori texture alla default. Senza questo restano
                    // quelli del preset precedente e la scacchiera default
                    // ricompare coi colori vecchi. Stesso reset del ramo Ray Marching.
                    if (ui->glWidget)
                        ui->glWidget->setGlobalTextureColors(QColor::fromRgbF(0.20f, 0.80f, 0.20f), Qt::black);

                    // Texture colorata appena attivata: il pallino dei Color va su Color 1
                    // (resetColorTargetToFirst); la tripla resta dov'è (gruppi indipendenti).
                    updateTextureUIState(true, true);

                    generateTexture();
                    // Il visual della default e' lo shader procedurale, non l'immagine
                    // appena caricata (vedi applyDefaultCheckerShader). Se la bake
                    // fallisse resta lo shader standard -> fallback sull'immagine.
                    applyDefaultCheckerShader();
                }

                if (ui->glWidget) ui->glWidget->rebuildShader();
            }
        }

        updateFlatPreviewButton();

        // COSTANTI: spegnere una texture (di superficie o di sfondo) puo' far
        // cadere in disuso una lettera, accenderla sulla default pure. Senza
        // ricalcolo lo slider restava acceso col valore di prima, a muovere nulla.
        refreshConstants();

        // --- GESTIONE AUTOMATICA ANIMAZIONE (START/STOP) SICURA ---
        bool needsAnim = false;

        // 1. Controllo Equazioni Base
        if (implicitMode()) { // Ray Marching
            // Solo l'SDF (lineEquation/script) è geometria; il displacement
            // (lineVariations) è del modulo texture ed è controllato al punto 2.
            QString eq = activeImplicitEquationText() + " " + m_surfaceScriptApplied;
            if (hasTimeVariable(eq)) needsAnim = true;
        } else { // Parametrica
            // Includere i campi Composition (lineU/lineV/lineW) e i vincoli espliciti:
            // 't' può vivere SOLO lì (es. U(u,v)=u+t*D) mentre X/Y/Z/P ne sono privi.
            // Senza questi, accendere/spegnere la texture ricalcolava needsAnim=false
            // e fermava per errore l'animazione della geometria. Stesso insieme di
            // rawEqsForT (onStartClicked) e mainEq (updateMasterButtonState).
            QString eq = m_eq.x + " " + m_eq.y + " " +
                         m_eq.z + " " + m_eq.p + " " +
                         m_eq.u + " " + m_eq.v + " " + m_eq.w + " " +
                         m_eq.explicitU + " " + m_eq.explicitV + " " + m_eq.explicitW + " " +
                         m_surfaceScriptApplied;
            if (hasTimeVariable(eq)) needsAnim = true;
        }

        // 2. Controllo Texture Superficie (SOLO SE ABILITATA)
        // MULTI-MESH: il modulo e' attivo, e il suo 't' va cercato, anche quando
        // la texture ce l'ha solo una FASCIA. Qui si guardava m_surfaceTextureCode
        // (la sola texture globale) con un gate sul checkbox / su
        // m_surfaceTextureState: con l'animazione sulla fascia 1 e nessuna
        // texture globale, needsAnim usciva falso e il ramo sotto chiamava
        // applyAnimationState(false), fermando l'animazione.
        // Si vedeva SOLO applicando in Background la texture di DEFAULT: le
        // altre (script o immagine) portano un proprio codice e passano da
        // percorsi che non ricalcolano needsAnim, mentre la default arriva qui.
        bool isSurfTexActive = editingBackground() ? m_surfaceTextureState : checked;
        if (!isSurfTexActive) isSurfTexActive = anyMeshTextureActive();
        if (isSurfTexActive) {
            QString tex = (implicitMode())
                    ? (m_rm.texture + m_rm.displacement)
                    : allSurfaceTextureCode();
            if (hasTimeVariable(tex)) needsAnim = true;
        }

        // 3. Controllo Texture Sfondo (SOLO SE ABILITATA)
        if (ui->glWidget && ui->glWidget->isBackgroundTextureEnabled()) {
            if (hasTimeVariable(m_bgTextureCode)) needsAnim = true;
        }

        // 4. APPLICAZIONE STATO E AGGIORNAMENTO UI
        if (needsAnim) {
            if (m_btnStart && m_btnStart->text() != "START") {
                applyAnimationState(true);
            }
        } else {
            applyAnimationState(false);
        }

        if (ui->glWidget) ui->glWidget->update();
    });

    // Avviso di rallentamento: il GLWidget segnala quando il rendering resta
    // sotto soglia troppo a lungo. PRIMA fermiamo l'animazione, POI avvisiamo:
    // col collasso in corso (frame da secondi) il popup modale restava sepolto
    // dietro il rendering e l'utente non riusciva nemmeno a premere "Stop"
    // (visto su iPhone: magenta + app di fatto inutilizzabile). Fermare subito
    // libera GPU e GUI thread, la finestra appare ed e' reattiva; chi vuole fa
    // ripartire tutto dal popup (equivale a un master Start).
    // QueuedConnection: il segnale parte dal thread di rendering del QRhiWidget,
    // il QMessageBox deve invece girare nel thread GUI.
    // SHADER CHE NON COMPILA: la superficie SPARISCE (buildPipeline azzera le
    // pipeline) e finora l'unica traccia era un qWarning sulla console, che
    // l'utente non vede -- restava solo lo schermo vuoto, senza spiegazione.
    // Caso tipico: due texture per-mesh i cui script dichiarano lo stesso
    // simbolo. Il generatore rinomina per-mesh le forme note (#define, funzioni,
    // globali, struct), ma uno script NUOVO puo' sempre introdurne una non
    // prevista: qui l'utente almeno legge QUALE simbolo e' in conflitto.
    connect(ui->glWidget, &GLWidget::shaderCompilationFailed, this,
            [this](const QString &err) {
        // Un popup alla volta. La sorgente ne emette gia' UNO solo per errore
        // (m_shaderErrorReported in GLWidget), ma la guardia resta come rete:
        // il segnale e' Queued e il box e' asincrono, quindi due errori diversi
        // in rapida successione potrebbero comunque accavallarsi.
        if (m_shaderErrorPopupActive) return;
        m_shaderErrorPopupActive = true;

        // BOX ASINCRONO, non exec(): un QMessageBox modale gira un event loop
        // ANNIDATO, e i segnali consegnati li' dentro riaprivano il popup sopra
        // se stesso -- la raffica che ha reso necessaria l'uscita forzata.
        // open() ritorna subito e l'applicazione resta utilizzabile.
        auto *box = new QMessageBox(this);
        box->setIcon(QMessageBox::Warning);
        box->setWindowTitle(tr("Shader Compilation Failed"));

        // TESTO CORTO in setText, RESTO in setInformativeText: QMessageBox tratta
        // il testo principale come un titolo e NON lo manda a capo, allargando la
        // finestra quanto serve a contenerlo su una riga. Con la spiegazione
        // intera li' dentro il box arrivava a occupare tutto lo schermo (visto su
        // iPhone). L'informativeText invece va a capo da solo: e' lo stesso
        // schema del box "Transparency on a heavy scene", che infatti si e'
        // sempre visto di dimensioni normali.
        const QString detail = err.trimmed();

        // Una RISORSA di shader mancante non e' un errore dell'utente: il testo
        // sulle texture in conflitto sarebbe una spiegazione falsa, e manderebbe
        // a cercare un problema inesistente nei propri script. bakeShader in quel
        // caso confeziona gia' il messaggio giusto (riconoscibile dal prefisso),
        // che qui si mostra al posto di quello standard.
        if (detail.startsWith(QLatin1String("Internal error:"))) {
            box->setText(tr("The application is missing part of its built-in data."));
            box->setInformativeText(detail);
        } else {
            box->setText(tr("The surface was not updated: what you see is the "
                            "last valid image."));
            box->setInformativeText(
                tr("The shader could not be compiled.\n\n"
                   "If you have just applied a texture to a mesh, its script may "
                   "declare a symbol (a function, a #define, a global variable) "
                   "with the same name as another mesh's texture.\n\n"
                   "To recover, turn that texture off on the mesh, or load a "
                   "different one."));
            // Il log del compilatore e' lungo e a righe fisse: sta nei dettagli,
            // dove ha una sua area con scorrimento invece di allargare il box.
            if (!detail.isEmpty()) box->setDetailedText(detail);
        }

        box->setStandardButtons(QMessageBox::Ok);
        box->setAttribute(Qt::WA_DeleteOnClose);
        connect(box, &QDialog::finished, this,
                [this](int){ m_shaderErrorPopupActive = false; });
        box->open();
    }, Qt::QueuedConnection);

    // IMMAGINE DI TEXTURE NON DECODIFICABILE.
    // Il file c'e' ed e' leggibile (validato prima del caricamento) ma Qt non ne
    // ricava pixel: formato non supportato o file corrotto. Prima
    // loadTextureFromFile usciva in silenzio e a schermo restava la texture
    // PRECEDENTE, che sembrava "la texture di default del preset": nessun
    // indizio della causa. Box asincrono per gli stessi motivi del segnale
    // gemello qui sopra (niente event loop annidato).
    connect(ui->glWidget, &GLWidget::textureImageLoadFailed, this,
            [this](const QString &path) {
        if (m_textureImageErrorPopupActive) return;
        m_textureImageErrorPopupActive = true;

        auto *box = new QMessageBox(QMessageBox::Warning, "Image Not Loaded",
            "This image could not be loaded, so the previous texture is still "
            "showing:\n\n" + path + "\n\nThe file exists but is not a readable "
            "image: it may be corrupted, or in a format that is not supported.",
            QMessageBox::Ok, this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        connect(box, &QDialog::finished, this,
                [this](int){ m_textureImageErrorPopupActive = false; });
        box->open();
    }, Qt::QueuedConnection);

    connect(ui->glWidget, &GLWidget::performanceWarning, this, [this]() {
        // Un popup alla volta: eventuali segnali gia' in coda quando il box e'
        // aperto (peggioramenti misurati prima del nostro stop) non devono
        // aprirne altri. E se il master e' gia' fermo (stop manuale arrivato
        // prima della consegna del segnale in coda) l'avviso e' stantio.
        //
        // m_transparencyGuardActive: una guardia trasparenza (interattiva o
        // post-load) sta gestendo il caso o ha il suo popup aperto. Il segnale
        // del watchdog qui e' STANTIO — era stato emesso (QueuedConnection) sui
        // primi frame trasparenti PRIMA che la guardia mettesse alpha a 1 e
        // chiamasse acknowledgePerformanceWarning(); l'ack blocca le emissioni
        // FUTURE ma non questa gia' in coda. Scartarlo evita il popup del
        // watchdog SOPRA la finestra della guardia (la confusione segnalata).
        if (m_perfPopupActive || m_masterStopped || m_transparencyGuardActive) return;
        m_perfPopupActive = true;

        // Lo stop e la trasparenza tolta qui sotto li decide l'app, non
        // l'utente: non sono lavoro da salvare.
        AbsorbChangesGuard absorbWatchdog(this);

        performMasterStop();

        // Allo stop da collasso riportiamo anche la trasparenza a 1 (solo Ray
        // Marching): il ramo trasparente e' il moltiplicatore di costo del
        // marcher (MAX_FACES x 3 marchNextLayer per pixel), e con alpha<1 OGNI
        // ridisegno (slider, drag, resize) restava da secondi anche a scena
        // ferma — lo stop toglie il moto, non il costo per pixel. Opaco costa
        // una frazione: l'interfaccia torna reattiva subito, popup compreso.
        // setValue(100) passa dall'handler valueChanged (value==100: nessun
        // check ill/warn) e riallinea slider, label e GLWidget in un colpo.
        // Sul parametrico la trasparenza non e' il collo di bottiglia: non si
        // tocca. Se l'utente sceglie "Restart animation" l'alpha ORIGINALE
        // viene ripristinato prima del riavvio (prosegue a suo rischio con la
        // scena identica a prima); con "Keep it stopped" resta opaco.
        bool alphaReset = false;
        int  alphaPrev  = 100;
        const bool perfIsImplicit = (implicitMode());
        if (perfIsImplicit && ui->alphaSlider->value() < 100) {
            alphaPrev = ui->alphaSlider->value();
            ui->alphaSlider->setValue(100);
            alphaReset = true;
        }

        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Animation stopped"));
        box.setText(tr("The animation was stopped because rendering was "
                       "slowing down dangerously."));
        QString info = tr("Pushing the complexity further (steps, effects, "
                          "resolution) may degrade the image or, on some "
                          "devices, freeze the application.\n\n"
                          "You can restart the animation at your own risk, "
                          "or leave it stopped and reduce some parameters "
                          "first.");
        if (alphaReset)
            info += tr("\n\nTransparency has been set to fully opaque to keep "
                       "the app responsive while stopped. Restarting the "
                       "animation restores your transparency setting.");
        box.setInformativeText(info);
        QPushButton *resumeBtn = box.addButton(tr("Restart animation"), QMessageBox::AcceptRole);
        QPushButton *stayBtn   = box.addButton(tr("Keep it stopped"), QMessageBox::RejectRole);
        box.setDefaultButton(stayBtn);
        box.exec();
        if (box.clickedButton() == resumeBtn) {
            // Ripristina la trasparenza originale PRIMA del riavvio, cosi' la
            // scena riparte identica a com'era. Set programmatico: il flag salta
            // i check ill/warn dell'handler (gia' assolti quando l'utente aveva
            // abbassato lo slider la prima volta).
            if (alphaReset) {
                m_settingAlphaProgrammatic = true;
                ui->alphaSlider->setValue(alphaPrev);
                m_settingAlphaProgrammatic = false;
            }
            // Riavvio = vero master Start (il gestore riconosce sender()==m_btnStart:
            // riarma i flag user-stop e riparte il moto camera corrente).
            if (m_btnStart) m_btnStart->click();
            // L'utente e' avvisato e ha scelto di proseguire: zittiamo il watchdog
            // per QUESTA animazione (niente popup a raffica sullo stesso
            // rallentamento). DOPO il riavvio, non prima: da fermo il ramo
            // !animating del watchdog azzererebbe subito il flag.
            if (ui->glWidget) ui->glWidget->acknowledgePerformanceWarning();
        }
        m_perfPopupActive = false;
    }, Qt::QueuedConnection);

    connect(ui->btnWireUPlus,  &QPushButton::clicked, this, [this](){ ui->glWidget->increaseWireframeUDensity(); });
    connect(ui->btnWireVPlus,  &QPushButton::clicked, this, [this](){ ui->glWidget->increaseWireframeVDensity(); });
    connect(ui->btnWireUMinus, &QPushButton::clicked, this, [this](){ ui->glWidget->decreaseWireframeUDensity(); });
    connect(ui->btnWireVMinus, &QPushButton::clicked, this, [this](){ ui->glWidget->decreaseWireframeVDensity(); });

    // Colori Default
    float defR = 0.20f, defG = 0.80f, defB = 0.20f;
    m_currentSurfaceColor = QColor::fromRgbF(defR, defG, defB);

    if (ui->glWidget) {
        // Colore superficie (Verde)
        ui->glWidget->setColor(defR, defG, defB);
        // Colori della texture di default: verde e nero.
        ui->glWidget->setGlobalTextureColors(QColor::fromRgbF(0.20f, 0.80f, 0.20f), Qt::black);
    }

    ui->sliderR->setRange(0, 255);
    ui->sliderG->setRange(0, 255);
    ui->sliderB->setRange(0, 255);
    ui->lightSlider->setRange(0, 200); ui->lightSlider->setValue(100);
    ui->lblValLight->setText(QString::number(ui->lightSlider->value()) + " %");
    ui->speed3DSlider->setRange(1, 100); setPathSpeed3D(10);
    ui->speed4DSlider->setRange(1, 100); setPathSpeed4D(10);
    // Il trascinamento scrive lo stato (vedi pathSpeed3D).
    connect(ui->speed3DSlider, &QSlider::valueChanged, this, [this](int v) { m_pathSpeed3D = v; });
    connect(ui->speed4DSlider, &QSlider::valueChanged, this, [this](int v) { m_pathSpeed4D = v; });
    // FOV UNICO (dock renderer, sotto Light). Prima erano due slider separati nei
    // dock 3D e 4D, attivi solo con la RISPETTIVA path in corsa: andavano in
    // conflitto (due controlli sullo stesso m_cameraFov) e da fermo erano
    // entrambi bloccati, quindi dopo un path a FOV largo non si poteva
    // correggere l'inquadratura se non con Reset View. Questo e' l'unico
    // controllo, non si blocca mai e agisce su TUTTO: path 3D/4D, rotazioni,
    // t-motion e superfici statiche.
    ui->fovSliderMain->setRange(20, 110);
    ui->fovSliderMain->setValue(45);
    ui->lblValFov->setText(QString::number(45) + QString::fromUtf8("°"));

    UiStyleManager::setupBigSliders(ui->sliderR, ui->sliderG, ui->sliderB, ui->alphaSlider, ui->lightSlider, ui->speed3DSlider, ui->speed4DSlider, ui->fovSliderMain);

    // Color slot della texture (col1/col2). Surface NON è qui: è nella coppia
    // m_bgTargetGroup (Surface/Background). L'esclusività FRA i due gruppi è a mano.
    m_colorGroup = new QButtonGroup(this);
    m_colorGroup->addButton(ui->radioTexColor1);
    m_colorGroup->addButton(ui->radioTexColor2);
    m_colorGroup->setExclusive(true);

    m_currentBackgroundColor = QColor::fromRgbF(0.3f, 0.3f, 0.3f);
    ui->glWidget->setBackgroundColor(m_currentBackgroundColor);

    // (onColorTargetChanged è già invocato dall'handler toggled di radioBackground
    //  definito sopra: nessuna connessione separata per evitare doppia chiamata.)

    auto handleColorChange = [this]() {
        int r = ui->sliderR->value(); int g = ui->sliderG->value(); int b = ui->sliderB->value();
        ui->valR->setNum(r); ui->valG->setNum(g); ui->valB->setNum(b);
        QColor newColor(r, g, b);

        // Due gruppi INDIPENDENTI: la coppia (Surface/Background) dice DOVE
        // operiamo, la coppia Color1/Color2 QUALE tinta della texture editiamo. La
        // priorità è data dalla coppia; Color1/2 scelgono solo lo slot quando il target
        // ha una texture colorata attiva.
        if (editingBackground()) {
            if (targetTextureOn() && activeTextureUsesColors()) {
                // Texture di sfondo colorata: Color1/Color2 scelgono quale tinta.
                if (ui->radioTexColor2->isChecked()) m_bgTexColor2 = newColor;
                else m_bgTexColor1 = newColor;

                ui->glWidget->setProperty("bg_col1", QVector3D(m_bgTexColor1.redF(), m_bgTexColor1.greenF(), m_bgTexColor1.blueF()));
                ui->glWidget->setProperty("bg_col2", QVector3D(m_bgTexColor2.redF(), m_bgTexColor2.greenF(), m_bgTexColor2.blueF()));
                ui->glWidget->update();
            } else {
                m_currentBackgroundColor = newColor;
                ui->glWidget->setBackgroundColor(m_currentBackgroundColor);
                ui->glWidget->update();
            }
        }
        else { // target = Surface
            // In Wireframe la texture è nascosta e le linee usano il COLORE SUPERFICIE.
            bool wireframeMode = (shownRenderMode() == 2);
            // TEXTURE ATTIVA SUL DESTINATARIO CORRENTE. Con una fascia
            // selezionata conta la SUA texture: m_surfaceTextureState e' lo
            // stato di quella GLOBALE e resta false se si e' texturizzata solo
            // la fascia, quindi questo ramo non veniva mai preso e gli slider
            // finivano a editare il colore della superficie invece dei due
            // u_col1/u_col2 della texture -- "i picker non cambiano colore".
            const bool texActiveHere =
                (ui->glWidget && ui->glWidget->activeMeshPart() >= 0)
                    ? ui->glWidget->activeMeshTextureActive()
                    : m_surfaceTextureState;
            if (!wireframeMode && texActiveHere && activeTextureUsesColors()) {
                // Texture di superficie colorata: Color1/Color2 scelgono lo slot.
                // AMBITO "MESH": i colori vanno nella PARTE, non nei due slot
                // globali. Quelli appartengono alla texture di superficie, e
                // scriverli qui cambiava i colori di tutte le altre fasce che li
                // ereditano (stesso difetto del caso Mandelbrot, ma sul percorso
                // interattivo degli slider).
                // Lo slot non toccato resta quello che il bersaglio sta gia'
                // disegnando (surfaceTexColor: i colori propri della fascia, o
                // i globali che eredita).
                const bool slot2 = ui->radioTexColor2->isChecked();
                const QColor c1 = slot2 ? surfaceTexColor(1) : newColor;
                const QColor c2 = slot2 ? newColor : surfaceTexColor(2);
                if (!ui->glWidget->setActiveMeshTexColors(c1, c2))
                    ui->glWidget->setGlobalTextureColors(c1, c2);
                if (!surfaceTextureIsCustom() && !surfaceHasImage()) scheduleTextureGeneration();
            } else {
                m_currentSurfaceColor = newColor;
                ui->glWidget->setColor(r/255.0f, g/255.0f, b/255.0f);
            }
        }
    };

    ui->sliderR->disconnect(); ui->sliderG->disconnect(); ui->sliderB->disconnect();
    connect(ui->sliderR, &QSlider::valueChanged, this, handleColorChange);
    connect(ui->sliderG, &QSlider::valueChanged, this, handleColorChange);
    connect(ui->sliderB, &QSlider::valueChanged, this, handleColorChange);

    connect(ui->lightSlider, &QSlider::valueChanged, this, [this](int val){
        float intensity = val / 100.0f;
        ui->glWidget->setLightIntensity(intensity);
        ui->lblValLight->setText(QString::number(val) + " %");
    });

    // LUCE DI RIEMPIMENTO (Fill Light): luce dall'osservatore, SOLO Ray Marching.
    // Serve dove le due luci principali non arrivano -- le pareti viste da dentro
    // un tubo o una cavita' lungo un path, che restano nere per quanto si alzi
    // Light (che moltiplica, quindi su un valore quasi nullo non ha presa).
    // Scala 0..100 = 0.00..1.20. Il tetto era 0.50 e si e' rivelato basso: su
    // certe geometrie bisognava portare il cursore a fondo senza riuscire a
    // schiarire l'ombra, perche' le facce quasi PERPENDICOLARI alla vista sono
    // quelle che la headlight prende meno (vedi la formula "wrap" nel template,
    // che le recupera). Con 1.20 il cursore ha margine anche in quei casi e
    // resta comunque una regolazione fine nella prima meta' della corsa.
    // DEFAULT 0: a zero il termine nello shader e' esattamente zero e l'immagine
    // e' identica a prima. E' la ragione per cui questa luce e' un controllo e
    // non una costante -- vedi u_fillLight in glwidget.h.
    ui->fillLightSlider->setRange(0, 100);
    ui->fillLightSlider->setValue(0);
    ui->lblValFill->setText("0.00");
    // Stesso aspetto degli altri slider grandi. Copiato da lightSlider (che
    // setupBigSliders ha appena stilizzato) invece di allungare la firma di
    // quella funzione a dieci parametri: e' lo stesso stile, senza toccare una
    // API usata anche altrove.
    if (ui->lightSlider) {
        ui->fillLightSlider->setStyleSheet(ui->lightSlider->styleSheet());
        ui->fillLightSlider->setMinimumHeight(ui->lightSlider->minimumHeight());
    }
    connect(ui->fillLightSlider, &QSlider::valueChanged, this, [this](int val){
        const float v = val * 0.012f;          // 0..100 -> 0.00..1.20
        ui->glWidget->setFillLight(v);
        ui->lblValFill->setText(QString::number(v, 'f', 2));
    });

    // FOV UNICO: agisce SEMPRE e subito, qualunque cosa stia guidando la camera
    // (path 3D/4D, rotazioni, t-motion, o superficie ferma). Nessun gate: era il
    // blocco "solo con la propria path in corsa" a rendere il valore non
    // correggibile da fermo.
    connect(ui->fovSliderMain, &QSlider::valueChanged, this, [this](int val){
        applyCameraFov((float)val);
    });

    // ASPETTO PER-MESH: lo spinbox sceglie su quale parte agiscono i controlli
    // gia' esistenti (colore, trasparenza, Light, Solid/Wireframe). Il valore 0
    // mostra "All" e li riporta sullo stato globale, cioe' il comportamento di
    // sempre; 1..N selezionano la parte k-1. Cambiando selezione riallineiamo i
    // controlli ai valori di quella parte, cosi' gli slider mostrano cio' che
    // stanno per modificare invece di un valore ereditato da un'altra mesh.
    // Le parti di mesh nascono in GLWidget::updateSurfaceData, che ha molti
    // chiamanti (script, equazioni, cambio tab, load di preset): agganciarsi al
    // segnale invece che ai singoli chiamanti copre tutti i percorsi. In
    // particolare il load di un preset SCRIPT non passa da
    // checkAndTriggerMeshUpdate, dove l'aggancio precedente non scattava.
    connect(ui->glWidget, &GLWidget::meshPartsChanged, this, [this](){
        applyPendingMeshAppearance();
        updateMeshSelectorRange();
    });

    // ALL / MESH: due radio espliciti al posto della vecchia voce "All" nascosta
    // dentro lo spinbox (che era il valore 0 con specialValueText). Li' non si
    // capiva che 0 fosse uno stato diverso, e digitarlo veniva rifiutato perche'
    // updateMeshSelectorRange riportava subito la selezione a 1.
    // Passando ad All l'aspetto per-mesh NON si perde: resta nelle parti, e
    // tornando su Mesh si ritrova (i valori vivono in MeshPart, non nei radio).
    auto applyMeshScope = [this](){
        if (!ui->glWidget) return;
        const bool single = !meshScopeAll();
        ui->spinMeshSel->setEnabled(single);
        // In "All" la superficie si comporta come UNA SOLA: l'aspetto proprio
        // delle parti viene SOSPESO (ignorato dal render, non cancellato), cosi'
        // colore, trasparenza, luce e wireframe globali valgono per tutte.
        // Premendo "Mesh" le differenze tornano da sole: i valori sono rimasti
        // nelle MeshPart.
        ui->glWidget->setMeshAppearanceUniform(!single);
        ui->glWidget->setActiveMeshPart(single ? ui->spinMeshSel->value() - 1 : -1);
        syncAppearanceControlsToActiveMesh();

        // OROLOGIO TEXTURE. Cambiare ambito cambia QUALI texture sono in gioco:
        // in "All" quelle per-mesh sono sospese, quindi il clock va rivalutato o
        // resterebbe acceso per una texture animata che nessuno sta piu'
        // disegnando (e viceversa, spento tornando su "Mesh").
        // Si guarda SOLO la texture di SUPERFICIE: e' quella che questo clock
        // governa. Con allSurfaceTextureCode() (che aggrega anche le fasce),
        // passare ad "All" con una texture per-mesh animata RIACCENDEVA il clock
        // di superficie che l'utente aveva appena fermato -- e il tasto tornava
        // su "Stop" da solo.
        // ...e MAI dopo un master Stop. Il gate storico e' m_userStoppedTexClock,
        // che pero' registra il solo Stop del DOCK: performMasterStop alza
        // m_masterStopped (e m_userStoppedMeshTexClock per le fasce), non
        // quello. Cosi' bastava cambiare ambito per far ripartire la texture di
        // superficie a scena ferma -- e il master, che la contava come attivita'
        // in moto, tornava a dire STOP senza che si muovesse nient'altro.
        // Stessa regola di applyAnimationState: il master e' il gate di ogni
        // riaccensione, qualunque sia il modulo.
        if (!m_userStoppedTexClock && !m_masterStopped)
            ui->glWidget->setSurfaceTextureAnimating(
                hasTimeVariable(m_surfaceTextureCode));

        // In "All" il checkbox torna a mostrare lo stato GLOBALE della texture:
        // in "Mesh" ci pensa syncAppearanceControlsToActiveMesh (display della
        // parte), ma quella esce presto quando non c'e' parte attiva.
        if (!single && !editingBackground()
            && !implicitMode()) {
            refreshTextureCheckbox();
        }
        // Come per lo spinbox: il sync muove i radio a segnali bloccati, quindi
        // il gating (tasti densita' U/V) va aggiornato a mano.
        updateRenderState();

        // TASTI RICALCOLATI PER ULTIMI. syncAppearanceControlsToActiveMesh (piu'
        // sopra) li aggiorna gia', ma gira PRIMA che il clock texture venga
        // rivalutato qui: leggeva quindi lo stato vecchio e il tasto restava
        // quello dell'ambito precedente. Ordine: stato -> display, mai il
        // contrario.
        updateScriptButtonText();
        updateMasterButtonState();

        // GATING DEI LIMITI PER-MESH. I quattro campi u/v sono attivi solo in
        // ambito "Mesh" (in "All" non c'e' una parte a cui riferirli), e quel
        // gating vive in updateMeshScopeEnabled. Senza questa chiamata il
        // cambio di ambito non lo aggiornava: gli altri controlli per-mesh sono
        // sempre abilitati e cambiano solo DESTINATARIO, quindi finora nessuno
        // aveva bisogno di rivalutare il gating al click sui radio -- il giro
        // passava solo da meshPartsChanged, che al solo cambio di ambito non
        // scatta. I campi restavano percio' spenti e vuoti fino alla prima
        // rigenerazione della griglia.
        // Va per ULTIMA: legge i radio, che sono gia' nello stato finale.
        updateMeshScopeEnabled();
        // ...e il gating appena calcolato ha potuto RIABILITARE i campi, che
        // pero' sono ancora vuoti: updateMeshScopeEnabled li riempie solo
        // quando li spegne. Il display tocca a questa.
        syncMeshLimitFields();
    };
    // ESCLUSIVITA': i due radio NON sono fratelli (radioMeshOne sta dentro
    // groupMeshOne, il riquadro che lo tiene insieme allo spinbox; radioMeshAll
    // sta in widgetMeshSel). Qt rende esclusivi solo i radio con lo stesso
    // genitore, quindi senza questo gruppo esplicito ognuno faceva storia a se':
    // si potevano avere entrambi accesi, oppure entrambi spenti (un radio solo
    // nel suo gruppo e' anche deselezionabile).
    // Il gruppo li riunisce a prescindere dal layout: il riquadro attorno a
    // "Mesh" resta libero di essere spostato o ridisegnato.
    m_meshScopeGroup = new QButtonGroup(this);
    m_meshScopeGroup->setExclusive(true);
    m_meshScopeGroup->addButton(ui->radioMeshAll);
    m_meshScopeGroup->addButton(ui->radioMeshOne);
    // Il clic: prima lo STATO, che applyMeshScope legge. Arriva solo dai clic:
    // il programma scrive i radio a segnali bloccati (setMeshScopeAll).
    connect(ui->radioMeshAll, &QRadioButton::toggled, this, [this, applyMeshScope](bool on){
        if (!on) return;
        m_meshScopeAll = true;
        applyMeshScope();
    });
    connect(ui->radioMeshOne, &QRadioButton::toggled, this, [this, applyMeshScope](bool on){
        if (!on) return;
        m_meshScopeAll = false;
        applyMeshScope();
    });
    // Stato iniziale: radioMeshAll e' gia' checked nella .ui, quindi il suo
    // toggled NON scatta qui (le connect sono appena state fatte). Senza questa
    // chiamata l'ambito "All" sarebbe mostrato dai radio ma non applicato al
    // motore, e le mesh partirebbero gia' differenziate.
    //
    // PRIMA pero' va allineato il renderMode del motore alla modalita'
    // PARAMETRICA di partenza (Base, come radioBasic gia' selezionato sopra).
    // Nel setup del Ray Marching il motore riceve setGlobalRenderMode(1) = Shell, che
    // resta li' finche' nessuno lo cambia: applyMeshScope -> ...ToActiveMesh ->
    // ramo "All" -> i radio sulla globalRenderMode() del motore lo leggevano come
    // modalita' parametrica e accendeva PHONG, mentre il toro di default era
    // ovviamente disegnato in Base. Radio e superficie non concordavano.
    if (ui->glWidget) ui->glWidget->setGlobalRenderMode(m_savedRenderMode);
    applyMeshScope();

    // MOBILE: il campo Mesh e' un intero 1..N e non ha bisogno della tastiera.
    // Le frecce native vengono sostituite da due tasti a forma di freccia e il
    // campo diventa non editabile (vedi installMobileSpinButtons: su iOS
    // toccarlo apriva tastierino e menu di modifica senza poterli chiudere).
    // No-op su desktop.
    UiStyleManager::installMobileSpinButtons(ui->spinMeshSel);
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    // I radio non devono cedere spazio: senza questo il testo "Mesh" veniva
    // troncato in "Mes". Un minimo "a occhio" non basta — dipende dal font
    // effettivo, che su mobile e' piu' grande — quindi lo calcoliamo dal testo
    // reale: larghezza del testo + indicatore + margini dello stile, e lo
    // imponiamo come minimo E come dimensione preferita, cosi' il layout non
    // puo' comprimerli sotto quella soglia per far posto ai tasti.
    // NB: si usa sizeHint(), non un calcolo a mano su fontMetrics +
    // pixelMetric. Il QSS mobile ridefinisce sia la dimensione
    // dell'indicatore (QRadioButton::indicator width/height) sia lo spacing,
    // quindi i pixelMetric dello stile restituirebbero i valori di DEFAULT e
    // non quelli realmente usati: il minimo risulterebbe troppo stretto e il
    // testo continuerebbe a essere troncato. sizeHint tiene conto del foglio
    // di stile applicato.
    auto fitRadio = [](QRadioButton* rb) {
        if (!rb) return;
        rb->ensurePolished();                       // QSS applicato prima di misurare
        const int w = rb->sizeHint().width() + 8;    // 8 = respiro
        rb->setMinimumWidth(w);
        rb->setSizePolicy(QSizePolicy::Fixed, rb->sizePolicy().verticalPolicy());
    };
    fitRadio(ui->radioMeshAll);
    fitRadio(ui->radioMeshOne);

    // MARGINE SINISTRO (solo mobile). Va toccato SOLO quello: la distribuzione
    // fra i due gruppi la fa lo stretch 1,2 della .ui (non si tocca, o "Mesh"
    // torna sotto il campo come su desktop), e il rightMargin deve restare 0.
    // Quello zero non e' un caso: e' cio' che tiene il gruppo "Mesh + frecce +
    // campo" compattato a DESTRA, e quindi nettamente separato dal radio "All".
    // Un margine simmetrico lo staccherebbe dal bordo destro e i due gruppi
    // tornerebbero a somigliarsi.
    // Il leftMargin invece e' 24px tarati sui widget piccoli del desktop: su
    // mobile indicatori, font e tasti sono piu' grandi, la riga si riempie quasi
    // tutta e quei 24px venivano mangiati dal layout, lasciando "All"
    // appiccicato al bordo sinistro. Bastano pochi px in piu' per staccarlo,
    // senza spostare nulla a destra.
    if (auto* meshRow = qobject_cast<QHBoxLayout*>(ui->widgetMeshSel->layout())) {
        const QMargins m = meshRow->contentsMargins();
        meshRow->setContentsMargins(12, m.top(), 0, m.bottom());
        // COMPATTAZIONE A DESTRA. Con lo stretch 1,2 le due celle si allargano,
        // ma i widget dentro restano allineati a SINISTRA della propria cella:
        // il gruppo "Mesh" galleggiava a meta' di una cella larga il doppio,
        // invece di stare tutto a destra come prima. Il rightMargin a 0 da solo
        // non basta a rimediare, perche' e' la cella a essere piu' larga del
        // gruppo, non il margine a spingerlo dentro.
        // Allineandolo a destra torna compatto contro il bordo e nettamente
        // staccato da "All", che resta a sinistra nella sua cella.
        meshRow->setAlignment(ui->groupMeshOne, Qt::AlignRight | Qt::AlignVCenter);
    }
#endif

    connect(ui->spinMeshSel, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int val){
        if (!ui->glWidget) return;
        if (meshScopeAll()) return;   // in All lo spinbox e' inerte
        ui->glWidget->setActiveMeshPart(val - 1);     // lo spinbox parte da 1
        syncAppearanceControlsToActiveMesh();
        // syncAppearanceControlsToActiveMesh muove i radio a SEGNALI BLOCCATI
        // (deve: il loro handler scriverebbe la modalita' sulla parte), quindi
        // updateRenderState non gira da solo e il gating dei controlli resta
        // fermo allo stato della mesh PRECEDENTE. Senza questa chiamata,
        // selezionando una mesh in wireframe i tasti densita' U/V restavano
        // grigi. Va DOPO il sync, cosi' rilegge i radio gia' aggiornati.
        updateRenderState();
    });

    // Click su Color1/Color2. Gruppi indipendenti: scegliere quale tinta editare NON
    // tocca la coppia Surface/Background (che resta dov'è: continui a operare
    // sulla superficie o sullo sfondo). Basta riallineare gli slider alla tinta scelta.
    connect(m_colorGroup, &QButtonGroup::buttonClicked, this, [this](){
        onColorTargetChanged();
    });

    // =========================================================================
    // 8. MOTION, PATHS & NAVIGATION
    // =========================================================================
    float omega = 0.0f, phi = 0.0f, psi = 0.0f;
    refreshRotationSpeedLabels();

    ui->glWidget->setRotation4D(omega, phi, psi);
    ui->glWidget->addObjectRotation(30.0f, 30.0f, 0.0f);
    ui->glWidget->setNutationSpeed(0.0f); ui->glWidget->setPrecessionSpeed(0.0f); ui->glWidget->setSpinSpeed(0.0f);
    ui->glWidget->setOmegaSpeed(0.0f); ui->glWidget->setPhiSpeed(0.0f); ui->glWidget->setPsiSpeed(0.0f);

    ui->btnStart_2->setText("GO");

    m_lightingMode4D = 0;
    ui->glWidget->setLightingMode4D(0);
    ui->btnLightMode->setText("Directional Lighting");

    connect(ui->btnLightMode, &QPushButton::clicked, this, [this](){
        QString xEq = m_eq.x.trimmed(); QString yEq = m_eq.y.trimmed();
        QString zEq = m_eq.z.trimmed(); QString pEq = m_eq.p.trimmed();

        auto isNullCoord = [](const QString &s) { return s.isEmpty() || s == "0" || s == "0.0"; };

        bool isDegenerate4D = isNullCoord(xEq) || isNullCoord(yEq) || isNullCoord(zEq) || isNullCoord(pEq);
        int numModes = (!isDegenerate4D) ? 3 : 2;
        m_lightingMode4D = (m_lightingMode4D + 1) % numModes;

        switch (m_lightingMode4D) {
        case 0: ui->glWidget->setLightingMode4D(0); ui->btnLightMode->setText("Directional Lighting"); break;
        case 1: ui->glWidget->setLightingMode4D(1); ui->btnLightMode->setText("Observer Lighting"); break;
        case 2: ui->glWidget->setLightingMode4D(2); ui->btnLightMode->setText("Slice Lighting"); break;
        }
    });

    navTimer = new QTimer(this); navTimer->setInterval(30);
    connect(navTimer, &QTimer::timeout, this, &MainWindow::onNavTimerTick);

    pathTimer = new QTimer(this); pathTimer->setInterval(30);
    connect(pathTimer, &QTimer::timeout, this, &MainWindow::onPathTimerTick);

    pathTimer3D = new QTimer(this); pathTimer3D->setInterval(30);
    connect(pathTimer3D, &QTimer::timeout, this, &MainWindow::onPath3DTimerTick);

    ui->btnDeparture->setEnabled(false); connect(ui->btnDeparture, &QPushButton::clicked, this, &MainWindow::onDepartureClicked);
    ui->btnDeparture3D->setEnabled(false); connect(ui->btnDeparture3D, &QPushButton::clicked, this, &MainWindow::onDeparture3DClicked);

    m_pathViewMode4D = ModeTangential;
    m_pathViewMode3D = ModeTangential;
    // Enabled iniziale ai View: deciso da updateViewButtonsEnabled (campi path
    // vuoti all'avvio -> spenti, come il Departure; si accendono compilandoli).
    ui->pushView->setText("Tangent View"); ui->pushView->setEnabled(false); connect(ui->pushView, &QPushButton::clicked, this, &MainWindow::onToggleViewClicked);
    ui->pushView3D->setText("Tangent View"); ui->pushView3D->setEnabled(false); connect(ui->pushView3D, &QPushButton::clicked, this, &MainWindow::onToggleView3DClicked);

    connect(ui->lineX_P, &QLineEdit::textChanged, this, &MainWindow::checkPathFields);
    connect(ui->lineY_P, &QLineEdit::textChanged, this, &MainWindow::checkPathFields);
    connect(ui->lineZ_P, &QLineEdit::textChanged, this, &MainWindow::checkPathFields);
    connect(ui->lineP_P, &QLineEdit::textChanged, this, &MainWindow::checkPathFields);

    // Alpha/Beta/Gamma NON sono connessi: non accendono il Departure (solo le
    // COORDINATE lo fanno, vedi hasPath4DInput), quindi il loro textChanged
    // ricalcolerebbe sempre lo stesso esito. Collegarli direbbe il contrario.

    connect(ui->lineX_P3D, &QLineEdit::textChanged, this, &MainWindow::checkPath3DFields);
    connect(ui->lineY_P3D, &QLineEdit::textChanged, this, &MainWindow::checkPath3DFields);
    connect(ui->lineZ_P3D, &QLineEdit::textChanged, this, &MainWindow::checkPath3DFields);
    // lineR_P3D (rollio) idem: fuori dal gate, vedi hasPath3DInput.

    // Le espressioni dei path possono usare le costanti A..F/S: scrivere una
    // costante in un campo path deve sbloccarne l'edit come nelle equazioni.
    for (QLineEdit* pathEdit : { ui->lineX_P, ui->lineY_P, ui->lineZ_P, ui->lineP_P,
                                 ui->lineAlpha_P, ui->lineBeta_P, ui->lineGamma_P,
                                 ui->lineX_P3D, ui->lineY_P3D, ui->lineZ_P3D, ui->lineR_P3D }) {
        connect(pathEdit, &QLineEdit::textEdited, this, [this] { m_constantsEditPending = true; });
        connect(pathEdit, &QLineEdit::textChanged, this, &MainWindow::updateConstantsUIState);
    }

    // (L'Invio sui campi path passa dai filtri tastiera desktop/mobile, che
    // consumano il Return e chiamano commitPathFieldOnEnter: connettere qui
    // returnPressed non servirebbe, il segnale non viene mai emesso.)

    // Stessa ragione per i limiti U/V/W, che ammettono A..F/S: scrivere "2*A"
    // in uMax sblocca subito slider e casella di A. Qui SOLO lo stato dell'UI
    // delle costanti: il limite si applica alla conferma del campo (Invio o
    // uscita), mai su textChanged -- altrimenti la mesh si rigenererebbe a ogni
    // carattere digitato.
    for (QLineEdit* limitEdit : { ui->uMinEdit, ui->uMaxEdit,
                                  ui->vMinEdit, ui->vMaxEdit,
                                  ui->wMinEdit, ui->wMaxEdit }) {
        connect(limitEdit, &QLineEdit::textEdited, this, [this] { m_constantsEditPending = true; });
        connect(limitEdit, &QLineEdit::textChanged, this, &MainWindow::updateConstantsUIState);

        // Il dominio fa parte della definizione della superficie: toccarlo
        // riaccende il Run one-shot, esattamente come toccare X/Y/Z/P
        // (markUserEdit). Senza questo, cambiando i SOLI limiti il tasto
        // restava spento e non c'era piu' alcun modo di applicarli, ora che
        // non si applicano piu' da soli all'Invio.
        connect(limitEdit, &QLineEdit::textEdited, this, [this](const QString&) {
            if (!m_uiReady) return;
            QWidget* w = qobject_cast<QWidget*>(sender());
            noteSceneEdited(w);
            m_parametricApplied = false;
            // Modifica DELL'UTENTE in attesa di conferma. textEdited (non
            // textChanged) scatta solo per la digitazione: i setText di preset,
            // reset e cambio tab non lo emettono, e non devono far validare
            // nulla all'uscita dal campo.
            if (w) w->setProperty("userEditPending", true);
            updateMasterButtonState();
        });

        // Conferma del campo al CAMBIO DI FOCUS, non solo con l'Invio: valida
        // il numero (popup se illeggibile o min>=max) e registra il dominio --
        // che entra a schermo subito se la superficie e' in moto, al Run se e'
        // ferma. L'Invio arriva qui dai filtri tastiera, che consumano il
        // Return prima che QLineEdit emetta returnPressed; editingFinished
        // copre chi si limita a cliccare altrove.
        connect(limitEdit, &QLineEdit::editingFinished, this, [this, limitEdit]() {
            if (!m_uiReady) return;
            // Solo se c'e' una digitazione da confermare. Qt emette
            // editingFinished a ogni perdita di focus -- anche entrando e
            // uscendo da un campo senza toccarlo, e anche subito dopo il
            // clearFocus() del filtro tastiera sull'Invio: senza questa
            // guardia il campo verrebbe validato (e il popup mostrato) due
            // volte, o per un testo che l'utente non ha mai scritto.
            // Il controllo-e-consuma del flag sta dentro commitLimitFieldOnEnter,
            // sede unica: farlo anche qui lo consumerebbe prima, e la chiamata
            // del filtro tastiera che segue il clearFocus() tornerebbe a
            // rivalidare (due popup per un solo Invio).
            commitLimitFieldOnEnter(limitEdit->objectName());
        });
    }

    // LIMITI PER-MESH (pannello Multi Mesh). Stesso cablaggio dei limiti
    // globali qui sopra -- textEdited per registrare la digitazione in attesa,
    // editingFinished per confermarla -- ma con due assenze deliberate:
    //  - updateConstantsUIState: questi campi accettano gli stessi A..F/S, ma
    //    lo sblocco delle costanti lo decidono gia' i campi globali e le
    //    equazioni. Agganciarli qui non aggiungerebbe nulla e farebbe girare
    //    il ricalcolo a ogni carattere.
    //  - m_parametricApplied / updateMasterButtonState: il Run dello script
    //    RISCRIVE questi domini, quindi accendere il tasto Run come se ci
    //    fosse qualcosa "da applicare" direbbe il contrario del vero. Il
    //    dominio per-mesh si applica da se' alla conferma del campo.
    for (QLineEdit* meshLimitEdit : { ui->meshUMinEdit, ui->meshUMaxEdit,
                                      ui->meshVMinEdit, ui->meshVMaxEdit }) {
        if (!meshLimitEdit) continue;

        connect(meshLimitEdit, &QLineEdit::textEdited, this, [this](const QString&) {
            if (!m_uiReady) return;
            QWidget* w = qobject_cast<QWidget*>(sender());
            // La scena e' cambiata come per ogni altro comando di aspetto
            // per-mesh: il dominio di una fascia fa parte di cio' che il
            // record salva.
            noteSceneEdited(w);
            if (w) w->setProperty("userEditPending", true);
        });

        connect(meshLimitEdit, &QLineEdit::editingFinished, this, [this, meshLimitEdit]() {
            if (!m_uiReady) return;
            commitMeshLimitFieldOnEnter(meshLimitEdit->objectName());
        });
    }

    connectNavButton(ui->btnForward, GLWidget::MoveForward); connectNavButton(ui->btnBackward, GLWidget::MoveBack);
    connectNavButton(ui->btnLeft, GLWidget::MoveLeft); connectNavButton(ui->btnRight, GLWidget::MoveRight);
    connectNavButton(ui->btnDown, GLWidget::MoveDown); connectNavButton(ui->btnUp, GLWidget::MoveUp);
    connectNavButton(ui->btnRollLeft, GLWidget::RollLeft); connectNavButton(ui->btnRollRight, GLWidget::RollRight);
    connectNavButton(ui->btnXPlus,  GLWidget::ObsMoveXPos); connectNavButton(ui->btnXMinus, GLWidget::ObsMoveXNeg);
    connectNavButton(ui->btnYPlus,  GLWidget::ObsMoveYPos); connectNavButton(ui->btnYMinus, GLWidget::ObsMoveYNeg);
    connectNavButton(ui->btnZPlus,  GLWidget::ObsMoveZPos); connectNavButton(ui->btnZMinus, GLWidget::ObsMoveZNeg);
    connectNavButton(ui->btnPPlus,  GLWidget::ObsMovePPos); connectNavButton(ui->btnPMinus, GLWidget::ObsMovePNeg);
    connectNavButton(ui->btnOmegaAhead, GLWidget::RotOmegaPos); connectNavButton(ui->btnOmegaRear,  GLWidget::RotOmegaNeg);
    connectNavButton(ui->btnPhiRear, GLWidget::RotPhiNeg); connectNavButton(ui->btnPhiAhead,  GLWidget::RotPhiPos);
    connectNavButton(ui->btnPsiAhead,   GLWidget::RotPsiPos); connectNavButton(ui->btnPsiRear,    GLWidget::RotPsiNeg);

    // =========================================================================
    // 9. SCRIPTING & TEXTURE DOCK
    // =========================================================================
    connect(ui->btnScriptMode, &QPushButton::clicked, this, &MainWindow::onToggleScriptMode);
    connect(ui->btnRunCurrentScript, &QPushButton::clicked, this, &MainWindow::onRunCurrentScript);
    connect(ui->btnSaveScript, &QPushButton::clicked, this, &MainWindow::onSaveScriptClicked);

    m_currentScriptMode = ScriptModeSurface;
    updateScriptButtonText();

    ui->btnFlatPreview->setText("2D View");
    ui->btnFlatPreview->setEnabled(false);

    connect(ui->btnFlatPreview, &QPushButton::toggled, this, [this](bool checked){
        if (checked) {
            ui->btnFlatPreview->setText("3D View");
            ui->alphaSlider->setEnabled(false); // Blocchiamo solo la trasparenza
        } else {
            updateFlatPreviewButton();
            ui->alphaSlider->setEnabled(true);
        }

        ui->glWidget->setFlatView(checked);
        ui->glWidget->update();
    });

    updateFlatPreviewButton();

    ui->txtScriptEditor->setPlaceholderText("Write GLSL code for custom texture.\nExample: return vec4(0.2 * u - 0.5, 0.2 * v - 0.5, 0.2 * sin(u * v), 1.0);");

    // =========================================================================
    // 10. LIBRARY TREES & FILE SYSTEM
    // =========================================================================
    QSettings settings;
    QStringList repos = settings.value("repositoryPaths").toStringList();

    if (!repos.isEmpty() && QDir(repos.first()).exists()) lastTextureFolder = repos.first();
    else {
        QString osBaseDir;
#ifdef Q_OS_ANDROID
        osBaseDir = "/storage/emulated/0/Download";
#elif defined(Q_OS_LINUX)
        osBaseDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
        if (osBaseDir.isEmpty()) osBaseDir = QDir::homePath();
#else
        osBaseDir = QDir::homePath();
#endif
        QString potentialPath = osBaseDir + "/Texture";
        if (QDir(potentialPath).exists()) lastTextureFolder = potentialPath;
        else lastTextureFolder = osBaseDir;
    }

    m_menuController = new LibraryMenuController(this);
    m_presetSerializer = new PresetSerializer(this);
    m_fileOps = new LibraryFileOperations(this);
    m_dragDropHandler = new LibraryDragDropHandler(this);
    m_audioController = new AudioController(this);

    auto initTree = [this](QTreeWidget* tree) {
        tree->setHeaderHidden(true);
        tree->setColumnCount(1);
        tree->setContextMenuPolicy(Qt::CustomContextMenu);
        tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        // Su mobile la selezione multipla col dito e' involontaria (muovendo il dito
        // si selezionano piu' file) e comunque il long-press per aprire il menu la
        // riduceva a 1 -> "Copy/Cut/Delete N items" agiva su un solo file. Selezione
        // SINGOLA: un tocco = un item, menu sempre coerente. Su desktop resta
        // ExtendedSelection (Ctrl/Shift+click funziona bene col mouse).
        tree->setSelectionMode(QAbstractItemView::SingleSelection);
        // Drag&drop diretto disabilitato su mobile (competeva con lo scroll a dito):
        // lo spostamento resta via menu Cut/Paste (long-press).
        tree->setDragDropMode(QAbstractItemView::NoDragDrop);
#else
        tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
        tree->setDragDropMode(QAbstractItemView::InternalMove);
#endif

        // 1. FORZA lo scroll per pixel
        tree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        tree->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);

        // 2. IL VERO SEGRETO: Dichiara che le righe sono tutte alte uguali.
        // Senza questo, Qt annulla lo scroll fluido e torna agli "scatti"!
        tree->setUniformRowHeights(true);

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        // Niente scroll cinetico a dito sul viewport: si scrolla solo con la scroll
        // bar (lo scroll a dito animava di moto proprio ed evidenziava gli item).
        // Il TapAndHold resta: apre il menu contestuale.
        tree->grabGesture(Qt::TapAndHoldGesture);

        tree->setIndentation(12);
#endif

        tree->installEventFilter(m_dragDropHandler);
        tree->viewport()->installEventFilter(m_dragDropHandler);
    };
    initTree(ui->treeSurfaces);
    connect(ui->treeSurfaces, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos){
        if (!ui->treeSurfaces->itemAt(pos)) { ui->treeSurfaces->clearSelection(); ui->treeSurfaces->setCurrentItem(nullptr); }
        m_menuController->showMenu(ui->treeSurfaces, pos);
    });
    connect(ui->treeSurfaces, &QTreeWidget::itemClicked, this, &MainWindow::onExampleItemClicked);

    initTree(ui->treeTextures);
    connect(ui->treeTextures, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos){
        if (!ui->treeTextures->itemAt(pos)) { ui->treeTextures->clearSelection(); ui->treeTextures->setCurrentItem(nullptr); }
        m_menuController->showMenu(ui->treeTextures, pos);
    });
    connect(ui->treeTextures, &QTreeWidget::itemClicked, this, &MainWindow::onExampleItemClicked);

    initTree(ui->treeMotions);
    connect(ui->treeMotions, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos){
        if (!ui->treeMotions->itemAt(pos)) { ui->treeMotions->clearSelection(); ui->treeMotions->setCurrentItem(nullptr); }
        m_menuController->showMenu(ui->treeMotions, pos);
    });
    connect(ui->treeMotions, &QTreeWidget::itemClicked, this, &MainWindow::onExampleItemClicked);

    initTree(ui->treeSounds);
    connect(ui->treeSounds, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos){
        if (!ui->treeSounds->itemAt(pos)) { ui->treeSounds->clearSelection(); ui->treeSounds->setCurrentItem(nullptr); }
        m_menuController->showMenu(ui->treeSounds, pos);
    });
    connect(ui->treeSounds, &QTreeWidget::itemClicked, this, &MainWindow::onSoundItemClicked);

    // APRIRE O CHIUDERE UNA CARTELLA NON SPOSTA L'EVIDENZIAZIONE. Il clic sulla
    // RIGA di una cartella (tap su mobile, doppio clic su desktop; solo la
    // freccetta non seleziona) passa prima da Qt, che sposta la selezione sulla
    // cartella: il preset in vigore perdeva il focus anche se il clic serviva
    // solo ad aprire o chiudere il ramo. Qui si ricorda cio' che la cartella ha
    // scalzato e lo si rimette quando il ramo si apre o si chiude.
    // Il ripristino scatta solo se la cartella e' ancora l'intera selezione:
    // le espansioni del programma (load, refresh) arrivano con la selezione
    // gia' sul preset nuovo, e una cartella selezionata con un clic singolo
    // resta selezionata finche' non la si apre (serve a Copy/Cut/Paste/Delete).
    // Indici persistenti, non puntatori: se l'albero viene ricostruito
    // (refreshLibrary) diventano invalidi da soli.
    auto keepHighlightOnToggle = [this](QTreeWidget *tree) {
        struct Displaced { QPersistentModelIndex folder; QList<QPersistentModelIndex> leaves; };
        auto d = std::make_shared<Displaced>();
        connect(tree->selectionModel(), &QItemSelectionModel::selectionChanged, this,
                [tree, d](const QItemSelection &, const QItemSelection &deselected) {
            d->folder = QPersistentModelIndex();
            d->leaves.clear();
            const QList<QTreeWidgetItem *> sel = tree->selectedItems();
            if (sel.size() != 1 || sel.first()->childCount() == 0) return;
            d->folder = tree->indexFromItem(sel.first());
            for (const QModelIndex &i : deselected.indexes()) {
                QTreeWidgetItem *it = tree->itemFromIndex(i);
                if (it && it->childCount() == 0) d->leaves << QPersistentModelIndex(i);
            }
        });
        auto restore = [tree, d](QTreeWidgetItem *folder) {
            if (d->leaves.isEmpty() || tree->indexFromItem(folder) != d->folder) return;
            const QList<QTreeWidgetItem *> sel = tree->selectedItems();
            if (sel.size() != 1 || sel.first() != folder) return;
            const QList<QPersistentModelIndex> leaves = d->leaves;   // la selectionChanged qui sotto lo azzera
            QItemSelectionModel *sm = tree->selectionModel();
            sm->clearSelection();
            QModelIndex current;
            for (const QPersistentModelIndex &i : leaves) {
                if (!i.isValid()) continue;
                sm->select(i, QItemSelectionModel::Select | QItemSelectionModel::Rows);
                if (!current.isValid()) current = i;
            }
            // Senza autoscroll: spostare l'elemento corrente fa scrollTo, che
            // RIAPRE i genitori chiusi -- cioe' la cartella appena chiusa.
            if (current.isValid()) {
                const bool autoScroll = tree->hasAutoScroll();
                tree->setAutoScroll(false);
                sm->setCurrentIndex(current, QItemSelectionModel::NoUpdate);
                tree->setAutoScroll(autoScroll);
            }
        };
        connect(tree, &QTreeWidget::itemExpanded, this, restore);
        connect(tree, &QTreeWidget::itemCollapsed, this, restore);
    };
    for (QTreeWidget *tree : { ui->treeSurfaces, ui->treeTextures, ui->treeMotions, ui->treeSounds })
        keepHighlightOnToggle(tree);

    connect(ui->btnSyncLibrary, &QPushButton::clicked, this, &MainWindow::onSyncPresetsClicked);

    m_fsWatcher = new QFileSystemWatcher(this);
    m_fsSyncTimer = new QTimer(this);
    m_fsSyncTimer->setSingleShot(true);
    m_fsSyncTimer->setInterval(500);

    connect(m_fsWatcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString &){ m_fsSyncTimer->start(); });
    connect(m_fsWatcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &){ m_fsSyncTimer->start(); });
    connect(m_fsSyncTimer, &QTimer::timeout, this, &MainWindow::refreshRepositories);

    // =========================================================================
    // 11. FINAL STARTUP CALLS
    // =========================================================================
    ui->chkBoxTexture->setChecked(false);
    applySurfaceTextureToEngine();   // intenzione iniziale: spenta
    updateTextureUIState(false);

    connectSidePanels();
    switchToMainMode();

    // ACCESSO ALLA LIBRERIA, UNA VOLTA PER TUTTA LA SESSIONE.
    // Non basta riaprirlo dentro refreshRepositories: la libreria contiene anche
    // FILE ESTERNI referenziati per percorso assoluto dai preset — i .wav/.mp3
    // dei suoni (//MUSIC:) e le immagini delle texture (//IMG:). Chi li carica
    // decide con QFile::exists() / QDirIterator, che sotto sandbox falliscono
    // esattamente come gli alberi vuoti: il record si apriva EVIDENZIANDO il
    // suono o l'immagine in libreria, ma senza attivarli (audio muto, texture
    // sostituita da quella di default). Aprendo lo scope qui, prima di ogni
    // caricamento, tutti quei punti trovano i file senza doverlo sapere.
    // No-op fuori dalla sandbox.
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    // RADICE DI SISTEMA SALVATA PER ERRORE: si BUTTA, non si corregge.
    //
    // Una versione precedente del dialogo di recupero salvava la cartella
    // scelta GREZZA, senza scendere in "presets": chi ha indicato ~/Documents
    // si e' ritrovato surfaces/textures/records/sounds sparsi direttamente li'.
    // Quel valore resta in QSettings e va tolto, altrimenti i quattro rami
    // vengono ricreati a ogni avvio.
    //
    // NON si prova a "riparare" la radice inventando <cartella>/presets: sotto
    // sandbox una cartella CREATA DAL CODICE non e' raggiungibile ai riavvii.
    // Un security-scoped bookmark autorizza solo cio' che l'UTENTE ha indicato
    // in un pannello di sistema (Powerbox); su una cartella mai scelta il
    // bookmark si salva ma non concede nulla, e all'avvio dopo compariva
    // "Your library folder can no longer be opened" -- un messaggio da
    // diagnostica, incomprensibile a chi installa l'app per la prima volta.
    // Peggio: ~/Documents sotto sandbox NON e' quella reale, e' la Documents
    // PRIVATA e vuota del container (Desktop e Downloads sono symlink,
    // Documents no), quindi quella radice non avrebbe mai mostrato nulla.
    //
    // Azzerando la chiave si torna esattamente al comportamento del DMG e del
    // primo avvio: nessun popup d'errore, e alla prima apertura del dock
    // Library parte setupDefaultFolders, che CHIEDE dove installare i preset
    // con un pannello di sistema -- l'unico gesto che sotto sandbox concede
    // davvero l'accesso, e che rende la cartella scelta valida per sempre.
    {
        QSettings settings;
        const QString cleanRoot = QDir::cleanPath(settings.value("libraryRootPath").toString());

        // Home REALE dell'utente, non quella del container: si ricava dal
        // percorso stesso (/Users/<nome>), perche' QStandardPaths sotto sandbox
        // risponde con i percorsi redirezionati e non darebbe mai riscontro.
        QStringList systemDirs;
        const QRegularExpression userRe(QStringLiteral("^(/Users/[^/]+)"));
        const auto m = userRe.match(cleanRoot);
        if (m.hasMatch()) {
            const QString home = m.captured(1);
            systemDirs << home
                       << home + "/Documents"
                       << home + "/Desktop"
                       << home + "/Downloads";
        }

        if (!cleanRoot.isEmpty() && systemDirs.contains(cleanRoot)) {
            qWarning() << "Libreria: radice di sistema" << cleanRoot
                       << "-- scartata, si tornera' a chiedere la cartella.";
            settings.remove("libraryRootPath");
            // Puntavano ai rami sparsi: se restassero, scavalcherebbero la
            // scelta successiva (sono lette con default rootPath+"/ramo").
            settings.remove("pathSurfaces");
            settings.remove("pathTextures");
            settings.remove("pathRecords");
            settings.remove("pathSounds");
        }
    }

    SecurityBookmark::restore(QSettings().value("libraryRootPath").toString());
#endif

    // LIBRERIA NON RAGGIUNGIBILE: dirlo, invece di mostrare quattro alberi vuoti.
    // E' il caso in cui la cartella e' stata scelta in passato (esiste un
    // bookmark) ma l'autorizzazione non si risolve piu'. Prima il fallimento era
    // MUTO: i gate QDir::exists() saltavano il caricamento in silenzio e il
    // sintomo ("libreria vuota") non suggeriva in alcun modo la causa. I file
    // NON sono persi: manca solo il permesso di raggiungerli.
    // needsAuthorization e' sempre false fuori dalla sandbox: su DMG, Windows e
    // Linux questo blocco non si attiva mai.
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    {
        const QString libRoot = QSettings().value("libraryRootPath").toString();

        // SOLO il caso "autorizzazione persa" (sandbox / App Store): la cartella
        // c'e' e i preset sono al loro posto, manca il permesso di raggiungerli.
        // Senza questo dialogo la libreria resterebbe vuota senza spiegazione, e
        // nessun altro punto lo direbbe.
        //
        // NON si avvisa qui della cartella SPARITA (radice configurata, cartella
        // inesistente): sarebbe un popup di troppo, perche' subito dopo arriva
        // comunque quello di setupDefaultFolders che chiede dove installare la
        // libreria -- due finestre in fila per la stessa decisione. Il secondo
        // basta da solo.
        if (SecurityBookmark::needsAuthorization(libRoot)) {
            // Testo breve in setText, resto in informativeText: il testo
            // principale di QMessageBox e' un TITOLO e non va a capo, quindi il
            // box si allarga fino a contenere su UNA riga la piu' lunga di
            // quelle logiche. Attenzione: le righe che qui sotto sembrano corte
            // sono letterali C++ ADIACENTI, che il compilatore concatena in
            // un'unica riga senza \n -- misurata 823 pt, da cui un box di 983 pt.
            // L'informativeText invece manda a capo da solo.
            QMessageBox box(this);
            box.setIcon(QMessageBox::Question);
            box.setWindowTitle("Library Not Accessible");
            box.setText("Your library folder can no longer be opened.");
            // Il testo diceva "Your presets are still there — only the permission
            // was lost": FALSO nel caso piu' comune che porta qui, la cartella
            // spostata o cancellata. needsAuthorization non distingue le due cose
            // (bookmark salvato + cartella irraggiungibile), quindi il messaggio
            // deve coprirle entrambe e dire cosa succede se la libreria non c'e'
            // piu'.
            box.setInformativeText(
                libRoot + "\n\n"
                "It may have been moved or deleted, or the permission to open it "
                "may have been lost after a system update.\n\n"
                "Do you want to select your library folder? If it no longer exists, "
                "select where to create a new one: the factory presets will be "
                "installed in a \"presets\" folder inside it.");
            box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
            box.setDefaultButton(QMessageBox::Yes);
            const auto answer = box.exec();

            if (answer == QMessageBox::Yes) {
                const QString picked = QFileDialog::getExistingDirectory(this,
                    "Select Your Library Folder", QDir::homePath());
                if (!picked.isEmpty()) {
                    const QString pickedClean = QDir::cleanPath(picked);

                    // DUE CASI, decisi da cio' che la cartella indicata CONTIENE.
                    // Dopo il pannello di sistema la cartella e' autorizzata, quindi
                    // qui il contenuto si legge davvero: non c'e' il rischio di
                    // scambiare una libreria irraggiungibile per una cartella vuota.
                    //
                    // RECUPERO: la cartella e' la libreria, o ne contiene una in
                    // "presets" (casi 1 e 2 di resolveLibraryRoot) -> si riprende
                    // quella, senza installare niente.
                    //
                    // NUOVA INSTALLAZIONE: nessuna libreria in vista -- la vecchia e'
                    // stata spostata o cancellata. Prima si prendeva la cartella
                    // COM'ERA come radice: libreria vuota, e Restore Factory Presets
                    // (che la trovava valida) rovesciava i quattro rami direttamente
                    // li' dentro. MISURATO il 27/9: cancellata ~/Projects/presets e
                    // indicata ~/Projects, rami sparsi in ~/Projects. Ora si fa come
                    // alla prima installazione: "<scelta>/presets" e preset di
                    // fabbrica. Eccezione: se la cartella indicata si chiama gia'
                    // "presets" si usa lei, per non creare presets/presets.
                    QString clean = resolveLibraryRoot(picked);
                    const bool newInstall = !dirIsLibraryRoot(QDir(clean));
                    if (newInstall
                        && QFileInfo(pickedClean).fileName().compare(
                               QLatin1String("presets"), Qt::CaseInsensitive) == 0) {
                        clean = pickedClean;
                    }

                    QDir().mkpath(clean);
                    {   // sync() esplicito: vedi setupDefaultFolders (cfprefsd)
                        QSettings s;
                        s.setValue("libraryRootPath", clean);
                        s.sync();
                    }
                    // Come in setupDefaultFolders: bookmark ANCHE della cartella
                    // indicata nel pannello, da cui nasce il diritto sotto sandbox.
                    if (clean != pickedClean) SecurityBookmark::save(pickedClean);
                    SecurityBookmark::save(clean);

                    // La radice ora e' raggiungibile, quindi setupDefaultFolders
                    // salta la domanda e fa solo il resto: crea i quattro rami,
                    // installa i preset di fabbrica e aggiorna gli alberi.
                    if (newInstall) setupDefaultFolders();
                }
            }
        }
    }
#endif

    refreshRepositories();
    updateWatcherPaths();

#if defined(Q_OS_IOS)
    // SU IOS: showFullScreen() è obbligatorio per sbloccare la risoluzione nativa
    this->showFullScreen();
#elif defined(Q_OS_ANDROID)
    // SU ANDROID: Ripristiniamo showMaximized().
    // Evita l'Immersive Mode che spinge la UI a destra contro il Notch.
    this->showMaximized();
#else
    this->resize(1280, 720);
    this->showMaximized();
#endif

    QPushButton* mobileMenuBtn = nullptr;

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    ui->menuBar->hide();

    // Creiamo il bottone, con parent 'this' per evitare problemi di touch col 3D
    mobileMenuBtn = new QPushButton(this);
    mobileMenuBtn->setObjectName("mobileMenuBtn");

    // Applichiamo tutto il disegno e lo stile tramite il manager!
    UiStyleManager::styleMobileMenuButton(mobileMenuBtn);

    mobileMenuBtn->move(10, 10);

    QMenu* overflowMenu = new QMenu(this);
    overflowMenu->addAction(ui->actionDocumentation);
    overflowMenu->addAction(ui->actionAbout);
    overflowMenu->addSeparator();
    overflowMenu->addAction(ui->actionQuit);

    // Applichiamo il foglio di stile CSS del menu
    UiStyleManager::styleMobileOverflowMenu(overflowMenu);

    // Connessione al tocco
    connect(mobileMenuBtn, &QPushButton::released, this, [mobileMenuBtn, overflowMenu]() {
        QPoint pos = mobileMenuBtn->mapToGlobal(QPoint(0, mobileMenuBtn->height()));
        overflowMenu->exec(pos);
    });

    mobileMenuBtn->raise();
    mobileMenuBtn->show();
#endif

#if defined (Q_OS_IOS)
    // Manteniamo il tuo HACK DI PRE-RISCALDAMENTO per l'iPad
    this->setUpdatesEnabled(false);

    ui->dockEquations->show(); ui->dockEquations->hide();
    ui->dockRenders->show();   ui->dockRenders->hide();
    ui->dock3D->show();        ui->dock3D->hide();
    ui->dock4D->show();        ui->dock4D->hide();
    ui->dockScripts->show();   ui->dockScripts->hide();
    ui->dockSurfaces->show();  ui->dockSurfaces->hide();

    ui->dock3D->blockSignals(false);
    ui->dock4D->blockSignals(false);

    this->setUpdatesEnabled(true);
#endif

    // =========================================================================
    // 12. RESCALING & DESKTOP FILTERS
    // =========================================================================

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    // Applica il blocco dell'invio su Desktop per TUTTI i campi di testo
    DesktopInputFilter* desktopFilter = new DesktopInputFilter(this);

    // Per i campi multi-riga delle equazioni (QPlainTextEdit)
    for (auto* textEdit : this->findChildren<QPlainTextEdit*>()) {
        textEdit->installEventFilter(desktopFilter);
    }

    // Per i campi a riga singola di costanti, limiti e percorsi (QLineEdit)
    for (auto* lineEdit : this->findChildren<QLineEdit*>()) {
        lineEdit->installEventFilter(desktopFilter);
    }
#endif

    EnterApplyFilter* equationEnterFilter = new EnterApplyFilter(this);
    // Il flag marca QUESTA via d'ingresso (Invio su un campo equazione), che
    // altrimenti onStartClicked non potrebbe distinguere dalle chiamate
    // programmatiche: entrambe arrivano senza sender(). Serve a riarmare il
    // clock della geometria come fa il Run. Ripristinato sempre, anche sulle
    // molte uscite anticipate di onStartClicked (validazioni, popup).
    equationEnterFilter->onEnter = [this]() {
        m_commitFromEnterKey = true;
        const auto reset = qScopeGuard([this]{ m_commitFromEnterKey = false; });
        onStartClicked();
    };
    ui->lineEquation->installEventFilter(equationEnterFilter);
    ui->lineEquationCrossSection->installEventFilter(equationEnterFilter);

    // Run del dock Equations: applica le equazioni parametriche senza passare
    // dal tasto master START/STOP della status bar
    connect(ui->btnRunParametric, &QPushButton::clicked, this, &MainWindow::onStartClicked);

    // Run del dock Equations (modalità implicita / Ray Marching): stesso effetto
    // dell'invio nel campo equazione, agisce SOLO sul modulo equazioni.
    connect(ui->btnImplicit, &QPushButton::clicked, this, &MainWindow::onStartClicked);

    // All'avvio entrambe le superfici di default (toro parametrico e sfera
    // implicita) sono già renderizzate: i Run "one-shot" devono nascere
    // DISABILITATI. Riasseriamo i flag a true perché impostare le equazioni di
    // default in costruzione fa scattare i loro textChanged (es. lineEquation a
    // segnali NON bloccati) che li avrebbero rimessi a false. Poi forziamo
    // l'allineamento dei tasti: senza questa chiamata updateMasterButtonState non
    // gira mai in costruzione (m_btnStart non esisteva ancora ai primi trigger) e
    // i tasti resterebbero sull'enabled di default del .ui.
    m_parametricApplied = true;
    m_implicitApplied = true;
    m_rmTextureApplied = true;
    // All'avvio a schermo c'e' la superficie di default (toro/sfera): i campi
    // sono pieni ma non sono lavoro dell'utente, come dopo un reset.
    m_surfaceOrigin = OriginDefault;
    // Tracciamento della posa mossa dall'utente (mouse, touch, tasti di camera).
    // PRIMA di m_uiReady: i segnali emessi durante il setup trovano la guardia
    // di noteViewControlUsed ancora chiusa.
    wireViewControlsTracking();
    m_uiReady = true;   // sblocca updateMasterButtonState: la UI è completa
    updateMasterButtonState();
    // La scena di avvio (superficie di default) e' il primo momento pulito.
    markSceneClean();
}

void MainWindow::connectSidePanels()
{
    // --- Navigazione Dock ---
    connect(ui->dock3D, &QDockWidget::visibilityChanged, this, [this](bool visible){
        if (visible) switchTo3DMode();
    });
    connect(ui->dock4D, &QDockWidget::visibilityChanged, this, [this](bool visible){
        if (visible) switchTo4DMode();
    });

    // --- CONTROLLI ROTAZIONE

    // Precessione
    setupSpeedControl(ui->btnPrecessionPlus, ui->btnPrecessionMinus,
                      [this]{ return ui->glWidget->getPrecessionSpeed(); },
                      [this](float v){ ui->glWidget->setPrecessionSpeed(v); });

    // Nutazione
    setupSpeedControl(ui->btnNutationPlus, ui->btnNutationMinus,
                      [this]{ return ui->glWidget->getNutationSpeed(); },
                      [this](float v){ ui->glWidget->setNutationSpeed(v); });

    // Spin
    setupSpeedControl(ui->btnSpinPlus, ui->btnSpinMinus,
                      [this]{ return ui->glWidget->getSpinSpeed(); },
                      [this](float v){ ui->glWidget->setSpinSpeed(v); });

    // Omega (4D)
    setupSpeedControl(ui->btnOmegaPlus, ui->btnOmegaMinus,
                      [this]{ return ui->glWidget->getOmegaSpeed(); },
                      [this](float v){
                          ui->glWidget->setOmegaSpeed(v);
                          update4DButtonState();
                      });

    // Phi (4D)
    setupSpeedControl(ui->btnPhiPlus, ui->btnPhiMinus,
                      [this]{ return ui->glWidget->getPhiSpeed(); },
                      [this](float v){
                          ui->glWidget->setPhiSpeed(v);
                          update4DButtonState();
                      });

    // Psi (4D)
    setupSpeedControl(ui->btnPsiPlus, ui->btnPsiMinus,
                      [this]{ return ui->glWidget->getPsiSpeed(); },
                      [this](float v){
                          ui->glWidget->setPsiSpeed(v);
                          update4DButtonState();
                      });

    // Tasto Stop/Go laterale
    connect(ui->btnStart_2, &QPushButton::clicked, this, &MainWindow::onStopClicked);
}

void MainWindow::connectNavButton(QPushButton *btn, int action)
{
    if (!btn) return;

    // Registriamo il pulsante per poterli disabilitare in blocco durante un path
    // (i tasti di spostamento a click dei dock 3D/4D non hanno senso mentre la
    // telecamera segue un percorso).
    m_navButtons.append(btn);

    // Quando PREMI un bottone
    connect(btn, &QPushButton::pressed, this, [this, action]() {
        // Se è il primo tasto che premo, avvio il timer
        if (activeNavActions.isEmpty()) {
            navTimer->start();
        }
        // Aggiungo questa azione alla lista delle azioni attive
        activeNavActions.insert(action);

        // (Opzionale) Eseguo subito uno scatto per reattività immediata
        onNavTimerTick();
    });

    // Quando RILASCI un bottone
    connect(btn, &QPushButton::released, this, [this, action]() {
        // Rimuovo l'azione dalla lista
        activeNavActions.remove(action);

        // Se non ci sono più tasti premuti, fermo il timer
        if (activeNavActions.isEmpty()) {
            navTimer->stop();
        }
    });
}

void MainWindow::updateLayoutForMode(int mode)
{
    bool old3D = ui->dock3D->blockSignals(true);
    bool old4D = ui->dock4D->blockSignals(true);

    if (mode == 1) { // 3D Mode
        if (!ui->dock3D->isVisible()) ui->dock3D->show();
        if (ui->dock4D->isVisible())  ui->dock4D->close();
        ui->dock3D->raise(); // Porta in primo piano
    }
    else if (mode == 2) { // 4D Mode
        if (ui->dock3D->isVisible())  ui->dock3D->close();
        if (!ui->dock4D->isVisible()) ui->dock4D->show();
        ui->dock4D->raise(); // Porta in primo piano
    }

    ui->dock3D->blockSignals(old3D);
    ui->dock4D->blockSignals(old4D);
}


// ==========================================
// PROTECTED
// ==========================================


// Chiusura dell'applicazione. Era l'unico percorso distruttivo rimasto senza
// avviso: da qui la scena, la texture e il suono modificati sparivano senza
// domande, mentre load di un preset, NEW, reset e cambio di modalita' chiedono
// tutti conferma.
//
// Passa dalla stessa confirmDiscardUnsaved(ScopeScene) degli altri percorsi:
// il popup elenca cio' che e' sporco e, su "Save", apre il salvataggio dalla
// radice dell'albero. Nessun flag va riletto qui -- la decisione sta in
// hasUnsavedWork(): duplicarla farebbe divergere questo percorso dagli altri.
//
// Vale anche quando l'unica cosa fatta e' applicare una texture da libreria: la
// scena che la usa si perde comunque, e il salvataggio proposto -- dalla
// radice, ramo records/ -- la conserva per intero. Il ramo di destinazione non
// c'entra: e' il RECORD a rendere salvabile quel lavoro, non la superficie.
//
// Cancel (o la chiusura del popup) -> event->ignore(): la finestra resta
// aperta. Vale anche per il quit dal menu/Cmd-Q, che passa comunque da qui.
void MainWindow::closeEvent(QCloseEvent* event)
{
    // Registrazione in corso: chiudere adesso lascerebbe il file video
    // troncato e l'encoder a meta'. Si chiede prima di fermare REC.
    if (m_isRecording) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle("Recording in progress");
        box.setText("A video is being recorded.");
        box.setInformativeText("Stop the recording before quitting.");
        box.addButton("OK", QMessageBox::AcceptRole);
        box.exec();
        event->ignore();
        return;
    }

    if (!confirmDiscardUnsaved(ScopeScene)) {
        event->ignore();
        return;
    }

    QMainWindow::closeEvent(event);
}

void MainWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::WindowStateChange) {
        if (windowState() & Qt::WindowMinimized) {
            if (m_audioController && m_audioController->isPlaying()) {
                m_audioController->stopAll();
            }
        }
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::hideEvent(QHideEvent* event)
{
    // Ferma l'audio
    if (m_audioController && m_audioController->isPlaying()) {
        m_audioController->stopAll();
    }

    // Ferma il timer del flusso geodetico e memorizza che era attivo,
    // così possiamo ripristinarlo al ritorno in foreground.
    // Necessario su iOS: al ritorno dal background il QRhi può essere
    // valido ma le risorse Metal non ancora ripristinate -> grid vuota
    // -> falso popup di singolarita'.
    if (m_geoAnimTimer && m_geoAnimTimer->isActive()) {
        m_geoAnimTimer->stop();
        this->setProperty("geoAnimTimerWasRunning", true);
    }

    QMainWindow::hideEvent(event);
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);

    // Riavvia il timer geodetico solo se era stato fermato da hideEvent.
    // Il delay (300 ms) serve a dare al backend grafico il tempo di
    // ricreare le risorse GPU su iOS/Android prima del primo tick.
    if (this->property("geoAnimTimerWasRunning").toBool()) {
        this->setProperty("geoAnimTimerWasRunning", false);

        QTimer::singleShot(300, this, [this]() {
            // Ricontrolla che l'utente non abbia premuto STOP nel frattempo
            if (!m_btnStart || m_btnStart->text() != "STOP") return;
            if (!isVisible()) return;
            if (!ui->glWidget || !ui->glWidget->getRhi()) return;

            if (m_geoAnimTimer && !m_geoAnimTimer->isActive()) {
                m_geoAnimTimer->start();
            }
        });
    }
}

MainWindow::~MainWindow()
{
    delete ui;
}
