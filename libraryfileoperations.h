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
    // destDirOverride: cartella di destinazione esplicita. Usata dalla voce
    // "Paste N Item(s)" del ramo PRINCIPALE, dove non c'e' un item selezionato da
    // cui dedurre la destinazione: su mobile getCurrentLibraryItem() e' nullo e le
    // vecchie euristiche sul nome del file sorgente fallivano -> paste inerte.
    void performPasteExample(const QString &destDirOverride = QString());
    void performPasteTexture(const QString &destDirOverride = QString());
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
    // Avvisa (solo su mobile) quando un paste non ha prodotto alcuna operazione:
    // copy/rename falliti o destinazione coincidente con l'origine. Su desktop
    // resta silenzioso per non intralciare il flusso rapido.
    void notifyPasteNoOp(const QString &destDir);

    MainWindow *m_mainWindow;
};

#endif // LIBRARYFILEOPERATIONS_H
