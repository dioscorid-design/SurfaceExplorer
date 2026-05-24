#ifndef UISTYLEMANAGER_H
#define UISTYLEMANAGER_H

#include <QList>

// Forward declarations
class QMainWindow;
class QDockWidget;
class QSlider;
class QLabel;
class QWidget;
class QPlainTextEdit;
class QDialog;
class QVBoxLayout;
class QTextBrowser;
class QPushButton;
class QMenu;

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
    static void setupBigSliders(QSlider* r, QSlider* g, QSlider* b, QSlider* alpha, QSlider* light = nullptr, QSlider* speed3D = nullptr, QSlider* speed4D = nullptr);
    static void applyInputFieldsStyle(const QList<QWidget*>& fields);
    static void addScrollToDock(QDockWidget* dock);
    static void applyConstraintStyle(QPlainTextEdit* editor, ConstraintState state);
    static void setupDocumentationDialog(QDialog* dialog, QVBoxLayout* layout, QTextBrowser* browser, QPushButton* closeBtn);
    static void styleMobileMenuButton(QPushButton* button);
    static void styleMobileOverflowMenu(QMenu* menu);
    static void styleMobileScriptButtons(const QList<QPushButton*>& btns);
    static void applyToastShadow(QWidget* widget);
    static void applyToastStyle(QLabel* label, bool isError);
    static void applyRecordButtonStyle(QPushButton* btn);
    static void applyActiveToggleStyle(QPushButton* btn, bool isActive);
};

#endif // UISTYLEMANAGER_H
