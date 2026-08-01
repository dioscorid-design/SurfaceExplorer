#include "uistylemanager.h"

#include <QScrollArea>
#include <QScroller>
#include <QMargins>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QLabel>
#include <QLayout>
#include <QGroupBox>
#include <QSlider>
#include <QDockWidget>
#include <QMainWindow>
#include <QApplication>
#include <QStyleFactory>
#include <QWidget>
#include <QDialog>
#include <QVBoxLayout>
#include <QTextBrowser>
#include <QPainter>
#include <QPixmap>
#include <QSpinBox>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QEvent>
#include <functional>
#include <QIcon>
#include <QMenu>

void UiStyleManager::applyDarkTheme(QMainWindow* window) {

    // 1. FORZA LO STILE FUSION (Fondamentale per vedere i colori scuri e i bordi)
    qApp->setStyle(QStyleFactory::create("Fusion"));

    // 2. Opzionale: Aggiusta la palette di base per i controlli non coperti dal CSS
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(45, 45, 48));
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, QColor(30, 30, 30));
    darkPalette.setColor(QPalette::AlternateBase, QColor(45, 45, 48));
    darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, QColor(45, 45, 48));
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::HighlightedText, Qt::black);
    qApp->setPalette(darkPalette);

    // --- SETUP DIMENSIONI FONT DINAMICHE ---
    QString baseFontSize = "12pt";
    QString inputFontSize = "10pt";

#if defined(Q_OS_MACOS)
    baseFontSize = "15pt";
    inputFontSize = "13pt";
#endif
    // ---------------------------------------

    // 3. IL TUO FOGLIO DI STILE (QSS)
    // Usa QString(...) e .arg() alla fine per iniettare i font
    QString darkStyle = QString(R"(
        /* --- GLOBAL --- */
        QWidget {
            background-color: #2D2D30;
            color: #F0F0F0;
            font-family: "Segoe UI", Arial, sans-serif;
            font-size: %1;
        }

        /* --- DOCK WIDGETS --- */
        QDockWidget {
            border: 1px solid #454545;
            titlebar-close-icon: url(:/icons/close.png);
            titlebar-normal-icon: url(:/icons/float.png);
        }
        QDockWidget::title {
            background: #3E3E42;
            padding-left: 5px;
            padding-top: 4px;
            border-bottom: 1px solid #111;
            font-weight: bold;
        }

        /* --- PULSANTI 3D (RAISED) --- */
        QPushButton {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #4E4E52, stop:1 #38383A);
            border: 2px solid #252526;
            border-top-color: #68686C;
            border-left-color: #68686C;
            border-right-color: #1E1E1E;
            border-bottom-color: #1E1E1E;
            border-radius: 4px;
            padding: 6px 12px;
            color: #FFFFFF;
            font-weight: bold;
        }
        QPushButton:hover { background-color: #55555A; }
        QPushButton:pressed, QPushButton:checked {
            background-color: #222222;
            border-top-color: #111;
            border-left-color: #111;
            border-right-color: #555;
            border-bottom-color: #555;
            padding-top: 7px;
            padding-left: 13px;
        }
        QPushButton:disabled {
            background-color: #2D2D30;
            color: #666;
            border: 1px solid #444;
        }

        /* --- INPUT 3D (SUNKEN/INCASSATI) --- */
        QLineEdit, QPlainTextEdit, QTextEdit, QSpinBox {
            background-color: #1E1E1E;
            border: 2px solid #3E3E42;
            border-top-color: #111;
            border-left-color: #111;
            border-bottom-color: #555;
            border-right-color: #555;
            border-radius: 2px;
            padding: 4px;
            color: #E0E0E0;
            selection-background-color: #264F78;
            font-size: %2;
        }
        QPlainTextEdit:focus, QLineEdit:focus { border: 2px solid #007ACC; }

        /* --- GROUP BOX --- */
        QGroupBox {
            border: 1px solid #555;
            border-radius: 5px;
            margin-top: 2ex;
            font-weight: bold;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top center;
            padding: 0 3px;
            background-color: #2D2D30;
            color: #BBB;
        }

        /* --- TABS --- */
        QTabWidget::pane { border: 1px solid #444; background: #2D2D30; }
        QTabBar::tab { background: #2D2D30; border: 1px solid #444; padding: 5px 10px; color: #AAA; }
        QTabBar::tab:selected { background: #3E3E42; color: #FFF; border-bottom-color: #3E3E42; }

/* --- TABS --- */
        QTabWidget::pane { border: 1px solid #444; background: #2D2D30; }
        QTabBar::tab { background: #2D2D30; border: 1px solid #444; padding: 5px 10px; color: #AAA; }
        QTabBar::tab:selected { background: #3E3E42; color: #FFF; border-bottom-color: #3E3E42; }
    )" // Chiude il primo blocco di testo

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
                                // --- TASTI SPINBOX MOBILE ---
                                // Su mobile i campi numerici non sono editabili
                                // da tastiera (vedi installMobileSpinButtons in
                                // mainwindow.cpp): il valore si cambia SOLO coi
                                // tasti, quindi devono essere bersagli veri.
                                // Le frecce native del QSpinBox sono nascoste
                                // (ButtonSymbols::NoButtons) e sostituite da due
                                // QPushButton con proprieta' "spinArrow": qui
                                // ricevono la forma di freccia, disegnata col
                                // carattere triangolare del testo.
                                R"(
        QPushButton[spinArrow="true"] {
            background-color: #3E3E42;
            border: 1px solid #555;
            border-radius: 4px;
            color: #E0E0E0;
            font-size: 13px;
            font-weight: bold;
            padding: 0px;
            margin: 0px;
        }
        QPushButton[spinArrow="true"]:pressed {
            background-color: #007ACC;
            border-color: #007ACC;
        }
        QPushButton[spinArrow="true"]:disabled {
            background-color: #2D2D30;
            border-color: #444;
            color: #666;
        }
        )"
                                // --- STILE SCROLLBAR MOBILE ---
                                R"(
        QScrollBar:vertical {
            background: transparent;
            width: 26px;
            margin: 0px;
            padding: 0px;
        }
        QScrollBar::handle:vertical {
            background-color: #666666;
            min-height: 28px;
            border-radius: 11px; /* 11px per una larghezza interna di 22px */
            margin: 2px;
            border: none; /* <--- QUESTO FORZA IL DISEGNO ROTONDO! */
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
            width: 0px;
            border: none;
            background: transparent;
            margin: 0px;
        }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
            background: none;
        }

        QScrollBar:horizontal {
            background: transparent;
            height: 22px;
            margin: 0px;
            padding: 0px;
        }
        QScrollBar::handle:horizontal {
            background-color: #666666;
            min-width: 28px;
            border-radius: 9px;  /* 9px per un'altezza interna di 18px */
            margin: 2px;
            border: none; /* <--- QUESTO FORZA IL DISEGNO ROTONDO! */
        }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
            height: 0px;
            width: 0px;
            border: none;
            background: transparent;
            margin: 0px;
        }
        QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
            background: none;
        }
    )"
#else
                                // --- STILE SCROLLBAR DESKTOP (Normali) ---
                                R"(
        QScrollBar:vertical { border: none; background: #2D2D30; width: 14px; margin: 0px; }
        QScrollBar::handle:vertical { background: #555; min-height: 20px; border-radius: 7px; margin: 2px; }
        QScrollBar::handle:vertical:hover { background: #666; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }

        QScrollBar:horizontal { border: none; background: #2D2D30; height: 14px; margin: 0px; }
        QScrollBar::handle:horizontal { background: #555; min-width: 20px; border-radius: 7px; margin: 2px; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }
    )"
#endif

                                // --- CONTINUAZIONE E FINE DELLO STILE ---
                                R"(
        /* --- MENU --- */
        QMenuBar { background-color: #2D2D30; color: #FFF; }
        QMenuBar::item:selected { background-color: #3E3E42; }
        QMenu { background-color: #2D2D30; border: 1px solid #555; color: #FFF; }
        QMenu::item:selected { background-color: #007ACC; }

        /* --- SLIDERS STANDARD (Costanti, Steps, ecc.) --- */
        QSlider::groove:horizontal {
            border: 1px solid #444;
            height: 8px;
            background: #1E1E1E;
            border-radius: 4px;
        }
        QSlider::handle:horizontal {
            background: #D0D0D0;
            border: 1px solid #555;
            width: 20px;
            height: 20px;
            margin: -6px 0;
            border-radius: 10px;
        }
        QSlider::handle:horizontal:hover {
            background: #007ACC;
            border: 1px solid #007ACC;
        }
        QSlider::handle:horizontal:pressed {
            background: #0098FF;
        }
    )").arg(baseFontSize, inputFontSize);

    // ==========================================================
    // 4. FIX: STILE CHECKBOX E RADIO BUTTON (Alto Contrasto e Pallino Solido)
    // ==========================================================
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    QString boxSize = "24px";
    QString radioRadius = "14px";
    QString radioBorder = "6px";
#else
    QString boxSize = "16px";
    QString radioRadius = "10px";
    QString radioBorder = "4px";
#endif

    QString formStyle = QString(R"(
        /* --- CHECKBOX --- */
        QCheckBox { color: #FFFFFF; spacing: 10px; font-weight: bold; }

        QCheckBox::indicator {
            width: %1; height: %1;
            background-color: #111111;
            border: 2px solid #888888;
            border-radius: 4px;
        }
        QCheckBox::indicator:unchecked {
            /* L'ARMA SEGRETA: PNG 1x1 trasparente in Base64 */
            image: url("data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNkYAAAAAYAAjCB0C8AAAAASUVORK5CYII=");
        }
        QCheckBox::indicator:hover {
            border: 2px solid #007ACC;
            background-color: #1E1E1E;
        }
        QCheckBox::indicator:checked {
            background-color: #007ACC;
            border: 2px solid #007ACC;
            image: url("data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'><path d='M3 8.5 L6.5 12 L13 4' stroke='%23FFFFFF' stroke-width='2.5' fill='none' stroke-linecap='round' stroke-linejoin='round'/></svg>");
        }
        QCheckBox::indicator:disabled {
            background-color: #2D2D30;
            border: 2px solid #444444;
            color: #555555;
        }

        /* --- RADIO BUTTON --- */
        QRadioButton { color: #FFFFFF; spacing: 10px; }

        QRadioButton::indicator {
            width: %1; height: %1;
            background-color: #111111;
            border: 2px solid #888888;
            border-radius: %2;
        }
        QRadioButton::indicator:unchecked {
            /* PNG 1x1 trasparente */
            image: url("data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNkYAAAAAYAAjCB0C8AAAAASUVORK5CYII=");
        }
        QRadioButton::indicator:hover {
            border: 2px solid #007ACC;
        }
        QRadioButton::indicator:checked {
            background-color: #007ACC;
            border: %3 solid #222222;
            /* PNG trasparente per evitare collassi anche nello stato acceso */
            image: url("data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNkYAAAAAYAAjCB0C8AAAAASUVORK5CYII=");
        }
        QRadioButton::indicator:disabled {
            background-color: #2D2D30;
            border: 2px solid #444444;
        }
    )").arg(boxSize, radioRadius, radioBorder);

    // ==========================================================
    // 5. STILE ALBERI (QTreeView) SPECIFICO PER PIATTAFORMA
    // ==========================================================
    QString treeStyle = R"(
        QTreeView {
            border: none;
            background-color: #2D2D30;
        }
    )";

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    // Distanza verticale "stile desktop" ma leggermente maggiorata (4px)
    treeStyle += R"(
        QTreeView::item {
            padding: 4px 5px;
        }
    )";
#endif

    // 6. Applica tutti gli stili combinati alla finestra
    window->setStyleSheet(darkStyle + formStyle + treeStyle);
}

void UiStyleManager::applyPlatformStyle(QMainWindow* window) {
#ifdef Q_OS_ANDROID
    QString menuStyle = R"(
        QMenu {
            background-color: #FAFAFA; border: 1px solid #BBBBBB; border-radius: 8px; padding: 4px;
        }
        QMenu::item {
            padding: 15px 30px; color: #222222; font-size: 22pt; min-height: 60px; min-width: 250px; margin: 2px 0px;
        }
        QMenu::item:selected { background-color: #B0D4F1; border-radius: 4px; color: #000000; }
        QMenu::separator { height: 2px; background-color: #DDDDDD; margin: 10px 10px; }
    )";
    window->setStyleSheet(menuStyle);
#else
    window->setStyleSheet(
        "QMenu { background-color: white; border: 1px solid gray; }"
        "QMenu::item { padding: 5px 20px; }"
        "QMenu::item:selected { background-color: #a8d1ff; }"
        );
#endif
}

void UiStyleManager::setupDockScroll(QDockWidget* dock, bool isExamplesDock) {
    if (!dock || !dock->widget()) return;

    QWidget* content = dock->widget();
    int minHeight = 0;

    // 1. Controllo UNIFICATO per la libreria (Android, iOS, Desktop)
    if (isExamplesDock) {
        minHeight = 0;
    } else {
        // 2. Altezze specifiche solo per i pannelli standard (Equazioni, Render, ecc.)
#ifdef Q_OS_ANDROID
        minHeight = content->sizeHint().height();
        if (minHeight < 400) minHeight = 450;
#else
        minHeight = dock->minimumHeight();
        if (minHeight < 800) minHeight = 850;
#endif
    }

    content->setMinimumHeight(minHeight);

    // Manteniamo la larghezza minima originale
    int minWidth = dock->minimumWidth();
    if (minWidth < 300) minWidth = 300;
    content->setMinimumWidth(minWidth);

    dock->setMinimumHeight(0); // Sblocca l'altezza del dock
    dock->setMinimumWidth(minWidth);

    if (isExamplesDock) {
        return; // Esce prima di creare la QScrollArea extra!
    }
    QScrollArea* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setWidget(content);

    dock->setWidget(scrollArea);
    QScroller::grabGesture(scrollArea->viewport(), QScroller::LeftMouseButtonGesture);
}

void UiStyleManager::compactForMobile(const QList<QWidget*>& containers) {
// Estendiamo la compattazione anche a iOS!
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    QString style = R"(
        QWidget { font-size: 13px; }
        QGroupBox { font-weight: bold; margin-top: 1ex; padding: 0px; border: 1px solid #999; }
        QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top center; padding: 0 2px; }
        QLabel { margin: 0px; padding: 0px; }
        QLineEdit { height: 30px; }

        /* --- INGRANDIMENTO SLIDER PER MOBILE --- */
        QSlider {
            min-height: 40px; /* Dà respiro verticale al layout per non tagliare il pallino */
        }
        QSlider::groove:horizontal {
            border: 1px solid #444;
            height: 8px;
            background: #1E1E1E;
            border-radius: 4px;
        }
        QSlider::handle:horizontal {
            background: #D0D0D0;
            border: 1px solid #555;
            width: 26px;       /* 1. Dimensione aggiornata */
            height: 26px;
            margin: -9px 0;    /* 2. Margine ricalcolato: -(26 - 8) / 2 = -9 */
            border-radius: 13px; /* 3. Esattamente la metà di 26 */
        }
        QSlider::handle:horizontal:pressed {
            background: #0098FF;
        }
    )";

    for (QWidget* w : containers) {
        if (!w) continue;
        w->setStyleSheet(style);

        QList<QLayout*> layouts = w->findChildren<QLayout*>();
        for (QLayout* l : layouts) {
            l->setSpacing(2);
            l->setContentsMargins(0, 2, 0, 2);
        }

        QList<QPushButton*> btns = w->findChildren<QPushButton*>();
        for (QPushButton* btn : btns) {
            btn->setFixedHeight(38);
            QFont f = btn->font(); f.setPointSize(10); btn->setFont(f);
        }

        QList<QPlainTextEdit*> textEdits = w->findChildren<QPlainTextEdit*>();
        for (QPlainTextEdit* edit : textEdits) {
            const QString on = edit->objectName();
            // Gli editor del tab Ray Marching sono gestiti da setupRaymarchTabMobile.
            if (on == "lineEquation" || on == "lineTexture" || on == "lineVariations")
                continue;
            edit->setMaximumHeight(75);
            edit->setMinimumHeight(70);
        }
    }
#endif
}

void UiStyleManager::setupRaymarchTabMobile(QWidget* equationsContainer) {
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    if (!equationsContainer) return;

    // Risolviamo i widget per objectName: nessuna dipendenza dall'oggetto ui.
    auto* lblEq        = equationsContainer->findChild<QLabel*>("lblEq");
    auto* lbl3DTexture = equationsContainer->findChild<QLabel*>("lbl3DTexture");
    auto* lbl2DTexture = equationsContainer->findChild<QLabel*>("lbl2DTexture");
    auto* panelRadio   = equationsContainer->findChild<QWidget*>("panelRadio");
    auto* lineEquation = equationsContainer->findChild<QPlainTextEdit*>("lineEquation");
    auto* lineTexture  = equationsContainer->findChild<QPlainTextEdit*>("lineTexture");
    auto* lineVars     = equationsContainer->findChild<QPlainTextEdit*>("lineVariations");

    // 1. Le tre label: altezza fissa e bassa, testo centrato verticalmente.
    for (QLabel* lbl : { lblEq, lbl3DTexture, lbl2DTexture }) {
        if (!lbl) continue;
        lbl->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        lbl->setMinimumHeight(30);
        lbl->setMaximumHeight(30);
        lbl->setAlignment(Qt::AlignCenter);   // centrato H e V
        lbl->setContentsMargins(0, 0, 0, 0);
    }

    // 2. Pannello radio Shell/Solid: più basso e compatto.
    if (panelRadio) {
        panelRadio->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        panelRadio->setMinimumHeight(30);
        panelRadio->setMaximumHeight(30);
        if (panelRadio->layout()) {
            panelRadio->layout()->setContentsMargins(0, 0, 0, 0);
            panelRadio->layout()->setSpacing(0);
        }
    }

    // 3. Editor più alti: equazione stretta, texture molto più ampie.
    if (lineEquation) { lineEquation->setMinimumHeight(60);  lineEquation->setMaximumHeight(QWIDGETSIZE_MAX); }
    if (lineTexture)  { lineTexture->setMinimumHeight(170);  lineTexture->setMaximumHeight(QWIDGETSIZE_MAX); }
    if (lineVars)     { lineVars->setMinimumHeight(170);     lineVars->setMaximumHeight(QWIDGETSIZE_MAX); }

    // 4. Ribilanciamo gli stretch ereditati dal .ui, altrimenti label e radio
    //    si riprendono lo spazio verticale a scapito degli editor.
    if (auto* vl24 = equationsContainer->findChild<QVBoxLayout*>("verticalLayout_24")) {
        vl24->setStretch(0, 0);   // lblEq
        vl24->setStretch(1, 1);   // lineEquation
        vl24->setStretch(2, 0);   // panelRadio
    }
    if (auto* vl26 = equationsContainer->findChild<QVBoxLayout*>("verticalLayout_26")) {
        vl26->setStretch(0, 0);   // lbl3DTexture
        vl26->setStretch(1, 5);   // lineTexture
        vl26->setStretch(2, 0);   // lbl2DTexture
        vl26->setStretch(3, 5);   // lineVariations
        vl26->setStretch(4, 0);   // btnTextureCode
    }
    if (auto* vl23 = equationsContainer->findChild<QVBoxLayout*>("verticalLayout_23")) {
        vl23->setStretch(0, 2);   // panelEquation (stretto)
        vl23->setStretch(1, 7);   // panelVariations (texture, più ampio)
        vl23->setStretch(2, 1);   // panelLimits
    }
#else
    Q_UNUSED(equationsContainer);
#endif
}

void UiStyleManager::setupBigSliders(QSlider* r, QSlider* g, QSlider* b, QSlider* alpha, QSlider* light, QSlider* speed3D, QSlider* speed4D, QSlider* fov, QSlider* fov4D) {
    QString baseStyle = R"(
        QSlider::groove:horizontal { border: 1px solid #999; height: 12px; border-radius: 6px; margin: 2px 0; }
        QSlider::handle:horizontal { background: white; border: 1px solid #5c5c5c; width: 30px; height: 30px; margin: -10px 0; border-radius: 15px; }
    )";

    if(r) r->setStyleSheet(baseStyle + "QSlider::groove:horizontal { background: #FF6666; }");
    if(g) g->setStyleSheet(baseStyle + "QSlider::groove:horizontal { background: #66FF66; }");
    if(b) b->setStyleSheet(baseStyle + "QSlider::groove:horizontal { background: #6666FF; }");
    if(alpha) alpha->setStyleSheet(baseStyle + "QSlider::groove:horizontal { background: #BBBBBB; }");
    if(light) light->setStyleSheet(baseStyle + "QSlider::groove:horizontal { background: #FFFFFF; }");

    // Nuovi slider di velocità
    if(speed3D) speed3D->setStyleSheet(baseStyle + "QSlider::groove:horizontal { background: #AAAAAA; }");
    if(speed4D) speed4D->setStyleSheet(baseStyle + "QSlider::groove:horizontal { background: #AAAAAA; }");
    if(fov) fov->setStyleSheet(baseStyle + "QSlider::groove:horizontal { background: #AAAAAA; }");
    if(fov4D) fov4D->setStyleSheet(baseStyle + "QSlider::groove:horizontal { background: #AAAAAA; }");

    int minH = 40;
    if(r) r->setMinimumHeight(minH);
    if(g) g->setMinimumHeight(minH);
    if(b) b->setMinimumHeight(minH);
    if(alpha) alpha->setMinimumHeight(minH);
    if(light) light->setMinimumHeight(minH);
    if(speed3D) speed3D->setMinimumHeight(minH);
    if(speed4D) speed4D->setMinimumHeight(minH);
    if(fov) fov->setMinimumHeight(minH);
    if(fov4D) fov4D->setMinimumHeight(minH);
}

void UiStyleManager::applyInputFieldsStyle(const QList<QWidget*>& fields) {
    if (fields.isEmpty()) return;

    // Prende il font di base dal primo elemento
    QFont boldFont = fields.first()->font();
    boldFont.setBold(true);

#if defined(Q_OS_MACOS)
    boldFont.setPointSize(14);
#endif

    // Applica il nuovo font a tutti i campi
    for (QWidget* field : fields) {
        if (field) {
            field->setFont(boldFont);
        }
    }
}

void UiStyleManager::addScrollToDock(QDockWidget* dock) {
    if (!dock) return;

    // Recupera il widget che contiene attualmente i bottoni/caselle
    QWidget* innerWidget = dock->widget();

    // Se c'è già una scrollarea o il widget è nullo, ci fermiamo
    if (!innerWidget || qobject_cast<QScrollArea*>(innerWidget)) return;

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    // La scrollbar mobile e' larga 26px e trasparente: senza margine destro i
    // controlli ancorati al bordo (bottoni di dock3D/dock4D) le finiscono sotto.
    // Il margine va messo QUI e non nel .ui: compactForMobile azzera i margini
    // di tutti i layout dei dock, e gira prima di questa funzione.
    {
        QMargins m = innerWidget->contentsMargins();
        m.setRight(m.right() + 26);
        innerWidget->setContentsMargins(m);
    }
#endif

    // Crea la nuova ScrollArea
    QScrollArea* scrollArea = new QScrollArea(dock);

    // Impostazioni estetiche e funzionali
    scrollArea->setWidget(innerWidget);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    // Assegna la scrollarea come nuovo widget principale del dock
    dock->setWidget(scrollArea);
}

void UiStyleManager::applyConstraintStyle(QPlainTextEdit* editor, ConstraintState state) {
    if (!editor) return;

    switch (state) {
    case ConstraintState::Active:
        editor->setStyleSheet("QPlainTextEdit { background-color: #1E1E1E; color: #FFFFFF; font-weight: bold; border: 1px solid #007ACC; }");
        break;
    case ConstraintState::Inactive:
        editor->setStyleSheet("QPlainTextEdit { background-color: #1E1E1E; color: #666666; border: 1px solid #3E3E42; } QPlainTextEdit:focus { border: 1px solid #007ACC; }");
        break;
    case ConstraintState::Default:
        editor->setStyleSheet("QPlainTextEdit { background-color: #1E1E1E; color: #FFFFFF; border: 1px solid #3E3E42; } QPlainTextEdit:focus { border: 1px solid #007ACC; }");
        break;
    case ConstraintState::Disabled:
        editor->setStyleSheet("QPlainTextEdit { background-color: #252526; color: #555555; border: 1px solid #333; } QPlainTextEdit:focus { border: 1px solid #007ACC; background-color: #1E1E1E; }");
        break;
    }
}

void UiStyleManager::setupDocumentationDialog(QDialog* dialog, QVBoxLayout* layout, QTextBrowser* browser, QPushButton* closeBtn) {
    if (!dialog || !layout || !browser || !closeBtn) return;

    // Stile del pulsante di chiusura
    closeBtn->setStyleSheet("padding: 12px; font-weight: bold; font-size: 16px; background-color: #333; color: white;");

#if defined(Q_OS_IOS)
    // --- COMPORTAMENTO iOS ---
    // FullScreen + Margini di sicurezza per evitare Notch e barra Home
    dialog->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    dialog->showFullScreen();
    layout->setContentsMargins(0, 45, 0, 20);
    layout->setSpacing(0);
    QScroller::grabGesture(browser->viewport(), QScroller::TouchGesture);
    QFont mobileFont = browser->font();
    mobileFont.setPointSize(mobileFont.pointSize() + 3);
    browser->setFont(mobileFont);

#elif defined(Q_OS_ANDROID)
    // --- COMPORTAMENTO ANDROID ---
    // Maximized mantiene la Status Bar intatta, evitando sbalzi di UI alla chiusura!
    dialog->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    dialog->showMaximized();
    layout->setContentsMargins(0, 0, 0, 0); // Nessun margine
    layout->setSpacing(0);
    QScroller::grabGesture(browser->viewport(), QScroller::TouchGesture);
    QFont mobileFont = browser->font();
    mobileFont.setPointSize(mobileFont.pointSize() + 3);
    browser->setFont(mobileFont);

#else
    // --- COMPORTAMENTO DESKTOP (Mac, Windows, Linux) ---
    dialog->setMinimumSize(320, 480);
    dialog->resize(800, 600);
#endif
}

void UiStyleManager::styleMobileMenuButton(QPushButton* button) {
    if (!button) return;

    button->setFlat(true);

    QPixmap pixmap(60, 60);
    pixmap.fill(Qt::transparent);
    {
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);

        int r = 4; // Raggio del puntino
        int cx = 30; // Centro orizzontale

        // --- NUOVO: Disegna prima l'ombra dei puntini ---
        // Usiamo un nero semi-trasparente e lo spostiamo leggermente in basso
        painter.setBrush(QColor(0, 0, 0, 180));
        painter.setPen(Qt::NoPen);
        int shadowOffset = 2; // Pixel di spostamento dell'ombra verso il basso

        painter.drawEllipse(QPoint(cx, 16 + shadowOffset), r, r);
        painter.drawEllipse(QPoint(cx, 30 + shadowOffset), r, r);
        painter.drawEllipse(QPoint(cx, 44 + shadowOffset), r, r);

        // --- ORIGINALE: Disegna i puntini bianchi esatti sopra ---
        painter.setBrush(Qt::white);

        painter.drawEllipse(QPoint(cx, 16), r, r);
        painter.drawEllipse(QPoint(cx, 30), r, r);
        painter.drawEllipse(QPoint(cx, 44), r, r);
    } // Il distruttore di QPainter applica definitivamente il disegno

    button->setIcon(QIcon(pixmap));
    button->setIconSize(QSize(60, 60));
    button->setFixedSize(60, 60);

    // Stile invariato con effetto "Ripple"
    button->setStyleSheet(
        "QPushButton { "
        "  background: transparent; "
        "  border: none; "
        "}"
        "QPushButton:pressed { "
        "  background-color: rgba(255, 255, 255, 0.2); "
        "  border-radius: 30px; "
        "}"
        "QPushButton::menu-indicator { "
        "  image: none; "
        "  width: 0px; "
        "}"
        );
}

void UiStyleManager::styleMobileOverflowMenu(QMenu* menu) {
    if (!menu) return;

    menu->setStyleSheet(
        "QMenu { font-size: 18px; background-color: #2D2D2D; border: 1px solid #444; }"
        "QMenu::item { padding: 20px 40px; color: white; border-bottom: 1px solid #333; }"
        "QMenu::item:selected { background-color: #007ACC; }"
        );
}

void UiStyleManager::applyRecordButtonStyle(QPushButton* btn) {
    if (btn) btn->setStyleSheet("QPushButton { color: red; font-weight: bold; }");
}

// MOBILE: due QPushButton a forma di freccia al posto delle frecce native.
//
// Perche' non bastava stilare le frecce native: su iOS toccarle dava il focus al
// QLineEdit interno dello spinbox, che apriva il tastierino numerico E il menu
// di modifica, senza poterli chiudere (app bloccata). Gli hint di input
// (ImhDigitsOnly) non risolvono: cambiano il tipo di campo per il text
// responder del plugin ma non gli impediscono di prendere il focus.
// Qui invece il campo diventa non editabile e senza focus, e il valore si
// cambia con due bottoni veri, che col plugin non hanno alcun rapporto.
void UiStyleManager::installMobileSpinButtons(QSpinBox* spin)
{
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    if (!spin || spin->property("mobileSpinButtons").toBool()) return;
    spin->setProperty("mobileSpinButtons", true);

    // Il campo resta come sola casella del numero: niente frecce native.
    spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spin->setFocusPolicy(Qt::NoFocus);
    spin->setAlignment(Qt::AlignCenter);
    if (QLineEdit* le = spin->findChild<QLineEdit*>()) {
        le->setReadOnly(true);
        le->setFocusPolicy(Qt::NoFocus);
        // Niente menu di modifica su un campo non editabile.
        le->setContextMenuPolicy(Qt::PreventContextMenu);
    }

    QWidget* parent = spin->parentWidget();
    if (!parent) return;
    // Il layout che contiene lo spinbox NON e' per forza quello del parent
    // widget: nel dialogo del recorder i campi stanno in un QFormLayout che e'
    // annidato dentro il QVBoxLayout di scrollContent. Cerchiamo il layout che
    // lo contiene davvero, o l'inserimento finirebbe nel posto sbagliato.
    QLayout* lay = nullptr;
    for (QLayout* cand : parent->findChildren<QLayout*>(QString(), Qt::FindChildrenRecursively)) {
        if (cand->indexOf(spin) >= 0) { lay = cand; break; }
    }
    if (!lay && parent->layout() && parent->layout()->indexOf(spin) >= 0)
        lay = parent->layout();
    if (!lay) return;

    // Lo spinbox viene sostituito, nella sua posizione, da una riga
    // [ giu ] [ campo ] [ su ]: cosi' il numero resta centrato fra i due tasti.
    auto* row = new QWidget(parent);
    auto* rowLay = new QHBoxLayout(row);
    rowLay->setContentsMargins(0, 0, 0, 0);
    rowLay->setSpacing(4);
    // La riga non deve crescere in altezza oltre i tasti: e' il campo, con il
    // suo padding, a spingerla in basso fin sopra l'etichetta sottostante.
    spin->setMaximumHeight(32);
    row->setMaximumHeight(32);

    auto makeArrow = [&](const QString& glyph) {
        auto* b = new QPushButton(glyph, row);
        b->setProperty("spinArrow", true);   // aggancia il QSS mobile
        b->setFocusPolicy(Qt::NoFocus);      // mai focus: nessuna tastiera
        b->setAutoRepeat(true);              // tenere premuto scorre i valori
        b->setAutoRepeatDelay(400);
        b->setAutoRepeatInterval(120);
        // 32px: bersaglio ancora comodo al dito ma contenuto. A 46 la riga
        // cresceva troppo in altezza e il tasto giu' finiva sopra l'etichetta
        // del gruppo sottostante.
        b->setFixedSize(32, 32);
        return b;
    };
    // Glifi triangolari: la forma della freccia la da' il carattere, cosi' non
    // servono icone da caricare ne' disegno custom.
    QPushButton* btnDown = makeArrow(QString::fromUtf8("▼"));
    QPushButton* btnUp   = makeArrow(QString::fromUtf8("▲"));

    // Il campo mostra un numero di 1-2 cifre: non serve piu' largo di cosi'.
    // Con una larghezza FISSA (non solo minima) il layout non puo' allargarlo
    // per riempire lo spazio, che e' cio' che spingeva la riga oltre il gruppo
    // e faceva finire il tasto giu' sopra l'etichetta sottostante.
    spin->setFixedWidth(36);

    // Posizione originale, letta PRIMA di rimuovere lo spinbox dal layout.
    const int boxIdx = lay->indexOf(spin);
    int formRow = -1;
    QFormLayout::ItemRole formRole = QFormLayout::FieldRole;
    auto* form = qobject_cast<QFormLayout*>(lay);
    if (form) form->getWidgetPosition(spin, &formRow, &formRole);

    // Lo spinbox passa dentro la riga: addWidget lo riparenta e lo toglie da
    // solo dal layout precedente.
    // Nessuno stretch sul campo (ha larghezza fissa) e uno stretch finale che
    // assorbe lo spazio in piu': il gruppo tasti+campo resta compatto a
    // sinistra invece di dilatarsi per tutta la riga.
    rowLay->addWidget(btnDown);
    rowLay->addWidget(spin);
    rowLay->addWidget(btnUp);
    rowLay->addStretch(1);

    // La riga prende il posto che aveva lo spinbox.
    if (auto* box = qobject_cast<QBoxLayout*>(lay)) {
        box->insertWidget(boxIdx < 0 ? box->count() : boxIdx, row);
    } else if (form && formRow >= 0) {
        // In un QFormLayout lo spinbox e' il campo di una riga: si sostituisce
        // il widget di quella riga, o la label resterebbe spaiata.
        form->setWidget(formRow, formRole, row);
    } else {
        lay->addWidget(row);
    }

    QObject::connect(btnUp,   &QPushButton::clicked, spin, &QSpinBox::stepUp);
    QObject::connect(btnDown, &QPushButton::clicked, spin, &QSpinBox::stepDown);

    // I tasti seguono lo stato del campo: quando lo spinbox e' disabilitato
    // (ambito "All" sul selettore Mesh) devono spegnersi con lui.
    auto syncEnabled = [spin, btnUp, btnDown]() {
        btnUp->setEnabled(spin->isEnabled());
        btnDown->setEnabled(spin->isEnabled());
    };
    syncEnabled();
    // enabledChange non e' un segnale: si osserva l'evento sul widget.
    class EnabledWatcher : public QObject {
    public:
        EnabledWatcher(QObject* p, std::function<void()> f) : QObject(p), fn(std::move(f)) {}
        bool eventFilter(QObject*, QEvent* e) override {
            if (e->type() == QEvent::EnabledChange) fn();
            return false;
        }
        std::function<void()> fn;
    };
    spin->installEventFilter(new EnabledWatcher(spin, syncEnabled));
#else
    Q_UNUSED(spin);
#endif
}
