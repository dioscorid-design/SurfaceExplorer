#include "glsltranslator.h"
#include <QRegularExpression>

QString GlslTranslator::translateEquation(const QString& mathInput)
{
    QString result = mathInput;

    if (result.trimmed().isEmpty()) {
        return "0.0";
    }

    // Regex compilate una volta sola: questa funzione gira per ogni equazione a ogni Run
    static const QRegularExpression rePi("\\bpi\\b", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reTau("\\btau\\b", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reEuler("\\be\\b");
    static const QRegularExpression reLn("\\bln\\b");
    static const QRegularExpression reCot("\\bcot\\b");
    static const QRegularExpression reSec("\\bsec\\b");
    static const QRegularExpression reCsc("\\bcsc\\b");
    static const QRegularExpression reLog10("\\blog10\\b");

    // 1. SOSTITUZIONE COSTANTI
    result.replace(rePi, "3.14159265359");
    result.replace(reTau, "6.28318530718");
    result.replace(reEuler, "2.71828182846");

    // 2. CORREZIONE SINONIMI FUNZIONI
    result.replace(reLn, "log");
    result.replace(reCot, "1.0/tan");
    result.replace(reSec, "1.0/cos");
    result.replace(reCsc, "1.0/sin");
    result.replace(reLog10, "(1.0/2.302585)*log");

    // 3. CONVERSIONE POTENZE E MODULO ( x^y -> pow(x, y) | x%y -> mod(x, y) )
    result = convertPowersToPowFunction(result);
    result = convertModuloToModFunction(result); // <--- NUOVA CHIAMATA

    // 3.5 POW SICURA: pow(x, y) in GLSL è undefined per x < 0 e su Android (Mali/
    // Adreno) dà NaN, facendo "esplodere" mesh come Fresnel (usa pow(cos(u), 4) con
    // base negativa). Riscriviamo ogni chiamata pow( in safePow( (definita negli
    // shader), che calcola su |base| e ripristina il segno per esponenti dispari.
    // \b evita di toccare identificatori che finiscono in "pow" (es. safePow stessa).
    static const QRegularExpression rePowCall("\\bpow\\s*\\(");
    result.replace(rePowCall, "safePow(");

    // 4. SICUREZZA FLOAT ( 2 -> 2.0 )
    result = fixIntegersToFloats(result);

    return result;
}

QString GlslTranslator::fixIntegersToFloats(QString expr)
{
    static const QRegularExpression re("(?<![\\w\\.])(\\d+)(?![\\w\\.])");
    return expr.replace(re, "\\1.0");
}

QString GlslTranslator::convertPowersToPowFunction(const QString& expr) {
    QString result = expr;
    int powerIdx;

    // Scorre da destra a sinistra per gestire potenze nidificate
    while ((powerIdx = result.lastIndexOf('^')) != -1) {

        // --- 1. TROVA L'OPERANDO SINISTRO ---
        int leftStart = powerIdx - 1;
        int parens = 0;
        while (leftStart >= 0) {
            QChar c = result[leftStart];
            if (c == ')') {
                parens++;
            } else if (c == '(') {
                parens--;
                if (parens < 0) { leftStart++; break; }
            } else {
                // Se non siamo dentro una parentesi
                if (parens == 0) {
                    bool isVarChar = c.isLetterOrNumber() || c == '_' || c == '.';
                    // Se troviamo un operatore matematico (+, -, *, /) ci fermiamo
                    if (!isVarChar) { leftStart++; break; }
                }
            }
            leftStart--;
        }
        if (leftStart < 0) leftStart = 0;

        // --- 2. TROVA L'OPERANDO DESTRO ---
        int rightEnd = powerIdx + 1;
        parens = 0;
        while (rightEnd < result.length()) {
            QChar c = result[rightEnd];
            if (c == '(') {
                parens++;
            } else if (c == ')') {
                parens--;
                if (parens < 0) { rightEnd--; break; }
            } else {
                if (parens == 0) {
                    bool isVarChar = c.isLetterOrNumber() || c == '_' || c == '.';
                    // Consente il segno +/- come primo carattere dell'esponente
                    if (rightEnd == powerIdx + 1 && (c == '+' || c == '-')) {
                        isVarChar = true;
                    }
                    if (!isVarChar) { rightEnd--; break; }
                }
            }
            rightEnd++;
        }
        if (rightEnd >= result.length()) rightEnd = result.length() - 1;

        // Estrazione esatta
        QString leftOp = result.mid(leftStart, powerIdx - leftStart).trimmed();
        QString rightOp = result.mid(powerIdx + 1, rightEnd - powerIdx).trimmed();

        // FIX: Usa la funzione nativa di OpenGL "pow"
        bool isNumeric;
        double expValue = rightOp.toDouble(&isNumeric);

        // Comportamento di default (usato per decimali, variabili e numeri interi PARI)
        QString replacement = QString("pow(abs(%1), %2)").arg(leftOp, rightOp);

        if (isNumeric && std::fmod(expValue, 1.0) == 0.0) {
            int intExp = static_cast<int>(expValue);

            if (intExp == 2) {
                replacement = QString("((%1) * (%1))").arg(leftOp);
            } else if (intExp == 3) {
                replacement = QString("((%1) * (%1) * (%1))").arg(leftOp);
            } else if (intExp % 2 != 0) {
                // Dispari (gestione conservazione del segno)
                replacement = QString("(sign(%1) * pow(abs(%1), %2))").arg(leftOp, rightOp);
            }
            // Non serve un "else" per i Pari, perché il default fa già la cosa giusta!
        }

        result.replace(leftStart, rightEnd - leftStart + 1, replacement);
    }

    return result;
}

QString GlslTranslator::convertModuloToModFunction(QString expr)
{
    int modIdx = expr.indexOf('%');
    while (modIdx != -1) {

        // --- TROVA L'OPERANDO SINISTRO (scorre all'indietro) ---
        int leftStart = modIdx - 1;
        int parens = 0;
        while (leftStart >= 0) {
            QChar c = expr[leftStart];
            if (c == ')') parens++;
            else if (c == '(') parens--;

            if (parens < 0) {
                leftStart++;
                break;
            }

            bool isVarChar = c.isLetterOrNumber() || c == '_' || c == '.';
            if (parens == 0 && !isVarChar) {
                leftStart++;
                break;
            }
            leftStart--;
        }
        if (leftStart < 0) leftStart = 0;

        // --- TROVA L'OPERANDO DESTRO (scorre in avanti) ---
        int rightEnd = modIdx + 1;
        parens = 0;
        while (rightEnd < expr.length()) {
            QChar c = expr[rightEnd];
            if (c == '(') parens++;
            else if (c == ')') parens--;

            if (parens < 0) {
                rightEnd--;
                break;
            }

            bool isVarChar = c.isLetterOrNumber() || c == '_' || c == '.';

            // Consenti il segno (+ o -) se è il primo carattere dopo il %
            if (rightEnd == modIdx + 1 && (c == '+' || c == '-')) {
                isVarChar = true;
            }

            if (parens == 0 && !isVarChar) {
                rightEnd--;
                break;
            }
            rightEnd++;
        }
        if (rightEnd >= expr.length()) rightEnd = expr.length() - 1;

        // --- ESTRAZIONE E SOSTITUZIONE ---
        QString leftOp = expr.mid(leftStart, modIdx - leftStart).trimmed();
        QString rightOp = expr.mid(modIdx + 1, rightEnd - modIdx).trimmed();

        // Traduce in mod(a, b) che accetta nativamente float in GLSL
        QString replacement = QString("mod(%1, %2)").arg(leftOp, rightOp);

        expr.replace(leftStart, rightEnd - leftStart + 1, replacement);

        // Cerca il prossimo operatore %
        modIdx = expr.indexOf('%');
    }

    return expr;
}
