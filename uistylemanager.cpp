#include "uistylemanager.h"

#include <QScrollArea>
#include <QScroller>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QLabel>
#include <QLayout>
#include <QGroupBox>
#include <QSlider>
#include <QDockWidget>
#include <QMainWindow>
#include <QDockWidget>
#include <QApplication>
#include <QStyleFactory>

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

    // 3. IL TUO FOGLIO DI STILE (QSS)
    QString darkStyle = R"(
        /* --- GLOBAL --- */
        QWidget {
            background-color: #2D2D30;
            color: #F0F0F0;
            font-family: "Segoe UI", Arial, sans-serif;
            font-size: 12pt;
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

        /* --- SCROLLBAR --- */
        QScrollBar:vertical { border: none; background: #2D2D30; width: 14px; margin: 0px; }
        QScrollBar::handle:vertical { background: #555; min-height: 20px; border-radius: 7px; margin: 2px; }
        QScrollBar::handle:vertical:hover { background: #666; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }

        QScrollBar:horizontal { border: none; background: #2D2D30; height: 14px; margin: 0px; }
        QScrollBar::handle:horizontal { background: #555; min-width: 20px; border-radius: 7px; margin: 2px; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }

        /* --- MENU --- */
        QMenuBar { background-color: #2D2D30; color: #FFF; }
        QMenuBar::item:selected { background-color: #3E3E42; }
        QMenu { background-color: #2D2D30; border: 1px solid #555; color: #FFF; }
        QMenu::item:selected { background-color: #007ACC; }

        /* --- SLIDERS STANDARD (Costanti, Steps, ecc.) --- */
        QSlider::groove:horizontal {
            border: 1px solid #444;
            height: 8px; /* Più spesso del default, ma non enorme */
            background: #1E1E1E;
            border-radius: 4px;
        }
        QSlider::handle:horizontal {
            background: #D0D0D0;
            border: 1px solid #555;
            width: 20px;  /* Pallino ben afferrabile (quelli grandi sono 30px) */
            height: 20px;
            margin: -6px 0; /* Centra il pallino verticalmente sulla barra */
            border-radius: 10px;
        }
        QSlider::handle:horizontal:hover {
            background: #007ACC; /* Diventa azzurro quando ci passi sopra col mouse */
            border: 1px solid #007ACC;
        }
        QSlider::handle:horizontal:pressed {
            background: #0098FF; /* Si illumina quando lo clicchi */
        }
    )";

    // ==========================================================
    // 4. FIX: STILE CHECKBOX E RADIO BUTTON (Alto Contrasto e Pallino Solido)
    // ==========================================================
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    QString boxSize = "24px";
    QString radioRadius = "12px";
    QString radioBorder = "6px"; // Bordo spesso per rimpicciolire il pallino
#else
    QString boxSize = "18px";
    QString radioRadius = "9px";
    QString radioBorder = "4px";
#endif

    QString formStyle = QString(R"(
        /* --- CHECKBOX --- */
        QCheckBox { color: #FFFFFF; spacing: 10px; font-weight: bold; }
        QCheckBox::indicator {
            width: %1; height: %1;
            background-color: #111111; border: 2px solid #888888; border-radius: 4px;
        }
        QCheckBox::indicator:hover { border: 2px solid #007ACC; background-color: #1E1E1E; }
        QCheckBox::indicator:checked {
            background-color: #007ACC; border: 2px solid #007ACC;
            image: url("data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'><path d='M3 8.5 L6.5 12 L13 4' stroke='%23FFFFFF' stroke-width='2.5' fill='none' stroke-linecap='round' stroke-linejoin='round'/></svg>");
        }
        QCheckBox::indicator:disabled { background-color: #2D2D30; border: 2px solid #444444; color: #555555; }

        /* --- RADIO BUTTON --- */
        QRadioButton { color: #FFFFFF; spacing: 10px; }
        QRadioButton::indicator {
            width: %1; height: %1;
            background-color: #111111;
            border: 2px solid #888888;
            border-radius: %2;
        }
        QRadioButton::indicator:hover { border: 2px solid #007ACC; }

        /* IL TRUCCO DEL PALLINO SOLIDO */
        QRadioButton::indicator:checked {
            background-color: #007ACC; /* Azzurro pieno */
            border: %3 solid #222222;  /* Il bordo scuro "stringe" l'azzurro al centro! */
        }

        QRadioButton::indicator:disabled { background-color: #2D2D30; border: 2px solid #444444; }
    )").arg(boxSize, radioRadius, radioBorder);

    // 5. Applica tutti gli stili combinati alla finestra
    window->setStyleSheet(darkStyle + formStyle);
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

#ifdef Q_OS_ANDROID
    if (isExamplesDock) {
        minHeight = 600;
    } else {
        minHeight = content->sizeHint().height();
        if (minHeight < 400) minHeight = 450;
    }
#else
    minHeight = dock->minimumHeight();
    if (minHeight < 800) minHeight = 850;
#endif

    content->setMinimumHeight(minHeight);

    // Manteniamo la larghezza minima originale
    int minWidth = dock->minimumWidth();
    if (minWidth < 300) minWidth = 300;
    content->setMinimumWidth(minWidth);

    dock->setMinimumHeight(0); // Sblocca l'altezza del dock
    dock->setMinimumWidth(minWidth);

    // --- FIX: Se è il dock Esempi (QTreeWidget), NON avvolgerlo in una QScrollArea ---
    // Il QTreeWidget gestisce lo scroll nativamente. Avvolgerlo creerebbe
    // doppie scrollbar e conflitti di gesture (galleggiamento).
    if (isExamplesDock) {
        return;
    }
    // --------------------------------------------------------------------------------

    QScrollArea* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setWidget(content);

    dock->setWidget(scrollArea);
    QScroller::grabGesture(scrollArea->viewport(), QScroller::LeftMouseButtonGesture);
}

void UiStyleManager::compactForMobile(const QList<QWidget*>& containers, QWidget* panelColor, const QList<QWidget*>& sliderRows, const QList<QLabel*>& valueLabels) {
#ifdef Q_OS_ANDROID
    QString style = R"(
        QWidget { font-size: 13px; }
        QGroupBox { font-weight: bold; margin-top: 1ex; padding: 0px; border: 1px solid #999; }
        QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top center; padding: 0 2px; }
        QLabel { margin: 0px; padding: 0px; }
        QLineEdit { height: 30px; }
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
            edit->setMaximumHeight(45); edit->setMinimumHeight(40);
        }
    }
#endif
}

void UiStyleManager::setupBigSliders(QSlider* r, QSlider* g, QSlider* b, QSlider* alpha, QSlider* light, QSlider* speed3D, QSlider* speed4D) {
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

    int minH = 40;
    if(r) r->setMinimumHeight(minH);
    if(g) g->setMinimumHeight(minH);
    if(b) b->setMinimumHeight(minH);
    if(alpha) alpha->setMinimumHeight(minH);
    if(light) light->setMinimumHeight(minH);
    if(speed3D) speed3D->setMinimumHeight(minH);
    if(speed4D) speed4D->setMinimumHeight(minH);
}
