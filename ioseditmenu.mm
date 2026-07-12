// Menu di modifica nativo per iOS (vedi MobileInputFilter in mainwindow.cpp).
//
// Perché esiste: da iOS 16 UIMenuController è deprecato e su iOS 17+
// setMenuVisible: non mostra più nulla — quindi il menu del plugin Qt
// (QIOSEditMenu in qiostextinputoverlay.mm, che lo incapsula) è morto
// sugli iPhone recenti, mentre il QMenu widget dei QPlainTextEdit non
// viene renderizzato visibilmente su iOS. Presentiamo quindi noi il menu
// con l'API moderna UIEditMenuInteraction (orizzontale: non finisce mai
// sotto la tastiera come il vecchio callout).
//
// Le voci NON passano dal first responder: le suggestedActions di sistema
// vengono raccolte dalla catena della view dell'interaction (la QUIView,
// che non implementa nulla), e il dispatch via sendAction è filtrato dal
// canPerformAction di QIOSTextResponder (che ad es. nega selectAll: quando
// c'è già una selezione). Ogni voce esegue direttamente il metodo del
// widget Qt: cut()/copy()/paste()/selectAll()/undo()/redo().

#include "ioseditmenu.h"

#import <UIKit/UIKit.h>

#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QTextDocument>
#include <QWidget>
#include <QWindow>

// Editor a cui si riferisce il menu correntemente presentato.
static QPointer<QWidget> s_menuEditor;

@interface SEEditMenuHost : NSObject <UIEditMenuInteractionDelegate>
@property (nonatomic, strong) UIEditMenuInteraction *interaction;
@end

// Host singleton (vive per tutta la sessione), implementato in fondo al file.
static SEEditMenuHost* s_host = nil;

namespace {

enum class EditOp { Cut, Copy, Paste, SelectAll, Undo, Redo };

void performEditOp(EditOp op)
{
    QWidget* w = s_menuEditor.data();
    if (!w)
        return;
    if (auto* pte = qobject_cast<QPlainTextEdit*>(w)) {
        switch (op) {
        case EditOp::Cut:       pte->cut();       break;
        case EditOp::Copy:      pte->copy();      break;
        case EditOp::Paste:     pte->paste();     break;
        case EditOp::SelectAll: pte->selectAll(); break;
        case EditOp::Undo:      pte->undo();      break;
        case EditOp::Redo:      pte->redo();      break;
        }
    } else if (auto* le = qobject_cast<QLineEdit*>(w)) {
        switch (op) {
        case EditOp::Cut:       le->cut();       break;
        case EditOp::Copy:      le->copy();      break;
        case EditOp::Paste:     le->paste();     break;
        case EditOp::SelectAll: le->selectAll(); break;
        case EditOp::Undo:      le->undo();      break;
        case EditOp::Redo:      le->redo();      break;
        }
    }
}

// keepsOpen: la voce non chiude il menu (Select All, Undo, Redo) e dopo
// l'azione il menu viene ricaricato, cosi' le voci si adattano al nuovo
// stato (es. Cut/Copy compaiono appena Select All crea la selezione).
UIAction* editAction(NSString* title, EditOp op, bool enabled, bool keepsOpen = false)
{
    UIAction* a = [UIAction actionWithTitle:title
                                      image:nil
                                 identifier:nil
                                    handler:^(__kindof UIAction* action) {
        Q_UNUSED(action);
        performEditOp(op);
        if (keepsOpen)
            [s_host.interaction reloadVisibleMenu];
    }];
    UIMenuElementAttributes attrs = 0;
    if (!enabled)
        attrs |= UIMenuElementAttributesDisabled;
    if (keepsOpen)
        attrs |= UIMenuElementAttributesKeepsMenuPresented;
    a.attributes = attrs;
    return a;
}

} // namespace

@implementation SEEditMenuHost
- (UIMenu *)editMenuInteraction:(UIEditMenuInteraction *)interaction
           menuForConfiguration:(UIEditMenuConfiguration *)configuration
               suggestedActions:(NSArray<UIMenuElement *> *)suggestedActions
{
    Q_UNUSED(interaction);
    Q_UNUSED(configuration);
    Q_UNUSED(suggestedActions);

    // Stato letto al momento della presentazione: Cut/Copy solo con una
    // selezione attiva, Undo/Redo disabilitati se lo stack e' vuoto.
    bool hasSel = false, undoAv = false, redoAv = false;
    if (auto* pte = qobject_cast<QPlainTextEdit*>(s_menuEditor.data())) {
        hasSel = pte->textCursor().hasSelection();
        undoAv = pte->document()->isUndoAvailable();
        redoAv = pte->document()->isRedoAvailable();
    } else if (auto* le = qobject_cast<QLineEdit*>(s_menuEditor.data())) {
        hasSel = le->hasSelectedText();
        undoAv = le->isUndoAvailable();
        redoAv = le->isRedoAvailable();
    }

    NSMutableArray<UIMenuElement*>* items = [NSMutableArray array];
    if (hasSel) {
        [items addObject:editAction(@"Cut", EditOp::Cut, true)];
        [items addObject:editAction(@"Copy", EditOp::Copy, true)];
    }
    [items addObject:editAction(@"Paste", EditOp::Paste, true)];
    [items addObject:editAction(@"Select All", EditOp::SelectAll, true, true)];
    [items addObject:editAction(@"Undo", EditOp::Undo, undoAv, true)];
    [items addObject:editAction(@"Redo", EditOp::Redo, redoAv, true)];
    return [UIMenu menuWithChildren:items];
}
@end

void iosDismissEditMenu()
{
    if (s_host && s_host.interaction)
        [s_host.interaction dismissMenu];
}

void iosPresentEditMenu(QWidget* editor, const QPoint& globalPos)
{
    QWindow* win = editor->window()->windowHandle();
    if (!win)
        return;
    UIView* view = reinterpret_cast<UIView*>(win->winId()); // la QUIView della finestra
    if (!view)
        return;

    s_menuEditor = editor;

    if (!s_host)
        s_host = [SEEditMenuHost new];

    if (s_host.interaction && s_host.interaction.view != view) {
        [s_host.interaction.view removeInteraction:s_host.interaction];
        s_host.interaction = nil;
    }
    if (!s_host.interaction) {
        s_host.interaction = [[UIEditMenuInteraction alloc] initWithDelegate:s_host];
        [view addInteraction:s_host.interaction];
    }

    // Le coordinate QWindow e quelle della UIView coincidono (punti).
    const QPoint p = win->mapFromGlobal(globalPos);
    const CGPoint sourcePoint = CGPointMake(p.x(), p.y());

    // Presentazione al giro di runloop successivo: la chiamata puo' arrivare
    // dalla callback di un gesture recognizer (fine trascinamento pallini) e
    // una present sincrona li' verrebbe annullata dal cleanup del tocco.
    dispatch_async(dispatch_get_main_queue(), ^{
        UIEditMenuConfiguration* conf =
            [UIEditMenuConfiguration configurationWithIdentifier:nil
                                                     sourcePoint:sourcePoint];
        [s_host.interaction presentEditMenuWithConfiguration:conf];
    });
}
