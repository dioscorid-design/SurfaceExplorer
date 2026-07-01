#ifndef INPUTVALIDATOR_H
#define INPUTVALIDATOR_H

#include <QString>
#include <QWidget>
#include <QLineEdit>
#include <QVector>
#include <functional>
#include <QMessageBox>

class InputValidator
{
public:
    struct LimitField {
        QLineEdit* edit;
        bool active;
    };

    struct NamedField {
        QString name;
        QString text;
    };

    using ParseFn = std::function<float(const QString&, bool*)>;

    // Controlli per la modalità Ray Marching (Implicita)
    static bool validateImplicitEquation(QWidget* parent, const QString& rawEq);
    static bool validateImplicitScriptContext(QWidget* parent, const QString& texCode);

    // Controlli per la modalità Parametrica
    static bool validateParametricVariables(QWidget* parent, const QString& mainEqs, const QString& allEqsToTest, bool hasConstraint);
    static bool validateCompositionFields(QWidget* parent, const QString& mainEqs, const QString& cU, const QString& cV, const QString& cW, bool geoHasText, const QString& composedTest);
    static bool validateLimits(QWidget* parent, float uMin, float uMax, bool uActive, float vMin, float vMax, bool vActive, float wMin, float wMax, bool wActive);
    static bool validateWUsage(QWidget* parent, const QString& allEqs, bool hasExplicit);
    static void resetGeodesicWarning();
    static bool validateGeodesicConformalFactor(QWidget* parent, const QString& lineX, const QString& lineY, const QString& lineZ, const QString& lineP, const QString& lambdaStr, bool showWarning);
    static void showGeodesicSingularityError(QWidget* parent);
    static void showEquationSyntaxError(QWidget* parent);
    static void showMathematicalCollapseError(QWidget* parent);
    static void showAnimatedGeodesicSingularityError(QWidget* parent);
    static void showInvalidConformalConstantError(QWidget* parent);

    // Gestione avvisi file e shader
    static void showImageNotFoundError(QWidget* parent, const QString& imgPath);
    static void showShaderCompilationError(QWidget* parent, const QString& title, const QString& errorLog);
    static void showInvalidConstantError(QWidget* parent, const QString& name, const QString& text);
    static void showInvalidStepsError(QWidget* parent, const QString& text);
    static void showInvalidLimitError(QWidget* parent, const QString& name, const QString& text);

    // Validazione specifica per il dock script (Parametrico)
    static bool validateParametricScriptContext(QWidget* parent, const QString& texCode);
    static bool validateScriptModeContext(QWidget* parent, int currentScriptMode, const QString& currentText);
    static bool validateParametricScriptReturn(QWidget* parent, const QString& cleanCode);
    static bool validateImplicitScriptReturn(QWidget* parent, const QString& cleanCode);
    static void showMetricMissingConditionsInfo(QWidget* parent);
    static void showMetricAmbiguousConstantWarning(QWidget* parent, const QStringList& names);
    static bool validateParentheses(QWidget* parent, const QString& cleanCode);

    // Validatore generico di sintassi.
    // outWarned (opzionale): posto a true se e' stato mostrato un WARNING non
    // bloccante (es. operatori consecutivi). Il chiamante puo' usarlo per NON
    // mostrare un secondo popup (es. errore di compilazione) sullo stesso errore.
    static bool validateExpressionSyntax(QWidget* parent, const QString& expr, const QString& fieldName, bool* outWarned = nullptr);
    static bool validateAndParseLimits(QWidget* parent, const QVector<LimitField>& fields, ParseFn parse, QVector<float>& outValues);
    static bool validateFieldList(QWidget* parent, const QVector<NamedField>& fields, bool* outWarned = nullptr);
    static bool validateConstants(QWidget* parent, const QVector<NamedField>& fields, ParseFn parse);
    static bool validateIdentifiers(QWidget* parent, const QString& expr, const QString& fieldName);
    static void showNegativeConstantError(QWidget* parent, const QString& name);


private:
    static void notify(QWidget* parent, QMessageBox::Icon icon,
                       const QString& title, const QString& text);
    static bool s_boxActive;
};

#endif // INPUTVALIDATOR_H
