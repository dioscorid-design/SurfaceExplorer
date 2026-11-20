// pathreshape.h - La traiettoria di un path della camera modellata col mouse.
//
// A path in corsa il mouse ruota, sposta e scala la TRAIETTORIA (non la
// camera), e il risultato si riscrive nei CAMPI del path: restano l'unica
// verita', quella che il tick, il REC e il Save leggono. Le espressioni si
// ricompongono sempre dagli ORIGINALI con la trasformazione accumulata, mai dal
// testo gia' trasformato: niente annidamenti a ogni gesto, niente errori di
// arrotondamento che si sommano, e il Reset riporta i testi di partenza.
//
// La trasformazione e' affine nelle coordinate delle equazioni:
//     campi = scala * rotazione * originali + spostamento
// Il path 3D ne usa X, Y, Z (il rollio R resta com'e'), il path 4D X, Y, Z, W
// (alpha, beta e gamma restano: sono gli angoli della camera, non la curva).
#pragma once

#include <QStringList>
#include <QVector4D>
#include <array>

class PathReshape
{
public:
    // dims: le coordinate della traiettoria, 3 (path 3D) o 4 (path 4D).
    explicit PathReshape(int dims);

    // C'e' una modifica in corso ED e' ancora quella nei campi. Campi diversi
    // da quelli scritti qui (digitati, caricati da un record) vogliono dire che
    // la modifica non c'e' piu': chi lo vede chiama forget().
    bool active() const { return m_active; }
    bool matches(const QStringList &fields) const { return m_active && fields == m_written; }
    // Il primo gesto: i campi di ora diventano gli originali.
    void begin(const QStringList &fields);
    void forget();
    const QStringList &originals() const { return m_originals; }

    // I gesti, nelle coordinate delle equazioni, composti dopo i precedenti.
    // Rotazione di `radians` nel piano dei versori ortonormali a, b (a va
    // verso b) attorno all'origine delle equazioni.
    void rotate(const QVector4D &a, const QVector4D &b, double radians);
    void translate(const QVector4D &d);
    void scale(double factor);

    // I testi dei campi: gli originali trasformati. Annota i testi come scritti.
    QStringList compose();

    // La composizione, senza stato (per i test): riga i = somma sui j di
    // linear[i][j] * (originale j) + offset[i]. Un originale numerico (vuoto
    // compreso, che vale 0) entra nella costante. Coefficienti a 5 decimali.
    // Una riga che non cambia (solo il proprio originale, coefficiente 1)
    // resta il testo originale, identico.
    using Matrix = std::array<std::array<double, 4>, 4>;
    static QStringList composeTexts(const QStringList &originals, const Matrix &linear,
                                    const std::array<double, 4> &offset, int dims);

private:
    void orthonormalize();

    int m_dims;
    bool m_active = false;
    QStringList m_originals;
    QStringList m_written;
    Matrix m_rotation{};        // ortonormale: la scala sta a parte
    double m_scale = 1.0;
    std::array<double, 4> m_offset{};
};
