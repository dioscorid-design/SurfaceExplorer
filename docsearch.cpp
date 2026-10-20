// docsearch.cpp - Ricerca nella guida. Vedi docsearch.h.
#include "docsearch.h"

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextDocumentFragment>

#include <algorithm>

QVector<DocSearch::Hit> DocSearch::search(const QString &needle)
{
    QVector<Hit> hits;
    const QString term = needle.trimmed();
    if (term.isEmpty()) return hits;

    // Le pagine si ENUMERANO dal .qrc invece di elencarle a mano: il manuale
    // cresce (17 file oggi) e una lista scritta qui nascerebbe incompleta al
    // primo capitolo aggiunto -- lo stesso motivo per cui il tasto Close/Back
    // usa una regola posizionale e non un elenco di pagine.
    QDirIterator it(":/docs", {"*.html"}, QDir::Files);
    while (it.hasNext()) {
        const QString path = it.next();

        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        const QString html = QString::fromUtf8(f.readAll());

        // Si cerca nel TESTO, non nell'HTML: cercando nel sorgente un termine
        // come "color" comparirebbe in ogni attributo di stile, e i risultati
        // sarebbero quasi tutti falsi. toPlainText() risolve anche le entita'
        // (&mdash;, &amp;) che altrimenti spezzerebbero le parole.
        const QString text = QTextDocumentFragment::fromHtml(html).toPlainText();

        int count = 0;
        int first = -1;
        int from = 0;
        while (true) {
            const int at = text.indexOf(term, from, Qt::CaseInsensitive);
            if (at < 0) break;
            if (first < 0) first = at;
            ++count;
            from = at + term.length();
        }
        if (!count) continue;

        Hit h;
        h.file  = "qrc" + path;   // QDirIterator da ":/docs/...", il browser vuole "qrc:/docs/..."
        h.count = count;

        // Titolo della pagina: e' l'etichetta che l'utente riconosce. Il <title>
        // porta il prefisso del prodotto ("Surface Explorer - X"), che ripetuto
        // su ogni riga sarebbe rumore: si tiene la parte dopo il separatore.
        QRegularExpression titleRe("<title>(.*?)</title>",
                                   QRegularExpression::DotMatchesEverythingOption
                                   | QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch tm = titleRe.match(html);
        if (tm.hasMatch()) {
            h.title = QTextDocumentFragment::fromHtml(tm.captured(1)).toPlainText().trimmed();
            const int sep = h.title.indexOf(QRegularExpression("\\s+[-\\x{2013}\\x{2014}]\\s+"));
            if (sep > 0) h.title = h.title.mid(sep).trimmed();
            h.title.remove(QRegularExpression("^[-\\x{2013}\\x{2014}]\\s*"));
        }
        if (h.title.isEmpty()) h.title = QFileInfo(path).baseName();

        // Frammento di contesto attorno alla PRIMA occorrenza, cosi' si capisce
        // se la pagina parla davvero di cio' che si cerca prima di aprirla.
        const int ctxStart = qMax(0, first - 60);
        const int ctxEnd   = qMin(text.length(), first + term.length() + 90);
        h.context = text.mid(ctxStart, ctxEnd - ctxStart).simplified();
        if (ctxStart > 0)            h.context.prepend(QString::fromUtf8("\xE2\x80\xA6 "));
        if (ctxEnd < text.length())  h.context.append(QString::fromUtf8(" \xE2\x80\xA6"));

        hits.append(h);
    }

    // Piu' occorrenze = pagina piu' pertinente; a parita', ordine alfabetico
    // per un elenco stabile fra una ricerca e l'altra.
    std::sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) {
        if (a.count != b.count) return a.count > b.count;
        return a.title.localeAwareCompare(b.title) < 0;
    });
    return hits;
}
