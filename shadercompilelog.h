#ifndef SHADERCOMPILELOG_H
#define SHADERCOMPILELOG_H

// ERRORI DI COMPILAZIONE DEGLI SHADER, PER I TEST INTERNI (round-trip, scenari).
//
// Un errore di compilazione non apre sempre un popup: la texture di un record
// che non compila restava rifiutata in silenzio, con il solo avviso sul log
// (Kerr Spin Animated, rotto dal 2026-09-05 al 2026-10-03 senza che nessun test
// se ne accorgesse: il Save era giusto). Qui i test leggono il log e contano.
//
// Due famiglie di messaggi:
//  - SHADER ERROR / "pipeline non costruite" (GLWidget::bakeShader, buildPipeline):
//    uno shader che DISEGNA non compila, a schermo manca qualcosa. Conta sempre.
//  - texture RIFIUTATA alla prova (loadCustomShader, load dei record): il motore
//    tiene lo shader di prima. Conta sempre: la prova compila solo il FRAGMENT,
//    cioe' il codice della texture. (Provava anche il vertex con le equazioni
//    del momento, e a meta' load Oloid -- vincolo in W -- veniva rifiutata per
//    un errore non suo: quell'errore era elencato senza contare. Tolta la prova
//    del vertex, e' sparita anche l'eccezione.)

#include <QList>
#include <QString>
#include <QtGlobal>
#include <cstdio>

namespace ShaderCompileLog {

struct Entry {
    QString text;       // il messaggio, su una riga
    bool counts = true; // false: elencato come atteso (vedi excuse)
    QString excuse;
};

inline QList<Entry> &pending() { static QList<Entry> l; return l; }
inline QtMessageHandler &previousHandler() { static QtMessageHandler h = nullptr; return h; }

// true se il messaggio e' un errore di compilazione; *out lo descrive.
inline bool classify(const QString &msg, Entry *out)
{
    const bool drawn = msg.contains(QLatin1String("SHADER ERROR"))
                    || msg.contains(QLatin1String("pipeline non costruite"));
    const bool rejected = msg.contains(QLatin1String("loadCustomShader: codice non valido"))
                       || msg.contains(QLatin1String("texture del record non compilata"));
    if (!drawn && !rejected) return false;
    out->text = msg.simplified().left(300);
    out->counts = true;
    out->excuse.clear();
    return true;
}

inline void handler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    Entry e;
    if (classify(msg, &e)) pending().append(e);
    // Il log continua come prima (app.log dei test).
    if (previousHandler()) previousHandler()(type, ctx, msg);
    else std::fprintf(stderr, "%s\n", qPrintable(qFormatLogMessage(type, ctx, msg)));
}

inline void install() { previousHandler() = qInstallMessageHandler(handler); }

// Gli errori raccolti dall'ultima chiamata, e azzera.
inline QList<Entry> take()
{
    QList<Entry> l = pending();
    pending().clear();
    return l;
}

} // namespace ShaderCompileLog

#endif // SHADERCOMPILELOG_H
