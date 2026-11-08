#include "Metrics.h"

#include <QLocale>

#include <QMap>

#include <algorithm>
#include <random>
#include <cmath>

namespace odonto {

Proportion wilson(int x, int n, double z) {
    Proportion result;
    result.x = x;
    result.n = n;
    if (n <= 0) {
        return result;
    }
    const double p = static_cast<double>(x) / n;
    const double d = 1 + z * z / n;
    const double center = (p + z * z / (2.0 * n)) / d;
    const double half = z * std::sqrt(p * (1 - p) / n + z * z / (4.0 * n * n)) / d;
    result.p = p;
    result.lower = std::max(0.0, center - half);
    result.upper = std::min(1.0, center + half);
    return result;
}

QString percent(double value) {
    return QLocale(QLocale::Portuguese, QLocale::Brazil).toString(std::nearbyint(100 * value), 'f', 0) + "%";
}

QString decimal(double value, int digits) {
    if (std::isnan(value)) {
        return QStringLiteral("—");
    }
    return QLocale(QLocale::Portuguese, QLocale::Brazil).toString(value, 'f', digits);
}

QString formatProportion(const Proportion& p, bool brief) {
    if (!p.valid() || std::isnan(p.p)) {
        return QStringLiteral("—");
    }
    if (brief) {
        return percent(p.p);
    }
    QString lower = percent(p.lower);
    lower.chop(1);
    return QStringLiteral("%1 (IC 95% %2–%3; %4/%5)")
        .arg(percent(p.p), lower, percent(p.upper))
        .arg(p.x)
        .arg(p.n);
}

const QStringList& modelOrder() {
    static const QStringList order{"RF (Hanói)", "RF (Hanói + nossas)", "U-Net (Hanói)", "U-Net + ajuste fino", "U-Net v2", "Duas etapas"};
    return order;
}

QColor modelColor(const QString& model) {
    static const QMap<QString, QColor> colors{{"RF (Hanói)", QColor("#B8C0C8")}, {"RF (Hanói + nossas)", QColor("#6B7782")},
                                              {"U-Net (Hanói)", QColor("#7FB2DE")}, {"U-Net + ajuste fino", QColor("#7A3E9D")},
                                              {"U-Net v2", QColor("#1F6FB4")}, {"Duas etapas", QColor("#0B2E59")}};
    return colors.value(model, QColor("#999999"));
}

QVector<QPair<QString, QVector<QPointF>>> frocCurves(const QVector<FrocRow>& rows) {
    QMap<QString, QVector<QPointF>> byModel;
    for (const auto& r : rows) {
        if (r.images > 0 && r.lesions > 0) {
            byModel[r.model].push_back({double(r.predicted - r.correct) / r.images, double(r.found) / r.lesions});
        }
    }
    QVector<QPair<QString, QVector<QPointF>>> curves;
    for (const auto& model : modelOrder()) {
        if (!byModel.contains(model)) continue;
        auto points = byModel[model];
        std::sort(points.begin(), points.end(), [](const QPointF& a, const QPointF& b) {
            return a.x() != b.x() ? a.x() < b.x() : a.y() > b.y();
        });
        QVector<QPointF> curve;
        double best = 0;
        for (const auto& p : points) {
            if (!curve.isEmpty() && curve.back().x() == p.x()) continue;  // mantém a maior sensibilidade por FP
            best = std::max(best, p.y());
            curve.push_back({p.x(), best});
        }
        curves.push_back({model, curve});
    }
    return curves;
}

QVector<OperatingMetrics> operatingMetrics(const QVector<PatientRow>& rows, int bootstrap, unsigned seed) {
    QMap<QString, QVector<PatientRow>> byModel;
    for (const auto& r : rows) {
        byModel[r.model].push_back(r);
    }
    std::mt19937 rng(seed);
    QVector<OperatingMetrics> out;
    for (const auto& model : modelOrder()) {
        if (!byModel.contains(model)) continue;
        const auto& d = byModel[model];
        int lesions = 0, found = 0, predicted = 0, correct = 0;
        double inter = 0, sum = 0;
        for (const auto& r : d) {
            lesions += r.lesions; found += r.found; predicted += r.predicted; correct += r.correct;
            inter += r.inter; sum += r.sum;
        }
        OperatingMetrics m;
        m.model = model;
        m.threshold = d.front().threshold;
        m.sensitivity = wilson(found, lesions);
        m.precision = wilson(correct, std::max(1, predicted));
        m.dice = sum > 0 ? 2 * inter / sum : qQNaN();
        m.fpPerImage = double(predicted - correct) / d.size();
        // bootstrap por paciente para o Dice (o R usa outro gerador: os limites podem diferir na 3ª casa)
        std::uniform_int_distribution<int> pick(0, static_cast<int>(d.size()) - 1);
        QVector<double> samples;
        for (int b = 0; b < bootstrap; ++b) {
            double bi = 0, bs = 0;
            for (int k = 0; k < d.size(); ++k) {
                const auto& r = d[pick(rng)];
                bi += r.inter;
                bs += r.sum;
            }
            if (bs > 0) samples.push_back(2 * bi / bs);
        }
        if (!samples.isEmpty()) {
            std::sort(samples.begin(), samples.end());
            auto quantile = [&](double q) {  // tipo 7, como o quantile() do R
                const double h = (samples.size() - 1) * q;
                const int lo = static_cast<int>(std::floor(h));
                const int hi = std::min(lo + 1, static_cast<int>(samples.size()) - 1);
                return samples[lo] + (h - lo) * (samples[hi] - samples[lo]);
            };
            m.diceLower = quantile(0.025);
            m.diceUpper = quantile(0.975);
        }
        out.push_back(m);
    }
    return out;
}

}  // namespace odonto
