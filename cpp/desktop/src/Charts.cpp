#include "Charts.h"

#include "Metrics.h"
#include "Pane.h"

#include <QImage>
#include <QLocale>
#include <QMap>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <optional>

namespace odonto {
namespace {

const QColor kInk("#000000");
const QColor kGrey(color::grey);
constexpr double kTick = 4.5;

QFont chartFont(double points, bool bold = false) {
    QFont f("Segoe UI");
    f.setPointSizeF(points);
    f.setBold(bold);
    return f;
}

QString number(double v) {
    return QLocale(QLocale::Portuguese, QLocale::Brazil).toString(v, 'g', 6);
}

}  // namespace

ChartWidget::ChartWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(220, 160);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void ChartWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::white);
    painter.setRenderHint(QPainter::Antialiasing);
    QRectF area = QRectF(rect()).adjusted(8, 8, -10, -6);
    if (!caption_.isEmpty()) {
        painter.setFont(chartFont(8));
        painter.setPen(kGrey);
        painter.drawText(area, Qt::AlignRight | Qt::AlignBottom, caption_);
        area.adjust(0, 0, 0, -16);
    }
    paintChart(painter, area);
}

bool ChartWidget::savePng(const QString& path, QSize size) const {
    if (!size.isValid()) {
        size = this->size();
    }
    QImage image(size * 3, QImage::Format_ARGB32);
    image.setDevicePixelRatio(3);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    QRectF area = QRectF(QPointF(0, 0), QSizeF(size)).adjusted(8, 8, -10, -6);
    if (!caption_.isEmpty()) {
        painter.setFont(chartFont(8));
        painter.setPen(kGrey);
        painter.drawText(area, Qt::AlignRight | Qt::AlignBottom, caption_);
        area.adjust(0, 0, 0, -16);
    }
    paintChart(painter, area);
    painter.end();
    return image.save(path, "PNG");
}

void ChartWidget::paintEmpty(QPainter& painter, const QRectF& area, const QString& text) const {
    painter.setFont(chartFont(11));
    painter.setPen(kGrey);
    painter.drawText(area, Qt::AlignCenter, text);
}

QVector<double> Scale::ticks(int target) const {
    const double span = max - min;
    if (span <= 0) {
        return {min};
    }
    const double raw = span / target;
    const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
    double step = magnitude;
    for (double m : {1.0, 2.0, 2.5, 5.0, 10.0}) {
        if (m * magnitude >= raw) {
            step = m * magnitude;
            break;
        }
    }
    QVector<double> result;
    for (double v = std::ceil(min / step - 1e-9) * step; v <= max + 1e-9; v += step) {
        result.push_back(std::abs(v) < 1e-12 ? 0.0 : v);
    }
    return result;
}

double Axes::mapX(double v) const { return frame.left() + (v - x.min) / (x.max - x.min) * frame.width(); }
double Axes::mapY(double v) const { return frame.bottom() - (v - y.min) / (y.max - y.min) * frame.height(); }
QPointF Axes::map(double vx, double vy) const { return {mapX(vx), mapY(vy)}; }

void drawFrame(QPainter& painter, const Axes& axes, const QString& xLabel, const QString& yLabel, bool xTicks,
               bool percentY, const QStringList& xCategoryLabels) {
    painter.setPen(QPen(kInk, 0.9));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(axes.frame);
    painter.setFont(chartFont(8.5));
    const QFontMetricsF fm(painter.font());
    for (double v : axes.y.ticks()) {
        const double y = axes.mapY(v);
        painter.drawLine(QPointF(axes.frame.left(), y), QPointF(axes.frame.left() + kTick, y));
        const QString text = percentY ? percent(v) : number(v);
        painter.drawText(QRectF(axes.frame.left() - 60, y - 8, 55, 16), Qt::AlignRight | Qt::AlignVCenter, text);
    }
    if (xTicks && xCategoryLabels.isEmpty()) {
        for (double v : axes.x.ticks()) {
            const double x = axes.mapX(v);
            painter.drawLine(QPointF(x, axes.frame.bottom()), QPointF(x, axes.frame.bottom() - kTick));
            painter.drawText(QRectF(x - 30, axes.frame.bottom() + 3, 60, 16), Qt::AlignHCenter | Qt::AlignTop, number(v));
        }
    }
    for (int i = 0; i < xCategoryLabels.size(); ++i) {
        const double x = axes.mapX(i);
        painter.drawLine(QPointF(x, axes.frame.bottom()), QPointF(x, axes.frame.bottom() - kTick));
        painter.drawText(QRectF(x - 70, axes.frame.bottom() + 3, 140, 30), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                         xCategoryLabels[i]);
    }
    painter.setFont(chartFont(9));
    if (!xLabel.isEmpty()) {
        painter.drawText(QRectF(axes.frame.left(), axes.frame.bottom() + 20, axes.frame.width(), 18), Qt::AlignHCenter, xLabel);
    }
    if (!yLabel.isEmpty()) {
        painter.save();
        painter.translate(axes.frame.left() - 44, axes.frame.center().y());
        painter.rotate(-90);
        painter.drawText(QRectF(-axes.frame.height() / 2 - 40, -16, axes.frame.height() + 80, 16), Qt::AlignHCenter, yLabel);
        painter.restore();
    }
}

void drawLegend(QPainter& painter, QPointF topLeft, const QStringList& names, const QVector<QColor>& colors,
                bool squares) {
    painter.setFont(chartFont(8.5));
    for (int i = 0; i < names.size(); ++i) {
        const QPointF p = topLeft + QPointF(0, i * 17);
        if (squares) {
            painter.setPen(QPen(kInk, 0.4));
            painter.setBrush(colors.value(i));
            painter.drawRect(QRectF(p.x(), p.y() + 3, 11, 11));
        } else {
            painter.setPen(QPen(colors.value(i), 2.2));
            painter.drawLine(p + QPointF(0, 8.5), p + QPointF(14, 8.5));
        }
        painter.setPen(kInk);
        painter.drawText(QPointF(p.x() + 18, p.y() + 13), names[i]);
    }
}

// ------------------------------------------------------------------ progresso

void ProgressChart::paintChart(QPainter& painter, const QRectF& area) const {
    if (data_.isEmpty()) {
        paintEmpty(painter, area, "Nenhuma revisão concluída ainda");
        return;
    }
    const QDate first = data_.front().first.addDays(-1);
    const QDate last = data_.back().first.addDays(1);
    int cumulative = 0;
    QVector<QPointF> line;
    for (const auto& [day, n] : data_) {
        cumulative += n;
        line.push_back({static_cast<double>(first.daysTo(day)), static_cast<double>(cumulative)});
    }
    Axes axes{area.adjusted(56, 4, -4, -40), {0, static_cast<double>(first.daysTo(last))}, {0, cumulative * 1.1}};
    painter.setPen(QPen(QColor("#5f86b3"), 0.6));
    painter.setBrush(QColor("#B9D3EC"));
    const double width = std::max(2.0, axes.frame.width() / (axes.x.max + 1) * 0.8);
    for (const auto& [day, n] : data_) {
        const double x = axes.mapX(first.daysTo(day));
        painter.drawRect(QRectF(QPointF(x - width / 2, axes.mapY(n)), QPointF(x + width / 2, axes.mapY(0))));
    }
    QPainterPath path;
    for (int i = 0; i < line.size(); ++i) {
        const auto p = axes.map(line[i].x(), line[i].y());
        i == 0 ? path.moveTo(p) : path.lineTo(p);
    }
    painter.setPen(QPen(QColor(color::blue), 1.8));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
    painter.setPen(QPen(kInk, 0.6));
    painter.setBrush(QColor(color::blue));
    for (const auto& p : line) {
        painter.drawEllipse(axes.map(p.x(), p.y()), 3.6, 3.6);
    }
    drawFrame(painter, axes, {}, "Imagens revisadas", false);
    painter.setFont(chartFont(8.5));
    painter.setPen(kInk);
    const int step = std::max<int>(1, static_cast<int>(data_.size() / 6));
    for (int i = 0; i < data_.size(); i += step) {
        const double x = axes.mapX(first.daysTo(data_[i].first));
        painter.drawLine(QPointF(x, axes.frame.bottom()), QPointF(x, axes.frame.bottom() - kTick));
        painter.drawText(QRectF(x - 30, axes.frame.bottom() + 3, 60, 16), Qt::AlignHCenter, data_[i].first.toString("dd/MM"));
    }
    painter.setPen(kGrey);
    painter.drawText(QRectF(axes.frame.left(), axes.frame.bottom() + 20, axes.frame.width(), 16), Qt::AlignRight,
                     "Barras: por dia · linha: acumulado");
}

// ------------------------------------------------------------------ detecções por imagem

void CountChart::paintChart(QPainter& painter, const QRectF& area) const {
    if (values_.isEmpty()) {
        paintEmpty(painter, area, "Sem detecções");
        return;
    }
    QMap<int, int> counts;
    for (int v : values_) {
        ++counts[v];
    }
    const auto keys = counts.keys();
    const int top = *std::max_element(counts.begin(), counts.end());
    QStringList labels;
    for (int k : keys) {
        labels << QString::number(k);
    }
    Axes axes{area.adjusted(56, 4, -4, -40), {-0.6, keys.size() - 0.4}, {0, top * 1.14}};
    painter.setPen(QPen(kInk, 0.4));
    painter.setBrush(QColor(color::blue));
    painter.setFont(chartFont(8.5));
    const double width = axes.frame.width() / keys.size() * 0.75;
    for (int i = 0; i < keys.size(); ++i) {
        const int n = counts[keys[i]];
        const double x = axes.mapX(i);
        painter.setPen(QPen(kInk, 0.4));
        painter.drawRect(QRectF(QPointF(x - width / 2, axes.mapY(n)), QPointF(x + width / 2, axes.mapY(0))));
        painter.drawText(QRectF(x - 30, axes.mapY(n) - 17, 60, 16), Qt::AlignHCenter | Qt::AlignBottom, QString::number(n));
    }
    drawFrame(painter, axes, "Lesões detectadas na imagem", QStringLiteral("Imagens (n = %1)").arg(values_.size()), false,
              false, labels);
}

// ------------------------------------------------------------------ FROC

void FrocChart::paintChart(QPainter& painter, const QRectF& area) const {
    if (curves_.isEmpty()) {
        paintEmpty(painter, area, "Sem dados de validação");
        return;
    }
    QStringList names;
    QVector<QColor> colors;
    for (const auto& c : curves_) {
        names << c.name;
        colors << c.color;
    }
    const double legendWidth = 150;
    Axes axes{area.adjusted(58, 4, -legendWidth, -40), {0, 6}, {0, 1}};
    painter.save();
    painter.setClipRect(axes.frame);
    for (const auto& c : curves_) {
        QPainterPath path;
        for (int i = 0; i < c.points.size(); ++i) {
            const auto p = axes.map(c.points[i].x(), c.points[i].y());
            if (i == 0) {
                path.moveTo(p);
            } else {
                path.lineTo(p.x(), path.currentPosition().y());  // degrau horizontal-vertical
                path.lineTo(p);
            }
        }
        if (!c.points.isEmpty()) {
            path.lineTo(axes.frame.right(), path.currentPosition().y());
        }
        painter.setPen(QPen(c.color, 1.8));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
    }
    for (const auto& c : curves_) {
        if (c.operating) {
            painter.setPen(QPen(kInk, 0.7));
            painter.setBrush(c.color);
            painter.drawEllipse(axes.map(c.operating->x(), c.operating->y()), 4.5, 4.5);
        }
    }
    painter.restore();
    drawFrame(painter, axes, "Falsos positivos por imagem", QStringLiteral("Sensibilidade por lesão (n = %1)").arg(lesions_),
              true, true);
    drawLegend(painter, QPointF(axes.frame.right() + 12, axes.frame.top() + 4), names, colors, false);
    painter.setFont(chartFont(8));
    painter.setPen(kGrey);
    painter.drawText(QRectF(axes.frame.right() + 12, axes.frame.top() + names.size() * 17 + 12, legendWidth - 14, 40),
                     Qt::TextWordWrap, "Pontos: limiar de operação (melhor F1)");
}

// ------------------------------------------------------------------ barras agrupadas

void GroupedBarChart::paintChart(QPainter& painter, const QRectF& area) const {
    bool any = false;
    for (const auto& s : bars_) {
        for (const auto& b : s) {
            any = any || !std::isnan(b.value);
        }
    }
    if (!any) {
        paintEmpty(painter, area, empty_);
        return;
    }
    const double legendWidth = legendTop_ ? 0 : 150;
    QRectF frame = area.adjusted(58, legendTop_ ? 24 : 4, -legendWidth - 4, -44);
    Axes axes{frame, {-0.5, categories_.size() - 0.5}, {0, 1.02}};
    const int ns = static_cast<int>(series_.size());
    const double group = axes.frame.width() / categories_.size() * 0.85;
    const double width = group / ns;
    painter.setFont(chartFont(8));
    for (int s = 0; s < ns; ++s) {
        for (int c = 0; c < categories_.size(); ++c) {
            const Bar& b = bars_[s].value(c);
            if (std::isnan(b.value)) {
                continue;
            }
            const double x = axes.mapX(c) - group / 2 + width * (s + 0.5);
            painter.setPen(QPen(kInk, 0.4));
            painter.setBrush(colors_.value(s));
            painter.drawRect(QRectF(QPointF(x - width / 2, axes.mapY(b.value)), QPointF(x + width / 2, axes.mapY(0))));
            if (!std::isnan(b.lower)) {
                painter.setPen(QPen(kInk, 0.9));
                painter.drawLine(QPointF(x, axes.mapY(b.lower)), QPointF(x, axes.mapY(b.upper)));
            }
            if (!b.label.isEmpty()) {
                painter.setPen(Qt::white);
                painter.drawText(QRectF(x - width / 2, axes.mapY(0.03) - 14, width, 14), Qt::AlignHCenter, b.label);
            }
        }
    }
    drawFrame(painter, axes, {}, yLabel_, false, true, categories_);
    if (legendTop_) {
        painter.setFont(chartFont(8.5));
        double x = axes.frame.left();
        for (int s = 0; s < ns; ++s) {
            painter.setPen(QPen(kInk, 0.4));
            painter.setBrush(colors_.value(s));
            painter.drawRect(QRectF(x, area.top() + 4, 11, 11));
            painter.setPen(kInk);
            painter.drawText(QPointF(x + 16, area.top() + 14), series_[s]);
            x += 30 + QFontMetricsF(painter.font()).horizontalAdvance(series_[s]);
        }
    } else {
        drawLegend(painter, QPointF(axes.frame.right() + 12, axes.frame.top() + 4), series_, colors_);
    }
}

// ------------------------------------------------------------------ mapa de lesões

void LesionMapChart::paintChart(QPainter& painter, const QRectF& area) const {
    if (lesions_.isEmpty()) {
        paintEmpty(painter, area, "Sem lesões de referência");
        return;
    }
    auto ordered = lesions_;
    std::stable_sort(ordered.begin(), ordered.end(), [](const Lesion& a, const Lesion& b) {
        if (a.upper != b.upper) {
            return a.upper;
        }
        return a.diameter < b.diameter;
    });
    const int upper = static_cast<int>(std::count_if(ordered.begin(), ordered.end(), [](const Lesion& l) { return l.upper; }));
    int none = 0;
    for (const auto& l : ordered) {
        none += std::none_of(l.found.begin(), l.found.end(), [](bool v) { return v; }) ? 1 : 0;
    }
    painter.setFont(chartFont(8.5));
    const QFontMetricsF fm(painter.font());
    double labelWidth = 0;
    for (const auto& m : models_) {
        labelWidth = std::max(labelWidth, fm.horizontalAdvance(m));
    }
    const QRectF grid = area.adjusted(labelWidth + 12, 24, -4, -24);
    const double w = grid.width() / ordered.size();
    const double h = grid.height() / models_.size();
    for (int r = 0; r < models_.size(); ++r) {
        painter.setPen(kInk);
        painter.drawText(QRectF(area.left(), grid.top() + r * h, labelWidth + 6, h), Qt::AlignRight | Qt::AlignVCenter, models_[r]);
        for (int c = 0; c < ordered.size(); ++c) {
            const bool found = ordered[c].found.value(models_[r], false);
            painter.setPen(QPen(Qt::white, 0.8));
            painter.setBrush(found ? colors_.value(r) : QColor("#ededed"));
            painter.drawRect(QRectF(grid.left() + c * w, grid.top() + r * h, w, h));
        }
    }
    const double cut = grid.left() + upper * w;
    painter.setPen(QPen(QColor("#4d4d4d"), 1, Qt::DashLine));
    painter.drawLine(QPointF(cut, grid.top() - 4), QPointF(cut, grid.bottom()));
    painter.setFont(chartFont(9, true));
    painter.setPen(kInk);
    painter.drawText(QRectF(grid.left(), area.top(), cut - grid.left(), 20), Qt::AlignCenter,
                     QStringLiteral("Maxila (n = %1)").arg(upper));
    painter.drawText(QRectF(cut, area.top(), grid.right() - cut, 20), Qt::AlignCenter,
                     QStringLiteral("Mandíbula (n = %1)").arg(ordered.size() - upper));
    painter.setFont(chartFont(8.5));
    painter.drawText(QRectF(grid.left(), grid.bottom() + 4, grid.width(), 18), Qt::AlignHCenter,
                     QStringLiteral("Lesões marcadas, da menor para a maior em cada metade. Nenhum modelo encontrou %1 de %2.")
                         .arg(none).arg(ordered.size()));
}

// ------------------------------------------------------------------ diagrama do fluxo

void FlowDiagram::paintChart(QPainter& painter, const QRectF& area) const {
    const auto fmt = [](int v) { return QLocale(QLocale::Portuguese, QLocale::Brazil).toString(v); };
    struct Node { QString text; QColor fill; QPointF pos; };  // pos em fração da área
    const QVector<Node> nodes{
        {"Dados públicos\nHanói + DENTEX", QColor("#EAF2FA"), {0.09, 0.30}},
        {values_.model, QColor("#B9D3EC"), {0.33, 0.30}},
        {QStringLiteral("Acervo local\n%1 panorâmicas").arg(fmt(values_.images)), Qt::white, {0.33, 0.68}},
        {QStringLiteral("Detecções\n%1 lesões em %2 imagens").arg(fmt(values_.detections), fmt(values_.withDetection)),
         QColor("#D5E5F3"), {0.56, 0.49}},
        {QStringLiteral("Revisão do especialista\n%1 imagens concluídas\namostra: %2 de %3")
             .arg(fmt(values_.completed)).arg(values_.sampleCompleted).arg(values_.sample), QColor("#D9F2E3"), {0.79, 0.49}},
        {QStringLiteral("Precisão na amostra\n%1").arg(values_.samplePrecision), Qt::white, {0.79, 0.86}},
        {QStringLiteral("Novas marcações\n%1 lesões adicionadas").arg(fmt(values_.added)), Qt::white, {0.79, 0.12}},
    };
    painter.setFont(chartFont(8.5));
    const QFontMetricsF fm(painter.font());
    QVector<QRectF> boxes;
    for (const auto& n : nodes) {
        const QRectF text = fm.boundingRect(QRectF(0, 0, 400, 200), Qt::AlignCenter, n.text);
        const QPointF c(area.left() + n.pos.x() * area.width(), area.top() + n.pos.y() * area.height());
        boxes.push_back(QRectF(c.x() - text.width() / 2 - 10, c.y() - text.height() / 2 - 6, text.width() + 20, text.height() + 12));
    }
    auto edgePoint = [](const QRectF& box, QPointF toward) {
        const QPointF c = box.center();
        const QPointF d = toward - c;
        const double sx = d.x() == 0 ? 1e9 : (box.width() / 2) / std::abs(d.x());
        const double sy = d.y() == 0 ? 1e9 : (box.height() / 2) / std::abs(d.y());
        return c + d * std::min(sx, sy);
    };
    auto arrow = [&](int a, int b, bool dashed = false, const QString& label = {}) {
        const QPointF p1 = edgePoint(boxes[a], boxes[b].center());
        const QPointF p2 = edgePoint(boxes[b], boxes[a].center());
        painter.setPen(QPen(QColor("#5b6b7c"), 1.1, dashed ? Qt::DashLine : Qt::SolidLine));
        painter.drawLine(p1, p2);
        const QPointF d = (p2 - p1) / std::hypot((p2 - p1).x(), (p2 - p1).y());
        const QPointF n(-d.y(), d.x());
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#5b6b7c"));
        painter.drawPolygon(QPolygonF{p2, p2 - d * 8 + n * 3.5, p2 - d * 8 - n * 3.5});
        if (!label.isEmpty()) {
            painter.setPen(QColor("#5b6b7c"));
            painter.setFont(chartFont(8));
            painter.drawText((p1 + p2) / 2 + QPointF(6, -4), label);
            painter.setFont(chartFont(8.5));
        }
    };
    arrow(0, 1);
    arrow(1, 3);
    arrow(2, 3);
    arrow(3, 4);
    arrow(4, 5);
    arrow(4, 6);
    arrow(6, 1, true, "ajuste fino");
    for (int i = 0; i < nodes.size(); ++i) {
        painter.setPen(QPen(QColor(color::darkBlue), 1.2));
        painter.setBrush(nodes[i].fill);
        painter.drawRoundedRect(boxes[i], 6, 6);
        painter.setPen(kInk);
        painter.drawText(boxes[i], Qt::AlignCenter, nodes[i].text);
    }
}

}  // namespace odonto
