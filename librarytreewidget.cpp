// librarytreewidget.cpp - Il clic su una cartella della Library non prende il
// focus (vedi librarytreewidget.h).
#include "librarytreewidget.h"

#include <QApplication>
#include <QFileInfo>
#include <QMouseEvent>

// LE CARTELLE NON PRENDONO IL FOCUS. Qt seleziona la voce alla PRESSIONE: un
// clic su una cartella toglieva l'evidenziazione al preset a schermo, e
// rimetterla dopo (al rilascio, all'apertura del ramo) la faceva lampeggiare
// sulla cartella. Qui un clic sinistro semplice su una cartella non tocca
// selezione ne' elemento corrente; aprire e chiudere resta alla freccetta,
// al doppio clic e al tocco su mobile (onExampleItemClicked). La cartella si
// seleziona solo:
//  - con Cmd/Shift+clic, selezione multipla voluta;
//  - col tasto destro, che la rende bersaglio del menu (LibraryMenuController);
//  - quando un trascinamento parte davvero: Qt trascina le voci selezionate, e
//    senza si sarebbe portato dietro il preset.
// Finito il trascinamento l'evidenziazione torna sul preset a schermo, come
// dopo un comando del menu contestuale (startDrag).

bool LibraryTreeWidget::isFolder(const QModelIndex &index) const
{
    if (!index.isValid()) return false;
    const QTreeWidgetItem *item = itemFromIndex(index);
    return item && (item->data(0, Qt::UserRole + 10).isValid() || item->childCount() > 0);
}

QItemSelectionModel::SelectionFlags LibraryTreeWidget::selectionCommand(const QModelIndex &index,
                                                                       const QEvent *event) const
{
    if (m_folderPress) return QItemSelectionModel::NoUpdate;
    return QTreeWidget::selectionCommand(index, event);
}

void LibraryTreeWidget::mousePressEvent(QMouseEvent *event)
{
    const QModelIndex index = indexAt(event->position().toPoint());
    if (event->button() == Qt::LeftButton) m_pressHighlight = LibraryTreeFocus::highlightOf(this);
    const Qt::KeyboardModifiers multi = Qt::ControlModifier | Qt::ShiftModifier | Qt::MetaModifier;
    m_folderPress = event->button() == Qt::LeftButton && !(event->modifiers() & multi) && isFolder(index);
    if (!m_folderPress) {
        QTreeWidget::mousePressEvent(event);
        return;
    }
    m_folder = index;
    m_pressPos = event->position().toPoint();
    // Qt porta l'elemento corrente sulla voce premuta: lo si rimette dov'era
    // nello stesso evento, quindi nessun fotogramma lo mostra sulla cartella.
    // Senza autoscroll: spostarlo fa scrollTo, che riapre i genitori chiusi.
    const QPersistentModelIndex current = currentIndex();
    QTreeWidget::mousePressEvent(event);
    if (current.isValid() && currentIndex() != current) {
        const bool autoScroll = hasAutoScroll();
        setAutoScroll(false);
        selectionModel()->setCurrentIndex(current, QItemSelectionModel::NoUpdate);
        setAutoScroll(autoScroll);
    }
}

void LibraryTreeWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_folderPress) {
        // Un clic che trema di qualche pixel non e' un trascinamento, e dove il
        // trascinamento non c'e' (mobile) il dito che scorre non seleziona.
        if (dragDropMode() == QAbstractItemView::NoDragDrop || !(event->buttons() & Qt::LeftButton)
            || (event->position().toPoint() - m_pressPos).manhattanLength() < QApplication::startDragDistance())
            return;
        // Trascinamento vero: si porta la cartella, quindi la si seleziona ora.
        m_folderPress = false;
        if (m_folder.isValid())
            selectionModel()->setCurrentIndex(m_folder, QItemSelectionModel::ClearAndSelect
                                                            | QItemSelectionModel::Rows);
    }
    QTreeWidget::mouseMoveEvent(event);
}

void LibraryTreeWidget::mouseReleaseEvent(QMouseEvent *event)
{
    // Ancora col clic sulla cartella acceso: Qt chiede la selezione anche al
    // rilascio, e deve ricevere NoUpdate.
    QTreeWidget::mouseReleaseEvent(event);
    m_folderPress = false;
}

// Dove e' finito `path` se stava dentro (o era) una delle voci trascinate:
// il rilascio seleziona le voci spostate, che nella cartella nuova hanno lo
// stesso nome. Altrimenti resta com'e'.
static QString movedPath(const QString &path, const QStringList &dragged, const QStringList &moved)
{
    for (const QString &src : dragged) {
        if (src.isEmpty() || (path != src && !path.startsWith(src + QLatin1Char('/')))) continue;
        const QString name = QFileInfo(src).fileName();
        for (const QString &dst : moved)
            if (dst != src && QFileInfo(dst).fileName() == name) return dst + path.mid(src.size());
    }
    return path;
}

void LibraryTreeWidget::startDrag(Qt::DropActions supportedActions)
{
    // Il trascinamento e' un comando come quelli del menu contestuale: finito
    // -- spostamento, rilascio sul posto o annullato -- l'evidenziazione torna
    // com'era prima della pressione, cioe' sul preset a schermo. Senza, restava
    // sulla cartella trascinata anche lasciandola dov'era. Il rilascio puo'
    // aver ricostruito l'albero (refreshAndFocusPaths): si rimette per
    // percorso, seguendo il preset se si e' spostato con la cartella.
    LibraryTreeFocus::Highlight before = m_pressHighlight;
    QStringList dragged;
    for (QTreeWidgetItem *item : selectedItems()) dragged << LibraryTreeFocus::itemPath(item);
    QTreeWidget::startDrag(supportedActions);
    const QStringList moved = LibraryTreeFocus::highlightOf(this).paths;
    for (QString &p : before.paths) p = movedPath(p, dragged, moved);
    before.current = movedPath(before.current, dragged, moved);
    LibraryTreeFocus::restoreHighlight(this, before);
}
