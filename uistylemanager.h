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
class QAbstractSpinBox;
class QMessageBox;
class QTabWidget;

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
    // Altezza minima degli slider dei dock: il pallino e' piu' alto della guida
    // e, lasciata a quella della guida, ne usciva tagliato sopra e sotto.
    static constexpr int kSliderMinHeight = 40;
    static void setupBigSliders(QSlider* r, QSlider* g, QSlider* b, QSlider* alpha, QSlider* light = nullptr, QSlider* speed3D = nullptr, QSlider* speed4D = nullptr, QSlider* fov = nullptr, QSlider* fov4D = nullptr);
    // Stile degli slider di VALORE (non colore) del dock Renderer: evidenziato
    // da acceso, grigio da spento. Unica sede: lo usano setupBigSliders e gli
    // slider aggiunti dopo (Headlight, Border Thickness).
    static QString valueSliderStyle();
    static void applyInputFieldsStyle(const QList<QWidget*>& fields);
    // contentFitsView: il contenuto e' largo quanto la vista (dock unico del
    // telefono), quindi niente margine destro per la barra di scorrimento.
    static void addScrollToDock(QDockWidget* dock, bool contentFitsView = false);
    static void applyConstraintStyle(QPlainTextEdit* editor, ConstraintState state);
    // searchEdit: il campo di ricerca in cima al dialogo. Passa di qui perche'
    // su mobile va allargato (l'altezza di default e' pensata per il mouse, non
    // per un dito) e questa e' la sede unica della distinzione desktop/mobile.
    static void setupDocumentationDialog(QDialog* dialog, QVBoxLayout* layout, QTextBrowser* browser, QPushButton* closeBtn,
                                         QLineEdit* searchEdit = nullptr);
    // MOBILE: sostituisce le frecce native di uno spinbox (intero o decimale)
    // con due QPushButton a forma di freccia, affiancati al campo. Su iOS il
    // campo diventa anche non editabile da tastiera: toccarlo (frecce native
    // comprese) apriva tastierino e menu di modifica senza poterli chiudere.
    // fieldWidth: larghezza fissa del campo; 0 = il campo prende il resto della
    // riga (dock Objects). editable: il campo si scrive anche (dock Objects),
    // con una tastiera che ha l'Invio, che conferma e la chiude. No-op su desktop.
    static void installMobileSpinButtons(QAbstractSpinBox* spin, int fieldWidth = 36,
                                         bool editable = false);
    static void styleMobileMenuButton(QPushButton* button);
    static void styleMobileOverflowMenu(QMenu* menu);
    static void applyRecordButtonStyle(QPushButton* btn);

    // Allarga i tasti di un QMessageBox con etichette-frase ("Save without
    // sound", "Don't save"): lo stile globale ne fissa la larghezza a 108px e
    // il testo piu' lungo viene TAGLIATO. Chiamare PRIMA di exec().
    // minWidth e' il valore CSS min-width, non la larghezza finale del tasto
    // (che vale minWidth + il padding del foglio globale, 18*2).
    static void widenMessageBoxButtons(QMessageBox* box, int minWidth = 100);

    // Linguette a tutta larghezza: si dividono la larghezza del tab widget e
    // l'ultima finisce sul suo bordo destro. Ricalcolate a ogni
    // ridimensionamento (la larghezza disponibile cambia fra desktop e mobile e
    // con la barra di scorrimento del dock).
    static void fillTabBarWidth(QTabWidget* tabs);
};

#endif // UISTYLEMANAGER_H
