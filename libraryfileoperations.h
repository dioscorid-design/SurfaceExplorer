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

    void performCut(QTreeWidgetItem* targetItem = nullptr);
    void performCopy(QTreeWidgetItem* targetItem = nullptr);
    void performPasteExample();
    void performPasteTexture();
    void deleteSelected();
    void undoDelete();
    void copyPath(const QString &src, const QString &dst);
    void updateJsonTypeForFolder(const QString &filePath);
    void backupBeforeOverwrite(const QString &filePath);

private:
    MainWindow *m_mainWindow;
};

#endif // LIBRARYFILEOPERATIONS_H
