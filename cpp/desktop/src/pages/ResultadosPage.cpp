// Resultados: validação nas 20 panorâmicas marcadas e revisão do acervo (mod_resultados.R).

#include "Charts.h"
#include "Metrics.h"
#include "Pane.h"
#include "pages/Pages.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>

namespace odonto {
namespace {

Pane* chartPane(const QString& title, ChartWidget* chart, const QString& name, int height, const QString& note = {}) {
    QWidget* body = chart;
    if (!note.isEmpty()) {
        body = new QWidget;
        auto* v = new QVBoxLayout(body);
        v->setContentsMargins(0, 0, 0, 4);
        v->addWidget(chart, 1);
        auto* n = new QLabel(note);
        n->setWordWrap(true);
        n->setStyleSheet("color:#5f6b7a; font-size:8.5pt; padding:0 8px;");
        v->addWidget(n);
    }
    auto* pane = new Pane(title, body);
    pane->addTool(pngButton(chart, name, pane));
    pane->setMinimumHeight(height);
    return pane;
}

QString interval(const Proportion& p) {
    if (!p.valid()) return QStringLiteral("—");
    return QStringLiteral("%1 (%2–%3)").arg(percent(p.p)).arg(std::nearbyint(100 * p.lower)).arg(std::nearbyint(100 * p.upper));  // metade para o par, como o R
}

}  // namespace

ResultadosPage::ResultadosPage(Context& ctx, QWidget* parent) : QWidget(parent), ctx_(ctx) {
    auto* tabs = new QTabWidget;

    // ---- validação
    froc_ = new FrocChart;
    metrics_ = new GroupedBarChart;
    map_ = new LesionMapChart;
    table_ = new QTableWidget;
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setStretchLastSection(true);
    auditModel_ = new QComboBox;
    excludeError_ = new QCheckBox("Não é lesão / erro de marcação");
    excludeInvisible_ = new QCheckBox("Não visível na panorâmica");
    excludeError_->setChecked(true);
    excludeInvisible_->setChecked(true);
    audited_ = new QLabel;
    audited_->setWordWrap(true);
    auto* auditBody = new QWidget;
    auto* a = new QVBoxLayout(auditBody);
    a->addWidget(new QLabel("Modelo"));
    a->addWidget(auditModel_);
    a->addWidget(new QLabel("Excluir da referência as lesões julgadas como"));
    a->addWidget(excludeError_);
    a->addWidget(excludeInvisible_);
    a->addWidget(audited_);
    a->addStretch(1);

    auto* validation = new QWidget;
    auto* grid = new QGridLayout(validation);
    grid->setContentsMargins(5, 5, 5, 5);
    grid->setSpacing(5);
    grid->addWidget(chartPane("Curvas FROC", froc_, "froc", 380), 0, 0);
    grid->addWidget(chartPane("Sensibilidade, precisão e Dice no limiar de operação", metrics_, "metricas", 380,
                              "Barras de erro: IC 95% (Wilson; bootstrap por paciente para o Dice)."), 0, 1);
    grid->addWidget(chartPane("Lesões encontradas por modelo", map_, "mapa", 300), 1, 0, 1, 2);
    auto* tablePane = new Pane("Tabela por modelo", table_);
    tablePane->setMinimumHeight(260);
    grid->addWidget(tablePane, 2, 0);
    auto* auditPane = new Pane("Referência corrigida pela auditoria", auditBody);
    grid->addWidget(auditPane, 2, 1);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    auto* scroll = new QScrollArea;
    scroll->setWidget(validation);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    tabs->addTab(scroll, "Validação (20 panorâmicas marcadas)");

    // ---- revisão do acervo
    precision_ = new GroupedBarChart;
    precision_->setEmptyText("Nenhuma revisão concluída ainda");
    perImage_ = new CountChart;
    kappa_ = new QLabel;
    kappa_->setWordWrap(true);
    kappa_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    kappa_->setMargin(8);
    auto* review = new QWidget;
    auto* rg = new QGridLayout(review);
    rg->setContentsMargins(5, 5, 5, 5);
    rg->setSpacing(5);
    rg->addWidget(chartPane("Desempenho estimado pela revisão", precision_, "precisao", 380,
                            "Use a amostra aleatória para relatar o desempenho; as demais imagens seguem a ordem de prioridade."),
                  0, 0);
    rg->addWidget(chartPane("Detecções por imagem no acervo", perImage_, "porimagem", 380), 0, 1);
    rg->addWidget(new Pane("Concordância entre revisores", kappa_), 1, 0, 1, 2);
    rg->setColumnStretch(0, 7);
    rg->setColumnStretch(1, 5);
    rg->setRowStretch(1, 1);
    tabs->addTab(review, "Revisão do acervo");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->addWidget(tabs);

    connect(auditModel_, &QComboBox::currentTextChanged, this, [this] { refreshAudited(); });
    connect(excludeError_, &QCheckBox::toggled, this, [this] { refreshAudited(); });
    connect(excludeInvisible_, &QCheckBox::toggled, this, [this] { refreshAudited(); });
    connect(&ctx_, &Context::changed, this, [this] {
        refreshReview();
        refreshAudited();
    });
    refreshValidation();
    refreshReview();
}

void ResultadosPage::refreshValidation() {
    const auto metrics = operatingMetrics(ctx_.db.patientRows());
    QVector<Curve> curves;
    int lesions = 0;
    for (const auto& r : ctx_.db.frocRows()) {
        lesions = std::max(lesions, r.lesions);
    }
    for (const auto& [model, points] : frocCurves(ctx_.db.frocRows())) {
        Curve c{model, modelColor(model), points, std::nullopt};
        for (const auto& m : metrics) {
            if (m.model == model) c.operating = QPointF(m.fpPerImage, m.sensitivity.p);
        }
        curves.push_back(c);
    }
    froc_->setCurves(curves, lesions);

    QStringList names;
    QVector<QColor> colors;
    QVector<QVector<Bar>> bars;
    for (const auto& m : metrics) {
        names << m.model;
        colors << modelColor(m.model);
        bars.push_back({Bar{m.sensitivity.p, m.sensitivity.lower, m.sensitivity.upper, {}},
                        Bar{m.precision.p, m.precision.lower, m.precision.upper, {}},
                        Bar{m.dice, m.diceLower, m.diceUpper, {}}});
    }
    metrics_->setData({"Sensibilidade", "Precisão", "Dice"}, names, colors, bars, "Valor no limiar de operação (IC 95%)");

    QMap<int, Lesion> lesionsById;
    QStringList models;
    for (const auto& r : ctx_.db.lesionMap()) {
        auto& l = lesionsById[r.id];
        l.id = r.id;
        l.upper = r.upper;
        l.diameter = r.diameter;
        l.found[r.model] = r.found;
        if (!models.contains(r.model)) models << r.model;
    }
    QStringList ordered;
    QVector<QColor> mapColors;
    for (const auto& m : modelOrder()) {
        if (models.contains(m)) {
            ordered << m;
            mapColors << modelColor(m);
        }
    }
    map_->setData(lesionsById.values().toVector(), ordered, mapColors);

    const QStringList headers{"Modelo", "Limiar", "Sensibilidade", "Precisão", "FP/imagem", "Dice"};
    table_->clear();
    table_->setColumnCount(headers.size());
    table_->setHorizontalHeaderLabels(headers);
    table_->setRowCount(metrics.size());
    for (int i = 0; i < metrics.size(); ++i) {
        const auto& m = metrics[i];
        const QStringList cells{m.model, decimal(m.threshold, 3), interval(m.sensitivity), interval(m.precision),
                                decimal(m.fpPerImage, 2),
                                QStringLiteral("%1 (%2–%3)").arg(decimal(m.dice), decimal(m.diceLower), decimal(m.diceUpper))};
        for (int c = 0; c < cells.size(); ++c) {
            table_->setItem(i, c, new QTableWidgetItem(cells[c]));
        }
    }
    table_->resizeColumnsToContents();

    auditModel_->blockSignals(true);
    auditModel_->clear();
    auditModel_->addItems(ordered);
    auditModel_->setCurrentText("U-Net v2");
    auditModel_->blockSignals(false);
    refreshAudited();
}

void ResultadosPage::refreshAudited() {
    if (ctx_.reviewer.isEmpty() || auditModel_->currentText().isEmpty()) {
        audited_->setText("<span style='color:#5f6b7a'>Defina o revisor para ver a referência corrigida.</span>");
        return;
    }
    QStringList excluded;
    if (excludeError_->isChecked()) excluded << "erro_marcacao";
    if (excludeInvisible_->isChecked()) excluded << "nao_visivel";
    const auto s = ctx_.db.auditedSensitivity(auditModel_->currentText(), ctx_.reviewer, excluded);
    if (!s) {
        audited_->clear();
        return;
    }
    audited_->setText(QStringLiteral(
        "<p>Lesões não encontradas: %1 · respondidas na auditoria por %2: %3 · excluídas: %4</p>"
        "<table width='100%' cellpadding='4'><tr><td>Sensibilidade com a referência original</td><td align='right'>%5</td></tr>"
        "<tr style='background:#d5e2f1'><td><b>Com a referência corrigida</b></td><td align='right'><b>%6</b></td></tr></table>"
        "<p style='color:#5f6b7a'>Critério por lesão no limiar de operação. Responda a auditoria para atualizar este número.</p>")
                          .arg(s->missed).arg(ctx_.reviewer.toHtmlEscaped()).arg(s->answered).arg(s->excluded)
                          .arg(formatProportion(s->original), formatProportion(s->corrected)));
}

void ResultadosPage::refreshReview() {
    const auto e = ctx_.db.reviewStats(ctx_.modelId(), ctx_.reviewer);
    auto bar = [](const Proportion& p) {
        return Bar{p.valid() ? p.p : qQNaN(), p.lower, p.upper, p.valid() ? QStringLiteral("%1/%2").arg(p.x).arg(p.n) : QString()};
    };
    precision_->setData({"Precisão", "Precisão (dúvida = rejeitada)", "Sensibilidade estimada"},
                        {"Amostra aleatória", "Todas as revisadas"}, {QColor(color::darkBlue), QColor(color::lightBlue)},
                        {{bar(e.sample.precision), bar(e.sample.precisionPessimistic), bar(e.sample.sensitivity)},
                         {bar(e.all.precision), bar(e.all.precisionPessimistic), bar(e.all.sensitivity)}},
                        "Estimativa (IC 95%)", true);
    perImage_->setData(ctx_.db.detectionsPerImage(ctx_.modelId()));

    const auto k = ctx_.db.kappa(ctx_.modelId());
    if (k.isEmpty()) {
        kappa_->setText("<span style='color:#5f6b7a'>Disponível quando dois revisores tiverem concluído as mesmas imagens. "
                        "A concordância entre especialistas é o teto realista para qualquer modelo.</span>");
        return;
    }
    QString html = "<table cellpadding='4'><tr><th>Revisor 1</th><th>Revisor 2</th><th>Detecções em comum</th>"
                   "<th>Concordância</th><th>Kappa de Cohen</th></tr>";
    for (const auto& r : k) {
        html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td></tr>")
                    .arg(r.first.toHtmlEscaped(), r.second.toHtmlEscaped()).arg(r.detections)
                    .arg(percent(r.agreement), decimal(r.kappa));
    }
    kappa_->setText(html + "</table>");
}

}  // namespace odonto
