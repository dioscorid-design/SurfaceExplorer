// texturecode.cpp - Il codice delle texture, senza stato. Vedi texturecode.h.
#include "texturecode.h"

#include "librarymanager.h"
#include "texthelpers.h"

#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>

bool TextureCode::hasLogic(const QString &code)
{
    return code.contains("return") || code.contains("vec3")
        || code.contains("vec4")   || code.contains("mainImage");
}

bool TextureCode::samplesImage(const QString &code)
{
    static const QRegularExpression re(R"(\biChannel[0-3]\b|\btex\b)");
    return stripCodeComments(code).contains(re);
}

QString TextureCode::withImageTagPath(const QString &code, const QString &path)
{
    static const QRegularExpression imgRe(R"(^[ \t]*//IMG:[ \t]*(.*)$)",
                                          QRegularExpression::MultilineOption);
    const QRegularExpressionMatch m = imgRe.match(code);
    if (!m.hasMatch()) return code;
    QString out = code;
    out.replace(m.capturedStart(1), m.capturedLength(1), path);
    return out;
}

QString TextureCode::resolveImagePath(const QString &scriptCode) {
    QRegularExpression imgRe(R"(^\s*//IMG:\s*(.*)$)", QRegularExpression::MultilineOption);
    QRegularExpressionMatch imgMatch = imgRe.match(scriptCode);

    if (!imgMatch.hasMatch()) return ""; // Nessun tag immagine trovato

    QString imgPath = imgMatch.captured(1).trimmed();
    // LEGGIBILE, non solo esistente. Sotto sandbox un file PUO' esistere ed
    // essere illeggibile: e' il caso dei preset che citano una vecchia copia
    // della libreria fuori dalla cartella autorizzata (es. una cartella di
    // lavoro del progetto). Con il solo exists() il percorso veniva accettato,
    // lo Smart Path Resolver NON entrava in funzione e l'immagine finiva nel
    // fallback grigio -- mentre la stessa immagine, citata da un record iOS con
    // percorso inesistente, veniva ritrovata correttamente per nome.
    if (isReadableFile(imgPath)) return imgPath; // Trovata al percorso originale!

    // --- SMART PATH RESOLVER (Ricerca automatica) ---
    QString fileName = QFileInfo(imgPath).fileName();
    QSettings settings;
    QString texDir = settings.value("pathTextures", settings.value("libraryRootPath").toString() + "/textures").toString();
    QDirIterator it(texDir, QStringList() << fileName, QDir::Files, QDirIterator::Subdirectories);

    if (it.hasNext()) return it.next(); // Ritrovata nella nuova cartella!

    return "NOT_FOUND|" + imgPath; // Restituisce un flag per far gestire l'errore a chi l'ha chiamata
}

bool TextureCode::itemMatchesCode(const LibraryItem &texItem, const QString &activeCode,
                     const QString &cleanedActiveCode)
{
    if (texItem.isImage) {
        // Un'immagine e' attiva SOLO se il suo file compare nel tag //IMG:, non
        // in un punto qualsiasi del sorgente: uno script procedurale puo'
        // trascinarsi un //IMG: orfano (o citare un nome file in un commento) e
        // con un contains() sul testo intero l'albero evidenziava l'immagine al
        // posto del procedurale davvero in uso.
        QRegularExpression imgRe(R"(^\s*//IMG:\s*(.*)$)", QRegularExpression::MultilineOption);
        QRegularExpressionMatch m = imgRe.match(activeCode);
        if (!m.hasMatch()) return false;

        // Il tag conta come immagine attiva solo se e' l'unico contenuto: se sotto
        // c'e' del codice GLSL, a disegnare e' quello (il tag e' un residuo).
        //
        // ECCEZIONE, il TRIPLANAR: in Ray Marching un'immagine non e' mai un tag
        // nudo. L'app le antepone sempre lo script che la campiona (~7747), che
        // e' generato dal motore e non scritto dall'utente: senza, l'immagine
        // non si vedrebbe affatto. Trattarlo come "codice sotto il tag" faceva
        // scartare OGNI immagine ray marching, che restava senza focus in
        // libreria -- il tag c'era, l'immagine si vedeva, ma l'albero non la
        // evidenziava mai.
        // Si riconosce dalla FIRMA (le tre proiezioni triplanari su pModel) e
        // non dal testo esatto: cosi' un ritocco di spaziatura o di 'scale' nel
        // generatore non rimette in piedi il difetto.
        QString rest = activeCode;
        rest.remove(imgRe);
        const QString restClean = cleanForComparison(rest);
        const bool isGeneratedTriplanar =
                restClean.contains("texture(tex,pModel.yz") &&
                restClean.contains("texture(tex,pModel.xz") &&
                restClean.contains("texture(tex,pModel.xy") &&
                restClean.contains("textureCol=cX*blend.x");
        if (!restClean.isEmpty() && !isGeneratedTriplanar) return false;

        QString activeImg = QFileInfo(m.captured(1).trimmed()).fileName();
        QString libImg    = QFileInfo(texItem.filePath).fileName();
        return !libImg.isEmpty() && !activeImg.isEmpty() &&
               QString::compare(activeImg, libImg, Qt::CaseInsensitive) == 0;
    }

    // NB: il confronto per NOME (libName) NON sta qui. Deve valere solo quando
    // NESSUNA voce combacia per codice, e una funzione che guarda un item alla
    // volta non puo' saperlo: rispondendo "si" sulla prima voce col nome giusto
    // faceva vincere l'ordine alfabetico sul criterio. Il fallback sul nome vive
    // percio' nel chiamante, che scorre l'albero in due passate.

    // Procedurale: confronto sui codici puliti (TextureCode::cleanForComparison toglie
    // gia' il tag //IMG:, quindi un residuo non impedisce il match).
    QString cleanLibCode = cleanForComparison(texItem.scriptCode);
    return !cleanedActiveCode.isEmpty() && cleanedActiveCode == cleanLibCode;
}

QString TextureCode::cleanForComparison(QString str) {
    QRegularExpression blockRe(R"(//\s*SOUND_BEGIN.*?//\s*SOUND_END\n?)",
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    while (str.contains(blockRe)) str.remove(blockRe);
    str.remove(QRegularExpression(R"(^\s*//(MUSIC|SYNTH):.*$\n?)",            QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption));
    str.remove(QRegularExpression(R"(^\s*//\s*(SOUND_BEGIN|SOUND_END).*$\n?)", QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption));
    str.remove(QRegularExpression(R"(^\s*//IMG:.*$\n?)",                       QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption));
    str.remove(QRegularExpression(R"(//.*$)",                                  QRegularExpression::MultilineOption));
    str.remove(QRegularExpression(R"(/\*.*?\*/)",                              QRegularExpression::DotMatchesEverythingOption));
    str.replace(QRegularExpression("\\s+"), "");
    return str;
}

QString TextureCode::defaultMeshCode()
{
    return QStringLiteral(
        "vec2 g = floor(vec2(u, v) * 8.0);\n"
        "float c = mod(g.x + g.y, 2.0);\n"
        "return mix(u_col1, u_col2, c);");
}
