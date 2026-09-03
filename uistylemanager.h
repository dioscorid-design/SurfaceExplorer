#ifndef UISTYLEMANAGER_H
#define UISTYLEMANAGER_H

#include <QList>

// Forward declarations
class QMainWindow;
class QDockWidget;
class QSlider;
class QWidget;
class QPlainTextEdit;
class QDialog;
class QVBoxLayout;
class QTextBrowser;
class QLineEdit;
class QPushButton;
class QMenu;
class QSpinBox;
class QMessageBox;

class UiStyleManager
{
public:

    enum class ConstraintState {
        Active,
        Inactive,
        Default,
        Disabled
    };

    static void applyDarkTheme(QMainWindow* window);
    static void applyPlatformStyle(QMainWindow* window);
    static void setupDockScroll(QDockWidget* dock, bool isExamplesDock = false);
    static void compactForMobile(const QList<QWidget*>& containers);
    static void setupRaymarchTabMobile(QWidget* equationsContainer);
    static void setupBigSliders(QSlider* r, QSlider* g, QSlider* b, QSlider* alpha, QSlider* light = nullptr, QSlider* speed3D = nullptr, QSlider* speed4D = nullptr, QSlider* fov = nullptr, QSlider* fov4D = nullptr);
    static void applyInputFieldsStyle(const QList<QWidget*>& fields);
    static void addScrollToDock(QDockWidget* dock);
    static void applyConstraintStyle(QPlainTextEdit* editor, ConstraintState state);
    // searchEdit: il campo di ricerca in cima al dialogo. Passa di qui perche'
    // su mobile va allargato (l'altezza di default e' pensata per il mouse, non
    // per un dito) e questa e' la sede unica della distinzione desktop/mobile.
    static void setupDocumentationDialog(QDialog* dialog, QVBoxLayout* layout, QTextBrowser* browser, QPushButton* closeBtn,
                                         QLineEdit* searchEdit = nullptr);
    // MOBILE: sostituisce le frecce native di un QSpinBox con due QPushButton a
    // forma di freccia, affiancati al campo. Su iOS il campo diventa anche non
    // editabile da tastiera: toccarlo (frecce native comprese) apriva tastierino
    // e menu di modifica senza poterli chiudere. No-op su desktop.
    static void installMobileSpinButtons(QSpinBox* spin);
    static void styleMobileMenuButton(QPushButton* button);
    static void styleMobileOverflowMenu(QMenu* menu);
    static void applyRecordButtonStyle(QPushButton* btn);

    // Allarga i tasti di un QMessageBox con etichette-frase ("Save without
    // sound", "Don't save"): lo stile globale ne fissa la larghezza a 108px e
    // il testo piu' lungo viene TAGLIATO. Chiamare PRIMA di exec().
    // minWidth e' il valore CSS min-width, non la larghezza finale del tasto
    // (che vale minWidth + il padding del foglio globale, 18*2).
    static void widenMessageBoxButtons(QMessageBox* box, int minWidth = 100);
};

#endif // UISTYLEMANAGER_H
