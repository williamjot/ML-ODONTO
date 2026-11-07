// Início: fluxo de trabalho, ambiente com os números atuais, atividade e progresso (mod_inicio.R).

#include "Charts.h"
#include "Metrics.h"
#include "Pane.h"
#include "pages/Pages.h"

#include <QFileDialog>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace odonto {

QPushButton* pngButton(ChartWidget* chart, const QString& name, QWidget* parent) {
    auto* button = new QPushButton("PNG", parent);
    button->setToolTip("Salvar o gráfico em PNG (alta resolução)");
    button->setStyleSheet("padding:0 6px; font-size:8.5pt;");
    QObject::connect(button, &QPushButton::clicked, parent, [chart, name, parent] {
        const QString file = QFileDialog::getSaveFileName(
            parent, "Salvar gráfico", QStringLiteral("odontolesao_%1_%2.png").arg(name, QDate::currentDate().toString("yyyy-MM-dd")),
            "Imagem PNG (*.png)");
        if (!file.isEmpty()) {
            chart->savePng(file);
        }
    });
    return button;
}

namespace {

QString tableHtml(const QVector<QPair<QString, QVector<QPair<QString, QString>>>>& sections) {
    QString html = "<style>td{padding:3px 8px;border-bottom:1px solid #eef1f5;font-family:Consolas;font-size:9pt}"
                   "th{background:#eef1f5;text-align:left;padding:3px 8px;font-size:9pt}</style>"
                   "<table width='100%' cellspacing='0'>";
    for (const auto& [title, rows] : sections) {
        if (!title.isEmpty()) {
            html += "<tr><th colspan='2'>" + title + "</th></tr>";
        }
        for (const auto& [name, value] : rows) {
            html += "<tr><td style='color:#5f6b7a'>" + name.toHtmlEscaped() + "</td><td>" + value.toHtmlEscaped() + "</td></tr>";
        }
    }
    return html + "</table>";
}

}  // namespace

InicioPage::InicioPage(Context& ctx, QWidget* parent) : QWidget(parent), ctx_(ctx) {
    flow_ = new FlowDiagram;
    auto* flowPane = new Pane("Fluxo de trabalho", flow_);
    auto* go = new QPushButton("▶ Continuar revisão");
    go->setProperty("primary", true);
    go->setStyleSheet("padding:1px 8px;");
    connect(go, &QPushButton::clicked, this, [this] { ctx_.goTo(Page::Revisao); });
    flowPane->addTool(go);

    environment_ = new QTextBrowser;
    model_ = new QTextBrowser;
    for (auto* b : {environment_, model_}) {
        b->setFrameShape(QFrame::NoFrame);
    }
    auto* envPane = new Pane(QStringList{"Ambiente", "Modelo"}, {environment_, model_});

    activity_ = new QPlainTextEdit;
    activity_->setObjectName("console");
    activity_->setReadOnly(true);
    auto* about = new QTextBrowser;
    about->setFrameShape(QFrame::NoFrame);
    about->setHtml("<p style='margin:10px'><b>ML ODONTO</b> reúne a revisão das detecções por especialistas, a auditoria das "
                   "lesões não encontradas, os resultados de validação e a aplicação do modelo a novas radiografias.</p>"
                   "<p style='margin:10px;color:#5f6b7a'>Tudo roda neste computador. Nenhuma radiografia é enviada para a internet.</p>");
    auto* consolePane = new Pane(QStringList{"Atividade", "Sobre"}, {activity_, about});

    progress_ = new ProgressChart;
    perImage_ = new CountChart;
    auto* chartsPane = new Pane(QStringList{"Progresso", "Detecções por imagem"}, {progress_, perImage_});

    auto* top = splitter(Qt::Horizontal, {flowPane, envPane}, {62, 38});
    auto* bottom = splitter(Qt::Horizontal, {consolePane, chartsPane}, {50, 50});
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->addWidget(splitter(Qt::Vertical, {top, bottom}, {58, 42}));

    connect(&ctx_, &Context::changed, this, &InicioPage::refresh);
    refresh();
}

void InicioPage::refresh() {
    const int modelId = ctx_.modelId();
    const Counts c = ctx_.db.counts(modelId);
    const ReviewStats e = ctx_.db.reviewStats(modelId, ctx_.reviewer);
    const QLocale br(QLocale::Portuguese, QLocale::Brazil);

    FlowDiagram::Values v;
    v.model = ctx_.model ? ctx_.model->description : QStringLiteral("—");
    v.images = c.images;
    v.detections = c.detections;
    v.withDetection = c.withDetection;
    v.completed = c.completed;
    v.sampleCompleted = c.sampleCompleted;
    v.sample = c.sample;
    v.added = e.all.added;
    v.samplePrecision = formatProportion(e.sample.precision, true);
    flow_->setValues(v);

    const auto [refLesions, refPatients] = ctx_.db.referenceCounts();
    QVector<QPair<QString, QVector<QPair<QString, QString>>>> sections{
        {"Dados", {{"acervo", QStringLiteral("%1 panorâmicas").arg(c.images)},
                   {"deteccoes", QStringLiteral("%1 lesões em %2 imagens").arg(br.toString(c.detections)).arg(c.withDetection)},
                   {"referencia", QStringLiteral("%1 lesões em %2 pacientes").arg(refLesions).arg(refPatients)},
                   {"revisores", QString::number(c.reviewers)}}},
        {"Revisão", {{"amostra_revisada", QStringLiteral("%1 / %2").arg(c.sampleCompleted).arg(c.sample)},
                     {"precisao_amostra", formatProportion(e.sample.precision)},
                     {"precisao_todas", formatProportion(e.all.precision)},
                     {"decisoes", QStringLiteral("%1 confirmadas · %2 rejeitadas · %3 dúvidas")
                                      .arg(e.all.confirmed).arg(e.all.rejected).arg(e.all.doubts)},
                     {"lesoes_adicionadas", QString::number(e.all.added)}}}};
    if (!ctx_.reviewer.isEmpty()) {
        if (const auto a = ctx_.db.auditedSensitivity("U-Net v2", ctx_.reviewer)) {
            sections.push_back({"Validação (U-Net v2)",
                                {{"sensibilidade", formatProportion(a->original)},
                                 {"sens_corrigida", QStringLiteral("%1 · auditoria %2/%3")
                                                        .arg(formatProportion(a->corrected)).arg(a->answered).arg(a->missed)}}});
        }
    }
    environment_->setHtml(tableHtml(sections));

    if (ctx_.model) {
        const auto& m = *ctx_.model;
        model_->setHtml(tableHtml({{QString(), {{"nome", m.name}, {"descricao", m.description}, {"tipo", m.type},
                                               {"largura", QStringLiteral("%1 px").arg(m.width)}, {"limiar", decimal(m.threshold)},
                                               {"pesos", m.file}, {"registrado_em", m.createdAt}}}}));
    }

    QStringList lines;
    for (const auto& ev : ctx_.db.events()) {
        lines << QStringLiteral("> %1 %2 %3 %4").arg(ev.at.mid(5, 14), ev.reviewer, ev.type, ev.detail).simplified();
    }
    activity_->setPlainText(lines.isEmpty() ? QStringLiteral("> nenhuma atividade registrada ainda") : lines.join('\n'));

    progress_->setData(ctx_.db.dailyProgress(modelId));
    perImage_->setData(ctx_.db.detectionsPerImage(modelId));
}

}  // namespace odonto
