#pragma once

// Métricas com intervalo de confiança (equivale a fct_metricas.R e fct_resultados.R).

#include "Database.h"

#include <QColor>
#include <QString>
#include <QStringList>
#include <QVector>

namespace odonto {

// Proporção com intervalo de confiança de Wilson (95%)
Proportion wilson(int x, int n, double z = 1.96);

// "83% (IC 95% 70–91%; 45/54)" ou "—"; curto: "83%"
QString formatProportion(const Proportion& p, bool brief = false);
QString percent(double value);                    // "83%"
QString decimal(double value, int digits = 2);    // vírgula decimal

// Modelos da validação, na ordem e com as cores das figuras do projeto
const QStringList& modelOrder();
QColor modelColor(const QString& model);

struct OperatingMetrics {
    QString model;
    double threshold{0};
    Proportion sensitivity, precision;
    double dice{qQNaN()}, diceLower{qQNaN()}, diceUpper{qQNaN()};  // IC por bootstrap de pacientes
    double fpPerImage{0};
};

// Curvas FROC (maior sensibilidade para cada nº de FP/imagem) e métricas no limiar de operação
QVector<QPair<QString, QVector<QPointF>>> frocCurves(const QVector<FrocRow>& rows);
QVector<OperatingMetrics> operatingMetrics(const QVector<PatientRow>& rows, int bootstrap = 2000, unsigned seed = 1);

}  // namespace odonto
