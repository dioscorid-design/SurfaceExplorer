#ifndef EXPRESSIONPARSER_H
#define EXPRESSIONPARSER_H

#include <QString>
#include "exprtk.hpp"

class ExpressionParser
{
public:
    ExpressionParser() : mA(1.0), mB(1.0), mC(1.0), mD(1.0), mE(1.0), mF(1.0), mS(0.0), mT(0.00001) {}

    static float evaluateSimple(const QString& mathStr) {
        if (mathStr.trimmed().isEmpty()) return 0.0f;

        // Tabella simboli temporanea con costanti (pi, epsilon, inf)
        exprtk::symbol_table<double> symbol_table;
        symbol_table.add_constants();

        exprtk::expression<double> expression;
        expression.register_symbol_table(symbol_table);

        exprtk::parser<double> parser;

        // Se la compilazione riesce, ritorna il valore calcolato
        if (parser.compile(mathStr.toStdString(), expression)) {
            return static_cast<float>(expression.value());
        }

        // Fallback: se non è un'equazione, prova la conversione standard (es. "0.5")
        return mathStr.toFloat();
    }

    // Configura le variabili dinamiche (coordinate)
    // Vengono passate per riferimento per garantire velocità estrema nel loop di rendering
    template <typename T>
    void setupVariables(T& u, T& v, T& w, T& p) {
        symbol_table.add_variable("u", u);
        symbol_table.add_variable("v", v);
        symbol_table.add_variable("w", w);
        symbol_table.add_variable("p", p);
        symbol_table.add_variable("t", mT);

        // --- QUI AGGIUNGIAMO LE COSTANTI MATEMATICHE (pi, e, ecc.) ---
        symbol_table.add_constants();
        // -------------------------------------------------------------
    }

    // Configura i parametri costanti dell'equazione (A, B, C, D, E, D, s)
    template <typename T>
    void setupConstants(T A, T B, T C, T D = 0, T E = 0, T F = 0, T S = 0) {
        // Aggiorniamo i valori interni
        mA = static_cast<double>(A); mB = static_cast<double>(B); mC = static_cast<double>(C);
        mD = static_cast<double>(D); mE = static_cast<double>(E); mF = static_cast<double>(F);
        mS = static_cast<double>(S);

        // Se non sono già nella tabella, le aggiungiamo collegandole alle variabili interne
        if (!symbol_table.symbol_exists("A")) {
            symbol_table.add_variable("A", mA); symbol_table.add_variable("B", mB); symbol_table.add_variable("C", mC);
            symbol_table.add_variable("D", mD); symbol_table.add_variable("E", mE); symbol_table.add_variable("F", mF);
            symbol_table.add_variable("s", mS);
        }
    }

    // Compila la stringa dell'equazione
    bool compile(const QString& exprStr) {
        expression.register_symbol_table(symbol_table);
        return parser.compile(exprStr.toStdString(), expression);
    }

    // Calcola il valore (da chiamare dentro il ciclo for)
    double value() {
        return expression.value();
    }

private:
    exprtk::symbol_table<double> symbol_table;
    exprtk::expression<double> expression;
    exprtk::parser<double> parser;

    // Memoria interna per mantenere vivi i valori di A, B, C
    double mA;
    double mB;
    double mC;
    double mD;
    double mE;
    double mF;
    double mS;
    double mT;
};

#endif // EXPRESSIONPARSER_H
