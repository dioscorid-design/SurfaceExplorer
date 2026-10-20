// librarytreewidget.h - Gli alberi della Library (Shapes, Textures, Sounds,
// Records): un QTreeWidget in cui il clic su una CARTELLA non prende il focus.
// Promosso nel .ui per i quattro alberi.
#pragma once

#include <QPersistentModelIndex>
#include <QPoint>
#include <QTreeWidget>

#include "librarytreefocus.h"

class LibraryTreeWidget : public QTreeWidget
{
    Q_OBJECT
public:
    using QTreeWidget::QTreeWidget;

protected:
    // Durante un clic semplice su una cartella: nessuna selezione, ne' alla
    // pressione ne' al rilascio.
    QItemSelectionModel::SelectionFlags selectionCommand(const QModelIndex &index,
                                                         const QEvent *event = nullptr) const override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    // Trascinamento finito (spostamento, rilascio sul posto o annullato):
    // l'evidenziazione torna com'era prima della pressione.
    void startDrag(Qt::DropActions supportedActions) override;

private:
    bool isFolder(const QModelIndex &index) const;

    bool m_folderPress = false;      // clic sinistro senza Cmd/Shift su una cartella, in corso
    QPersistentModelIndex m_folder;
    QPoint m_pressPos;
    LibraryTreeFocus::Highlight m_pressHighlight;   // prima della pressione sinistra
};
