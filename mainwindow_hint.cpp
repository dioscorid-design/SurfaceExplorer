// mainwindow_hint.cpp - MainWindow: suggerimento in sovrimpressione (hint) e ricerca
// nella guida.
// Parte della classe MainWindow divisa per argomento (mainwindow_p.h).
#include "mainwindow_p.h"


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
    // Nell'ordine in cui compaiono a schermo.
    const QList<QPair<QString, QString>> all = {
        { QStringLiteral("Surface"),    m_currentHintText.trimmed() },
        { QStringLiteral("Texture"),    m_currentTextureHintText.trimmed() },
        { QStringLiteral("Background"), m_currentBgTextureHintText.trimmed() },
    };

    // Vuoti fuori, e identici una volta sola: succede quando un record ha
    // ereditato il testo della texture da cui e' nato, o quando superficie e
    // sfondo usano la stessa texture. Mostrarlo due volte sarebbe solo rumore.
    QList<QPair<QString, QString>> shown;
    for (const auto &p : all) {
        if (p.second.isEmpty()) continue;
        bool dup = false;
        for (const auto &s : shown) if (s.second == p.second) { dup = true; break; }
        if (!dup) shown << p;
    }
    if (shown.isEmpty()) return QString();

    // Ogni legenda dice di CHI sono i suoi slider, anche da sola: senza,
    // "Sliders / F: ..." non diceva se F agisce sulla forma, sulla texture o
    // sullo sfondo. Il ruolo si mette QUI, non nel preset: una texture della
    // Library puo' finire sulla superficie o sullo sfondo. "Sliders" diventa
    // "Texture sliders", "Slider A: ..." "Surface slider A: ...".
    static const QRegularExpression head(QStringLiteral("^Sliders?\\b"));
    QStringList lines;
    for (const auto &s : shown) {
        QString text = s.second;
        const QRegularExpressionMatch m = head.match(text);
        if (m.hasMatch()) lines << text.replace(0, m.capturedLength(), s.first + " " + m.captured(0).toLower());
        else              lines << s.first + " - " + text;
    }
    return lines.join("\n");
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

QVector<MainWindow::DocHit> MainWindow::searchDocumentation(const QString &needle) const
{
    QVector<DocHit> hits;
    const QString term = needle.trimmed();
    if (term.isEmpty()) return hits;

    // Le pagine si ENUMERANO dal .qrc invece di elencarle a mano: il manuale
    // cresce (17 file oggi) e una lista scritta qui nascerebbe incompleta al
    // primo capitolo aggiunto -- lo stesso motivo per cui il tasto Close/Back
    // usa una regola posizionale e non un elenco di pagine.
    QDirIterator it(":/docs", {"*.html"}, QDir::Files);
    while (it.hasNext()) {
        const QString path = it.next();

        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        const QString html = QString::fromUtf8(f.readAll());

        // Si cerca nel TESTO, non nell'HTML: cercando nel sorgente un termine
        // come "color" comparirebbe in ogni attributo di stile, e i risultati
        // sarebbero quasi tutti falsi. toPlainText() risolve anche le entita'
        // (&mdash;, &amp;) che altrimenti spezzerebbero le parole.
        const QString text = QTextDocumentFragment::fromHtml(html).toPlainText();

        int count = 0;
        int first = -1;
        int from = 0;
        while (true) {
            const int at = text.indexOf(term, from, Qt::CaseInsensitive);
            if (at < 0) break;
            if (first < 0) first = at;
            ++count;
            from = at + term.length();
        }
        if (!count) continue;

        DocHit h;
        h.file  = "qrc" + path;   // QDirIterator da ":/docs/...", il browser vuole "qrc:/docs/..."
        h.count = count;

        // Titolo della pagina: e' l'etichetta che l'utente riconosce. Il <title>
        // porta il prefisso del prodotto ("Surface Explorer - X"), che ripetuto
        // su ogni riga sarebbe rumore: si tiene la parte dopo il separatore.
        QRegularExpression titleRe("<title>(.*?)</title>",
                                   QRegularExpression::DotMatchesEverythingOption
                                   | QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch tm = titleRe.match(html);
        if (tm.hasMatch()) {
            h.title = QTextDocumentFragment::fromHtml(tm.captured(1)).toPlainText().trimmed();
            const int sep = h.title.indexOf(QRegularExpression("\\s+[-\\x{2013}\\x{2014}]\\s+"));
            if (sep > 0) h.title = h.title.mid(sep).trimmed();
            h.title.remove(QRegularExpression("^[-\\x{2013}\\x{2014}]\\s*"));
        }
        if (h.title.isEmpty()) h.title = QFileInfo(path).baseName();

        // Frammento di contesto attorno alla PRIMA occorrenza, cosi' si capisce
        // se la pagina parla davvero di cio' che si cerca prima di aprirla.
        const int ctxStart = qMax(0, first - 60);
        const int ctxEnd   = qMin(text.length(), first + term.length() + 90);
        h.context = text.mid(ctxStart, ctxEnd - ctxStart).simplified();
        if (ctxStart > 0)            h.context.prepend(QString::fromUtf8("\xE2\x80\xA6 "));
        if (ctxEnd < text.length())  h.context.append(QString::fromUtf8(" \xE2\x80\xA6"));

        hits.append(h);
    }

    // Piu' occorrenze = pagina piu' pertinente; a parita', ordine alfabetico
    // per un elenco stabile fra una ricerca e l'altra.
    std::sort(hits.begin(), hits.end(), [](const DocHit &a, const DocHit &b) {
        if (a.count != b.count) return a.count > b.count;
        return a.title.localeAwareCompare(b.title) < 0;
    });
    return hits;
}

void MainWindow::showSceneHint(const QString &text, float seconds)
{
    // Memorizzato anche se non c'e' scena da decorare: e' il messaggio del
    // record corrente e va riscritto tale e quale a un eventuale risalvataggio.
    m_currentHintText = text.trimmed();
    m_currentHintSeconds = seconds;

    if (!ui->glWidget) return;

    if (composedHintText().isEmpty()) { hideSceneHint(); return; }

    if (!m_hintOverlay) {
        // Figlia del glWidget: sta SOPRA la scena senza toccare il render loop
        // (e quindi senza finire nei frame esportati, composti offscreen).
        m_hintOverlay = new QLabel(ui->glWidget);
        m_hintOverlay->setObjectName("sceneHintOverlay");
        m_hintOverlay->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_hintOverlay->setWordWrap(true);
        // Testo SEMPLICE: un "\n" nel messaggio va a capo dove vogliamo noi,
        // invece di lasciare la spezzatura alla larghezza della finestra (che
        // tagliava "Slider / E" a meta'). Esplicito anche perche' l'auto-detect
        // di Qt interpreterebbe come HTML un hintText che contenesse dei tag.
        m_hintOverlay->setTextFormat(Qt::PlainText);
        m_hintOverlay->setTextInteractionFlags(Qt::NoTextInteraction);
        // CHIUSURA AL CLICK / TOCCO. L'overlay resta TRASPARENTE al mouse e la
        // chiusura si intercetta sul glWidget (vedi il filtro poco sotto):
        // installare il filtro sulla QLabel funziona su desktop ma NON su iOS,
        // dove una widget figlia di una superficie RHI/OpenGL non riceve il
        // mouse sintetizzato dal tocco. Il glWidget invece i click li riceve
        // sempre -- e' lui a gestire rotazioni e pan -- quindi si guarda li' se
        // il punto premuto cade dentro il rettangolo del messaggio.
        m_hintOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_hintOverlay->setCursor(Qt::PointingHandCursor);
        m_hintOverlay->setStyleSheet(
            "QLabel#sceneHintOverlay {"
            "  color: #ffffff;"
            "  background-color: rgba(0, 0, 0, 170);"
            "  border: 1px solid rgba(255, 255, 255, 90);"
            "  border-radius: 8px;"
            // Padding verticale contenuto: l'altezza la calcola heightForWidth()
            // sul testo riflussato (vedi repositionSceneHint), quindi ogni pixel
            // qui sopra e sotto si aggiunge a quello -- con 10px il riquadro
            // mostrava una fascia vuota sopra e sotto le righe.
            "  padding: 6px 16px;"
            "  font-size: 15px;"
            "  font-weight: bold;"
            "}");
        // Riposiziona il messaggio quando la scena cambia dimensione. Filtro
        // dedicato (non un eventFilter su MainWindow) per non intrecciarsi con
        // i filtri gia' installati su tastiera/scroll mobile.
        class HintResizeWatcher : public QObject {
        public:
            explicit HintResizeWatcher(MainWindow *w) : QObject(w), m_win(w) {}
        protected:
            bool eventFilter(QObject *obj, QEvent *ev) override {
                if (ev->type() == QEvent::Resize) m_win->repositionSceneHint();

                // CLICK/TOCCO SUL MESSAGGIO -> lo chiude. Si intercetta qui e
                // non sulla QLabel per la ragione detta sopra (iOS). Si
                // consuma solo il premere DENTRO il riquadro: fuori l'evento
                // prosegue e la scena ruota come sempre.
                if (ev->type() == QEvent::MouseButtonPress
                    && m_win->m_hintOverlay && m_win->m_hintOverlay->isVisible()) {
                    auto *me = static_cast<QMouseEvent *>(ev);
                    if (m_win->m_hintOverlay->geometry().contains(me->position().toPoint())) {
                        m_win->hideSceneHint();
                        m_swallowRelease = true;
                        return true;   // non arriva a rotazioni/pan
                    }
                }

                // ...e si mangia anche il RELEASE di quel click: appartiene al
                // messaggio, non alla vista. (In origine era l'unica difesa: il
                // release orfano veniva preso per un trascinamento, emetteva
                // userMovedView e la scena passava per MODIFICATA. Oggi
                // InputHandler ignora da se' i release senza press -- vedi
                // takeMouseMovedView -- e questo resta come seconda cintura.)
                if (ev->type() == QEvent::MouseButtonRelease && m_swallowRelease) {
                    m_swallowRelease = false;
                    return true;
                }
                return QObject::eventFilter(obj, ev);
            }
        private:
            MainWindow *m_win;
            bool m_swallowRelease = false;
        };
        ui->glWidget->installEventFilter(new HintResizeWatcher(this));

        // CHIUSURA COL TOCCO (iOS/Android). Il filtro qui sopra copre il mouse,
        // ma su mobile il gesto viene consumato da InputHandler::handleTouch e
        // nessun evento di mouse viene mai sintetizzato: il tocco arriva solo
        // da questo segnale, emesso da GLWidget::event sul TouchBegin.
        connect(ui->glWidget, &GLWidget::scenePressedAt, this,
                [this](const QPoint &pos) {
            if (m_hintOverlay && m_hintOverlay->isVisible()
                && m_hintOverlay->geometry().contains(pos))
                hideSceneHint();
        });
    }

    if (!m_hintTimer) {
        m_hintTimer = new QTimer(this);
        m_hintTimer->setSingleShot(true);
        connect(m_hintTimer, &QTimer::timeout, this, &MainWindow::hideSceneHint);
    }

    m_hintOverlay->setText(composedHintText());
    repositionSceneHint();
    m_hintOverlay->show();
    m_hintOverlay->raise();

    if (seconds > 0.0f) {
        m_hintTimer->start(int(seconds * 1000.0f));
    } else {
        m_hintTimer->stop(); // 0 o negativo = resta finche' non lo nasconde qualcuno
    }
}

void MainWindow::hideSceneHint()
{
    if (m_hintTimer) m_hintTimer->stop();
    if (m_hintOverlay) m_hintOverlay->hide();
}

void MainWindow::repositionSceneHint()
{
    if (!m_hintOverlay || !ui->glWidget) return;

    const int margin = 24;
    const int maxW = qMax(160, int(ui->glWidget->width() * 0.8));

    // PRIMA DI MISURARE: togliere i vincoli della chiamata PRECEDENTE. Le
    // setFixedWidth/setFixedHeight qui sotto fissano minimum == maximum, e
    // restano appiccicati al widget: al messaggio successivo sizeHint() e
    // heightForWidth() venivano calcolati su una label ancora bloccata alle
    // dimensioni del messaggio VECCHIO, quindi un hint corto ereditava il
    // riquadro di uno lungo (enorme rispetto al suo testo).
    m_hintOverlay->setMinimumSize(0, 0);
    m_hintOverlay->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

    // LARGHEZZA. sizeHint() di una QLabel con wordWrap misura il testo su UNA
    // riga: e' il massimo utile, e va limitato allo spazio disponibile.
    // (Un tentativo di dimensionare sulla RIGA PIU' LUNGA con QFontMetrics e'
    // stato scartato: sui desktop e su iPad produceva riquadri enormi.)
    const int w = qMin(maxW, m_hintOverlay->sizeHint().width());
    m_hintOverlay->setFixedWidth(w);

    // ALTEZZA. adjustSize() da solo NON basta con il wordWrap: a larghezza
    // fissata continua a usare l'altezza del sizeHint (una riga o poco piu'),
    // quindi un messaggio lungo veniva TAGLIATO -- si vedeva su iPhone, dove
    // maxW e' stretto e gli hint multi-riga ("Sliders\nE: ...\nF: ...")
    // occupano parecchie righe. heightForWidth() e' il meccanismo previsto da
    // Qt proprio per questo: chiede alla label quanto e' alta ALLA LARGHEZZA
    // che le abbiamo imposto, contenuti riflussati.
    // heightForWidth() vuole la larghezza TOTALE del widget e restituisce
    // un'altezza che COMPRENDE GIA' i margini del contenuto (il padding del
    // foglio di stile). Sottrarli in ingresso e riaggiungerli in uscita --
    // come faceva la prima versione di questo fix -- li contava DUE volte, e
    // il riquadro mostrava una fascia vuota sopra e sotto il testo.
    int h = m_hintOverlay->heightForWidth(w);
    if (h <= 0) h = m_hintOverlay->sizeHint().height();   // wordWrap disattivo

    // Non superare l'altezza della scena: meglio un messaggio fitto in alto che
    // uno che esce dal fondo (e sul telefono in orizzontale ci sta poco).
    const int maxH = qMax(48, ui->glWidget->height() - 2 * margin);
    m_hintOverlay->setFixedHeight(qMin(h, maxH));

    // In alto a sinistra: non copre l'oggetto, che sta al centro della scena.
    m_hintOverlay->move(margin, margin);
}
