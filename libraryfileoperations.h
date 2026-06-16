#ifndef LIBRARYFILEOPERATIONS_H
#define LIBRARYFILEOPERATIONS_H

#include <QObject>
#include <QString>

class MainWindow;
class QTreeWidgetItem;

class LibraryFileOperations : public QObject
{
    Q_OBJECT
public:
    explicit LibraryFileOperations(MainWindow *parent);

    // Esito della richiesta di conferma quando uno spostamento (move) trova a
    // destinazione un file con lo stesso nome.
    enum class CollisionChoice { Overwrite, KeepBoth, Cancel };

    void performCut(QTreeWidgetItem* targetItem = nullptr);
    void performCopy(QTreeWidgetItem* targetItem = nullptr);
    void performPasteExample();
    void performPasteTexture();
    void deleteSelected();
    void undoDelete();
    void copyPath(const QString &src, const QString &dst);
    void updateJsonTypeForFolder(const QString &filePath);
    void backupBeforeOverwrite(const QString &filePath);

    // Chiede all'utente cosa fare quando uno spostamento collide con un file
    // omonimo a destinazione. Se la scelta è KeepBoth, riscrive destPath con un
    // suffisso _copyN libero. applyToAllChoice (-1 = ancora da chiedere) ricorda
    // la scelta per i file successivi di un batch quando l'utente spunta
    // "Applica a tutti".
    CollisionChoice resolveMoveCollision(const QString &fileName, QString &destPath,
                                         bool multiple, int &applyToAllChoice);

private:
    MainWindow *m_mainWindow;
};

#endif // LIBRARYFILEOPERATIONS_H
