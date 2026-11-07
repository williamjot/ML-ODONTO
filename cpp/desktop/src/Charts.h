#pragma once

// Gráficos no estilo das figuras do projeto (Nature: moldura, marcas para dentro, anotações em cinza),
// desenhados com QPainter. Equivale a fct_graficos.R e fct_graficos_resultados.R.

#include <QColor>
#include <QDate>
#include <QMap>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <optional>

namespace odonto {

class ChartWidget : public QWidget {
    Q_OBJECT
public:
    explicit ChartWidget(QWidget* parent = nullptr);
    // Salva em PNG em alta resolução (3x), fundo branco
    bool savePng(const QString& path, QSize size = {}) const;
    void setCaption(const QString& caption) { caption_ = caption; update(); }

protected:
    void paintEvent(QPaintEvent*) override;
    virtual void paintChart(QPainter& painter, const QRectF& area) const = 0;
    void paintEmpty(QPainter& painter, const QRectF& area, const QString& text) const;
    QString caption_;
};

// Escala linear de um eixo com marcas "bonitas"
struct Scale {
    double min{0};
    double max{1};
    [[nodiscard]] QVector<double> ticks(int target = 5) const;
};

// Área de plotagem com moldura e eixos
struct Axes {
    QRectF frame;
    Scale x;
    Scale y;
    [[nodiscard]] QPointF map(double vx, double vy) const;
    [[nodiscard]] double mapX(double v) const;
    [[nodiscard]] double mapY(double v) const;
};

void drawFrame(QPainter& painter, const Axes& axes, const QString& xLabel, const QString& yLabel,
               bool xTicks = true, bool percentY = false, const QStringList& xCategoryLabels = {});
void drawLegend(QPainter& painter, QPointF topLeft, const QStringList& names, const QVector<QColor>& colors,
                bool squares = true);

// Revisões concluídas por dia (barras) e acumulado (linha)
class ProgressChart : public ChartWidget {
public:
    using ChartWidget::ChartWidget;
    void setData(const QVector<QPair<QDate, int>>& data) { data_ = data; update(); }
protected:
    void paintChart(QPainter& painter, const QRectF& area) const override;
private:
    QVector<QPair<QDate, int>> data_;
};

// Quantas imagens têm 0, 1, 2... detecções
class CountChart : public ChartWidget {
public:
    using ChartWidget::ChartWidget;
    void setData(const QVector<int>& valuesPerImage) { values_ = valuesPerImage; update(); }
protected:
    void paintChart(QPainter& painter, const QRectF& area) const override;
private:
    QVector<int> values_;
};

struct Curve {
    QString name;
    QColor color;
    QVector<QPointF> points;   // (FP/imagem, sensibilidade)
    std::optional<QPointF> operating;
};

// Curvas FROC em degraus, com o ponto do limiar de operação
class FrocChart : public ChartWidget {
public:
    using ChartWidget::ChartWidget;
    void setCurves(const QVector<Curve>& curves, int lesions) { curves_ = curves; lesions_ = lesions; update(); }
protected:
    void paintChart(QPainter& painter, const QRectF& area) const override;
private:
    QVector<Curve> curves_;
    int lesions_{0};
};

struct Bar {
    double value{qQNaN()};
    double lower{qQNaN()};
    double upper{qQNaN()};
    QString label;   // texto dentro da barra (ex.: "12/15")
};

// Barras agrupadas (categoria x série) com intervalo de confiança; valores em proporção (0–1)
class GroupedBarChart : public ChartWidget {
public:
    using ChartWidget::ChartWidget;
    void setData(const QStringList& categories, const QStringList& series, const QVector<QColor>& colors,
                 const QVector<QVector<Bar>>& bars /* [série][categoria] */, const QString& yLabel,
                 bool legendTop = false) {
        categories_ = categories; series_ = series; colors_ = colors; bars_ = bars; yLabel_ = yLabel;
        legendTop_ = legendTop; update();
    }
    void setEmptyText(const QString& text) { empty_ = text; }
protected:
    void paintChart(QPainter& painter, const QRectF& area) const override;
private:
    QStringList categories_, series_;
    QVector<QColor> colors_;
    QVector<QVector<Bar>> bars_;
    QString yLabel_, empty_{"Sem dados"};
    bool legendTop_{false};
};

struct Lesion {
    int id{0};
    bool upper{false};       // maxila
    double diameter{0};
    QMap<QString, bool> found;
};

// Mapa lesão x modelo (encontrada ou não), lesões por arcada e diâmetro
class LesionMapChart : public ChartWidget {
public:
    using ChartWidget::ChartWidget;
    void setData(const QVector<Lesion>& lesions, const QStringList& models, const QVector<QColor>& colors) {
        lesions_ = lesions; models_ = models; colors_ = colors; update();
    }
protected:
    void paintChart(QPainter& painter, const QRectF& area) const override;
private:
    QVector<Lesion> lesions_;
    QStringList models_;
    QVector<QColor> colors_;
};

// Diagrama do fluxo de trabalho com as contagens atuais (substitui o Graphviz)
class FlowDiagram : public ChartWidget {
public:
    using ChartWidget::ChartWidget;
    struct Values {
        QString model;
        int images{0}, detections{0}, withDetection{0}, completed{0}, sampleCompleted{0}, sample{0}, added{0};
        QString samplePrecision;
    };
    void setValues(const Values& values) { values_ = values; update(); }
protected:
    void paintChart(QPainter& painter, const QRectF& area) const override;
private:
    Values values_;
};

}  // namespace odonto
