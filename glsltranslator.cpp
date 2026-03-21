#include "glsltranslator.h"
#include <QRegularExpression>

QString GlslTranslator::translateEquation(const QString& mathInput)
{
    QString result = mathInput;

    if (result.trimmed().isEmpty()) {
        return "0.0";
    }

    // 1. SOSTITUZIONE COSTANTI
    result.replace(QRegularExpression("\\bpi\\b", QRegularExpression::CaseInsensitiveOption), "3.14159265359");
    result.replace(QRegularExpression("\\be\\b"), "2.71828182846");
    result.replace(QRegularExpression("\\bt\\b"), "u_time");

    // 2. CORREZIONE SINONIMI FUNZIONI
    result.replace(QRegularExpression("\\bln\\b"), "log");
    result.replace(QRegularExpression("\\bcot\\b"), "1.0/tan");
    result.replace(QRegularExpression("\\bsec\\b"), "1.0/cos");
    result.replace(QRegularExpression("\\bcsc\\b"), "1.0/sin");
    result.replace(QRegularExpression("\\blog10\\b"), "(1.0/2.302585)*log");

    // 3. CONVERSIONE POTENZE E MODULO ( x^y -> pow(x, y) | x%y -> mod(x, y) )
    result = convertPowersToPowFunction(result);
    result = convertModuloToModFunction(result); // <--- NUOVA CHIAMATA

    // 4. SICUREZZA FLOAT ( 2 -> 2.0 )
    result = fixIntegersToFloats(result);

    return result;
}

QString GlslTranslator::fixIntegersToFloats(QString expr)
{
    QRegularExpression re("(?<![\\w\\.])(\\d+)(?![\\w\\.])");
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
