// libraryfolders.h - La cartella della LIBRERIA sul disco: dove sta, come si
// riconosce, se vive dentro un servizio di sincronizzazione, e la copia dei
// preset di fabbrica (le risorse :/library/presets) nei suoi rami. Nessuno
// stato: le scelte dell'utente stanno nelle impostazioni (QSettings).
// Gli alberi della Library e i dialoghi di scelta restano a MainWindow.
#pragma once

#include <QList>
#include <QString>

class QDir;
class QWidget;

namespace LibraryFolders {

// La radice della libreria: su desktop quella scelta dall'utente
// ("libraryRootPath"), su iOS e Android una cartella fissa dei Documenti.
QString root();
// La cartella e' una radice di libreria: contiene almeno uno dei rami
// (vedi branches(); anche con la maiuscola).
bool isLibraryRoot(const QDir &dir);
// Il ramo che contiene `path` (file o cartella), per POSIZIONE rispetto alle
// radici dei rami -- la cartella scelta con Change Folder (pathSurfaces, ...)
// e <radice>/<ramo> --, non per nome: una sottocartella "Surfaces" dentro
// records/ resta records. None = fuori da tutti i rami.
enum class Branch { None, Surfaces, Textures, Sounds, Records, Scenes };
Branch branchOf(const QString &path);
// I RAMI, sede unica: per ciascuno la chiave delle impostazioni (la cartella
// scelta con Change Folder), la cartella sotto la radice -- che e' anche
// quella dei preset di fabbrica, :/library/presets/<cartella> --, il nome
// della linguetta e quello di una sua voce nel menu ("Rename Scene...").
// Ordine = ordine delle linguette. Un ramo nuovo si aggiunge qui (e in
// MainWindow::libraryBranches, per il suo albero): le copie sparse della
// tabella avevano dimenticato le scene (Restore Factory, menu, drag and drop).
struct BranchInfo { Branch branch; const char *key; const char *folder; const char *label; const char *item; };
const QList<BranchInfo> &branches();
// La cartella di un ramo come la legge la libreria (refreshRepositories): la
// scelta con Change Folder, altrimenti <radice>/<cartella>. Vuota per None.
QString branchDir(Branch branch);
// Dimentica le cartelle scelte con Change Folder (le chiavi di tutti i rami,
// piu' la vecchia "pathMotions" dei record): da qui ogni ramo torna a
// <radice>/<cartella>.
void forgetBranchDirs();
// La cartella vive dentro iCloud Drive o un altro servizio di
// sincronizzazione (solo macOS; altrove false).
bool isCloudSynced(const QString &path);
// Data la cartella che l'utente ha indicato in un pannello, la RADICE DELLA
// LIBRERIA: quella cartella stessa se e' gia' una radice, la sua "presets" se
// quella lo e', altrimenti una "presets" da creare. Unico punto che decide se
// appendere: senza, indicare una libreria esistente la duplicava dentro se'
// stessa (presets/presets). Vedi il corpo per il perche' il nome della
// sottocartella va letto DA DISCO.
QString resolveRoot(const QString &pickedDir);
// Copia le risorse di fabbrica `resourcePath` in `diskPath`, ricorsivamente.
// Un file cancellato dall'utente (segnaposto ".deleted") non torna, salvo
// forceRestore; con forceRestore un file modificato si sovrascrive solo se
// l'utente lo conferma (dialogo figlio di dialogParent; overwriteState ricorda
// "Yes/No to All" fra una chiamata e l'altra).
void syncResources(const QString &resourcePath, const QString &diskPath,
                   bool forceRestore = false, int *overwriteState = nullptr,
                   QWidget *dialogParent = nullptr);

}
