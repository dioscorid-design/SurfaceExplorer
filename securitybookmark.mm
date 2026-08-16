#include "securitybookmark.h"

#include <QDir>
#include <QSettings>
#include <QCryptographicHash>
#include <QDebug>

// La sandbox esiste solo nel canale App Store. Il DMG (Developer ID) e la build
// di sviluppo girano NON sandboxed sullo stesso binario macOS, quindi non basta
// Q_OS_MACOS per decidere: si controlla a RUNTIME se il processo e' confinato.
// Cosi' un unico binario si comporta correttamente in entrambi i canali.
#ifdef Q_OS_MACOS

#import <Foundation/Foundation.h>
#include <QHash>
#include <QProcessEnvironment>

namespace {

// Processo sandboxed? Calcolato una volta sola.
// Si guarda APP_SANDBOX_CONTAINER_ID, che macOS inietta nell'ambiente dei soli
// processi confinati: e' il metodo documentato e stabile. (sandbox_check()
// sarebbe piu' diretto ma SANDBOX_FILTER_NONE non e' esposto nell'header
// pubblico, e l'intera API sandbox.h e' marcata deprecata.)
// Fuori dalla sandbox tutte le funzioni di questo file diventano no-op e il
// comportamento resta identico a prima (QDir::exists puro): e' cio' che serve
// al canale DMG, che gira NON sandboxed sullo stesso binario.
bool inSandbox()
{
    static const bool sandboxed =
        QProcessEnvironment::systemEnvironment().contains("APP_SANDBOX_CONTAINER_ID");
    return sandboxed;
}

// Una chiave per percorso, derivata dal path: cambiare cartella non invalida il
// bookmark della precedente, se l'utente ci torna.
QString settingsKey(const QString& path)
{
    const QByteArray h = QCryptographicHash::hash(QDir::cleanPath(path).toUtf8(),
                                                  QCryptographicHash::Sha1);
    return QStringLiteral("securityBookmarks/") + QString::fromLatin1(h.toHex());
}

// Percorsi con accesso attivo. macOS conta start/stop: aprire due volte lo
// stesso scope richiederebbe due stop, quindi si tiene traccia di cosa e' gia'
// aperto.
QHash<QString, NSURL*>& activeUrls()
{
    static QHash<QString, NSURL*> urls;
    return urls;
}

} // namespace

namespace SecurityBookmark {

bool save(const QString& path)
{
    if (path.isEmpty()) return false;
    if (!inSandbox()) return true;   // DMG/sviluppo: nulla da autorizzare

    @autoreleasepool {
        NSURL *url = [NSURL fileURLWithPath:QDir::cleanPath(path).toNSString()
                                isDirectory:YES];
        NSError *err = nil;
        NSData *data = [url bookmarkDataWithOptions:NSURLBookmarkCreationWithSecurityScope
                     includingResourceValuesForKeys:nil
                                      relativeToURL:nil
                                              error:&err];
        if (!data) {
            qWarning() << "SecurityBookmark: creazione fallita per" << path << "-"
                       << QString::fromNSString(err.localizedDescription);
            return false;
        }

        QSettings().setValue(settingsKey(path), QByteArray::fromNSData(data));

        // Apre subito lo scope: cosi' lo stato dopo save() e' identico a quello
        // dopo restore(), e il resto del codice non deve distinguere il primo
        // avvio dai successivi.
        restore(path);
        return true;
    }
}

bool restore(const QString& path)
{
    if (path.isEmpty()) return false;
    if (!inSandbox()) return true;

    const QString clean = QDir::cleanPath(path);
    if (activeUrls().contains(clean)) return true;   // gia' aperto

    const QByteArray stored = QSettings().value(settingsKey(clean)).toByteArray();
    if (stored.isEmpty()) {
        // Nessun bookmark per QUESTO percorso: si prova con un ANTENATO.
        // Sotto sandbox il diritto nasce da cio' che l'utente ha indicato nel
        // pannello di sistema, e le sottocartelle create dal codice (la
        // "presets" sotto la cartella scelta, i quattro rami sotto di essa)
        // non hanno un bookmark proprio. Aprendo lo scope del genitore, i
        // figli ereditano l'accesso: e' lo stesso motivo per cui in
        // refreshRepositories basta riaprire la radice.
        QDir up(clean);
        while (up.cdUp()) {
            const QString parent = QDir::cleanPath(up.absolutePath());
            if (parent == QLatin1String("/") || parent.isEmpty()) break;
            if (activeUrls().contains(parent)) return true;
            if (!QSettings().value(settingsKey(parent)).toByteArray().isEmpty())
                return restore(parent);
        }
        return false;
    }

    @autoreleasepool {
        BOOL stale = NO;
        NSError *err = nil;
        NSURL *url = [NSURL URLByResolvingBookmarkData:stored.toNSData()
                                               options:NSURLBookmarkResolutionWithSecurityScope
                                         relativeToURL:nil
                                   bookmarkDataIsStale:&stale
                                                 error:&err];
        if (!url) {
            qWarning() << "SecurityBookmark: risoluzione fallita per" << clean << "-"
                       << QString::fromNSString(err.localizedDescription);
            return false;
        }

        if (![url startAccessingSecurityScopedResource]) {
            qWarning() << "SecurityBookmark: accesso negato a" << clean;
            return false;
        }

        // "Stale": il bookmark ha funzionato ma macOS avvisa che va riscritto
        // (cartella spostata/rinominata, o sistema aggiornato). Va rigenerato
        // ORA che l'accesso e' aperto: al prossimo avvio sarebbe troppo tardi.
        if (stale) {
            NSError *reErr = nil;
            NSData *fresh = [url bookmarkDataWithOptions:NSURLBookmarkCreationWithSecurityScope
                          includingResourceValuesForKeys:nil
                                           relativeToURL:nil
                                                   error:&reErr];
            if (fresh)
                QSettings().setValue(settingsKey(clean), QByteArray::fromNSData(fresh));
        }

        activeUrls().insert(clean, [url retain]);
        return true;
    }
}

bool isAccessible(const QString& path)
{
    if (path.isEmpty()) return false;
    const QString clean = QDir::cleanPath(path);

    // Fuori dalla sandbox: esattamente il vecchio comportamento, niente di piu'.
    if (!inSandbox()) return QDir(clean).exists();

    // Dentro: prima si prova a riaprire l'accesso, perche' questa funzione viene
    // chiamata anche da punti che non sanno nulla di bookmark e deve dire la
    // verita' senza che ognuno debba ricordarsi di chiamare restore().
    if (!activeUrls().contains(clean)) restore(clean);

    return QDir(clean).exists();
}

bool needsAuthorization(const QString& path)
{
    if (path.isEmpty() || !inSandbox()) return false;

    // C'e' un bookmark salvato (quindi la cartella e' stata scelta in passato)
    // ma il percorso non e' raggiungibile: va riautorizzato, non reinstallato.
    const QString clean = QDir::cleanPath(path);
    const bool hasBookmark = !QSettings().value(settingsKey(clean)).toByteArray().isEmpty();
    return hasBookmark && !isAccessible(clean);
}

void releaseAll()
{
    @autoreleasepool {
        for (auto it = activeUrls().begin(); it != activeUrls().end(); ++it) {
            [it.value() stopAccessingSecurityScopedResource];
            [it.value() release];
        }
        activeUrls().clear();
    }
}

} // namespace SecurityBookmark

#else // !Q_OS_MACOS

// iOS, Android, Windows, Linux: nessuna sandbox con Powerbox di mezzo. iOS e
// Android non passano nemmeno da qui (presetsRootPath ricalcola il percorso dal
// sistema); Windows e Linux usano cartelle normali.
namespace SecurityBookmark {

bool save(const QString&) { return true; }
bool restore(const QString&) { return true; }
bool isAccessible(const QString& path) { return !path.isEmpty() && QDir(path).exists(); }
bool needsAuthorization(const QString&) { return false; }
void releaseAll() {}

} // namespace SecurityBookmark

#endif // Q_OS_MACOS
