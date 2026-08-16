#ifndef SECURITYBOOKMARK_H
#define SECURITYBOOKMARK_H

#include <QString>
#include <QtGlobal>

// =============================================================================
// SECURITY-SCOPED BOOKMARK — solo canale MAC APP STORE (sandbox)
//
// IL PROBLEMA, MISURATO (non ipotizzato):
// Sotto sandbox il container REDIRIGE le cartelle utente. Desktop, Downloads,
// Movies, Music e Pictures sono symlink verso quelle reali; DOCUMENTS NO: e'
// una cartella PRIVATA del container, vuota. Quindi il percorso
// "/Users/<x>/Documents/presets", salvato come semplice stringa in QSettings,
// alla riapertura viene risolto DENTRO il container e non trova nulla.
//
// Il valore in QSettings si salva e si rilegge benissimo (verificato:
// isWritable=true, status=NoError, rilettura corretta): a mancare e' il
// DIRITTO di raggiungere la cartella VERA. Solo la scelta dall'apposito
// pannello di sistema (Powerbox) lo concede, e vale per quella sessione.
//
// IL RIMEDIO: al momento della scelta si salva un bookmark
// (NSURL bookmarkDataWithOptions:NSURLBookmarkCreationWithSecurityScope) e a
// ogni avvio lo si risolve con startAccessingSecurityScopedResource. L'utente
// sceglie la cartella UNA VOLTA SOLA; da li' in poi ogni avvio la ritrova.
// Richiede l'entitlement com.apple.security.files.bookmarks.app-scope, aggiunto
// al SOLO macos_appstore.entitlements.
//
// PERCHE' IL BUG NON SI VEDE ALTROVE:
//   - DMG / Developer ID : macos_release.entitlements NON ha app-sandbox, quindi
//                          nessuna redirezione: la stringa basta e avanza.
//   - build di sviluppo  : idem, non e' sandboxed.
//   - iOS / Android      : non usano affatto libraryRootPath; presetsRootPath()
//                          ricalcola il percorso a ogni chiamata dal sistema.
// Fuori da macOS-sandbox queste funzioni sono no-op che rispondono ESATTAMENTE
// come il vecchio QDir::exists(), cosi' DMG/Windows/Linux non cambiano di una
// virgola e i punti di chiamata restano senza #ifdef.
// =============================================================================

namespace SecurityBookmark {

// Salva il bookmark della cartella scelta dall'utente. Da chiamare SUBITO dopo
// il pannello di sistema: e' l'unico momento in cui l'accesso e' concesso.
// false = bookmark non creato (la sessione corrente funziona comunque).
// Fuori dalla sandbox: no-op che ritorna true.
bool save(const QString& path);

// Risolve il bookmark salvato e riapre l'accesso. Da chiamare a ogni avvio
// PRIMA di leggere la libreria. false = nessun bookmark, o non piu' risolvibile
// (cartella cancellata). Fuori dalla sandbox: no-op che ritorna true.
bool restore(const QString& path);

// Sostituisce QDir::exists() dove la differenza fra "non esiste" e "non ho il
// diritto di vederla" cambia cosa mostrare all'utente. Prova a riaprire
// l'accesso e poi verifica l'esistenza.
// Fuori dalla sandbox equivale esattamente a QDir(path).exists().
bool isAccessible(const QString& path);

// true se il path NON e' raggiungibile per mancanza di autorizzazione, cioe'
// c'e' un bookmark salvato che non si risolve piu'. Serve a distinguere il caso
// "libreria da autorizzare di nuovo" da "prima installazione", che vogliono due
// messaggi diversi. Fuori dalla sandbox: sempre false.
bool needsAuthorization(const QString& path);

// Chiude gli accessi aperti da restore(). Non obbligatoria (il processo che
// termina li rilascia), ma tiene pulito il conteggio quando la cartella cambia.
void releaseAll();

} // namespace SecurityBookmark

#endif // SECURITYBOOKMARK_H
