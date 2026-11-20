// pathreshape.cpp - La traiettoria di un path modellata col mouse. Vedi pathreshape.h.
#include "pathreshape.h"

#include <cmath>

namespace {

// 5 decimali: sotto il centomillesimo un coefficiente e' zero, e un
// coefficiente che si scrive "1" e' 1 (niente "1*(...)").
QString formatNumber(double v)
{
    QString s = QString::number(v, 'f', 5);
    if (s.contains(QLatin1Char('.'))) {
        while (s.endsWith(QLatin1Char('0'))) s.chop(1);
        if (s.endsWith(QLatin1Char('.'))) s.chop(1);
    }
    if (s == QLatin1String("-0")) s = QStringLiteral("0");
    return s;
}

// Il testo di un campo come lo compila il path (vedi compilePath3DFromFields):
// vuoto = 0, virgola decimale tollerata.
QString cleaned(const QString &text)
{
    QString t = text.trimmed();
    t.replace(QLatin1Char(','), QLatin1Char('.'));
    return t;
}

} // namespace

PathReshape::PathReshape(int dims) : m_dims(dims)
{
    forget();
}

void PathReshape::forget()
{
    m_active = false;
    m_originals.clear();
    m_written.clear();
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) m_rotation[i][j] = (i == j) ? 1.0 : 0.0;
    m_scale = 1.0;
    m_offset = {};
}

void PathReshape::begin(const QStringList &fields)
{
    forget();
    m_active = true;
    m_originals = fields;
    m_written = fields;
}

void PathReshape::rotate(const QVector4D &a, const QVector4D &b, double radians)
{
    // G = I + (cos - 1)(a a' + b b') + sin (b a' - a b'): G a = cos a + sin b,
    // G b = cos b - sin a, il resto fermo. Composta a sinistra: R = G R, e lo
    // spostamento gira con lei (il perno e' l'origine delle equazioni).
    const double c = std::cos(radians) - 1.0, s = std::sin(radians);
    Matrix g{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            g[i][j] = (i == j ? 1.0 : 0.0) + c * (double(a[i]) * a[j] + double(b[i]) * b[j])
                      + s * (double(b[i]) * a[j] - double(a[i]) * b[j]);
    Matrix r{};
    std::array<double, 4> t{};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k) r[i][j] += g[i][k] * m_rotation[k][j];
        for (int k = 0; k < 4; ++k) t[i] += g[i][k] * m_offset[k];
    }
    m_rotation = r;
    m_offset = t;
    orthonormalize();
}

void PathReshape::translate(const QVector4D &d)
{
    for (int i = 0; i < 4; ++i) m_offset[i] += d[i];
}

void PathReshape::scale(double factor)
{
    m_scale *= factor;
    for (double &t : m_offset) t *= factor;
}

void PathReshape::orthonormalize()
{
    // Migliaia di piccole rotazioni in fila allontanano la matrice dall'essere
    // una rotazione: Gram-Schmidt sulle colonne la riporta li'.
    for (int j = 0; j < 4; ++j) {
        for (int k = 0; k < j; ++k) {
            double dot = 0.0;
            for (int i = 0; i < 4; ++i) dot += m_rotation[i][j] * m_rotation[i][k];
            for (int i = 0; i < 4; ++i) m_rotation[i][j] -= dot * m_rotation[i][k];
        }
        double len = 0.0;
        for (int i = 0; i < 4; ++i) len += m_rotation[i][j] * m_rotation[i][j];
        len = std::sqrt(len);
        if (len > 1e-12)
            for (int i = 0; i < 4; ++i) m_rotation[i][j] /= len;
    }
}

QStringList PathReshape::compose()
{
    Matrix linear{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) linear[i][j] = m_scale * m_rotation[i][j];
    m_written = composeTexts(m_originals, linear, m_offset, m_dims);
    return m_written;
}

QStringList PathReshape::composeTexts(const QStringList &originals, const Matrix &linear,
                                      const std::array<double, 4> &offset, int dims)
{
    QStringList out;
    for (int i = 0; i < dims; ++i) {
        // Riga invariata (la propria coordinata a coefficiente 1, nient'altro):
        // il testo originale, com'era, senza parentesi aggiunte.
        bool unchanged = formatNumber(offset[i]) == QLatin1String("0");
        for (int j = 0; j < dims && unchanged; ++j)
            unchanged = formatNumber(linear[i][j] - (i == j ? 1.0 : 0.0)) == QLatin1String("0");
        if (unchanged) {
            out << originals.value(i);
            continue;
        }
        QString expr;
        double constant = offset[i];
        for (int j = 0; j < dims; ++j) {
            const double c = linear[i][j];
            // Il testo si copia com'e' (la virgola la tratta la compilazione);
            // la forma pulita serve solo a riconoscere un numero.
            const QString orig = originals.value(j).trimmed();
            bool numeric = orig.isEmpty();
            const double value = numeric ? 0.0 : cleaned(orig).toDouble(&numeric);
            if (numeric) {
                constant += c * value;
                continue;
            }
            const QString coef = formatNumber(std::abs(c));
            if (coef == QLatin1String("0")) continue;
            const QString term = (coef == QLatin1String("1") ? QString() : coef + QLatin1Char('*'))
                                 + QLatin1Char('(') + orig + QLatin1Char(')');
            if (expr.isEmpty()) expr = (c < 0.0 ? QStringLiteral("-") : QString()) + term;
            else expr += (c < 0.0 ? QStringLiteral(" - ") : QStringLiteral(" + ")) + term;
        }
        const QString k = formatNumber(std::abs(constant));
        if (k != QLatin1String("0")) {
            if (expr.isEmpty()) expr = (constant < 0.0 ? QStringLiteral("-") : QString()) + k;
            else expr += (constant < 0.0 ? QStringLiteral(" - ") : QStringLiteral(" + ")) + k;
        }
        // Tutto zero: un campo vuoto resta vuoto, uno scritto diventa "0".
        if (expr.isEmpty()) expr = cleaned(originals.value(i)).isEmpty() ? QString() : QStringLiteral("0");
        out << expr;
    }
    return out;
}
