// Aplicar o modelo a novas radiografias (mod_aplicar.R + fct_aplicar.R). O pipeline C++ validado roda
// numa thread separada; o banco é gravado na thread da interface, à medida que cada radiografia termina.

#include "Contours.h"
#include "Icons.h"
#include "Metrics.h"
#include "Pane.h"
#include "pages/Pages.h"

#include "ml_odonto/inference.hpp"
#include "ml_odonto/pipeline.hpp"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QSplitter>
#include <QTableWidget>
#include <QThread>
#include <QVBoxLayout>

#include <atomic>
#include <cstring>

namespace odonto {

// Procura o modelo ONNX: ao lado do programa, na pasta do projeto ou no pacote R instalado
QString findModel(const QString& fileName, const QString& project) {
    const QString app = QCoreApplication::applicationDirPath();
    const QStringList candidates{QDir(app).filePath("modelos/" + fileName),
                                 QDir(app).filePath("software/odontolesao/inst/modelos/" + fileName),
                                 QDir(app).filePath("../software/odontolesao/inst/modelos/" + fileName),
                                 QDir(project).filePath("software/odontolesao/inst/modelos/" + fileName)};
    for (const auto& c : candidates) {
        if (QFileInfo::exists(c)) return QFileInfo(c).canonicalFilePath();
    }
    return {};
}

class Worker : public QThread {
    Q_OBJECT
public:
    QStringList files;
    QString model;
    int modelWidth{2000};
    float threshold{0.5F};
    std::atomic<bool> cancelled{false};

signals:
    void started(int index);
    void finished(int index, const QVector<NewDetection>& lesions, int width, int height, double seconds);
    void failed(int index, const QString& message);
    void fatal(const QString& message);

protected:
    void run() override {
        std::unique_ptr<ml_odonto::OnnxEngine> engine;
        try {
            engine = std::make_unique<ml_odonto::OnnxEngine>(model.toStdWString());
        } catch (const std::exception& e) {
            emit fatal(QStringLiteral("Não foi possível carregar o modelo: %1").arg(e.what()));
            return;
        }
        ml_odonto::PipelineOptions options;
        options.model_width = static_cast<std::size_t>(modelWidth);
        options.threshold = threshold;
        for (int i = 0; i < files.size() && !cancelled; ++i) {
            emit started(i);
            QElapsedTimer timer;
            timer.start();
            try {
                QString error;
                const QImage image = readRadiograph(files[i], &error);
                if (image.isNull()) {
                    emit failed(i, error.isEmpty() ? QStringLiteral("imagem ilegível") : error);
                    continue;
                }
                ml_odonto::GrayImage gray(static_cast<std::size_t>(image.height()), static_cast<std::size_t>(image.width()), 0);
                for (int y = 0; y < image.height(); ++y) {
                    std::memcpy(gray.data() + static_cast<std::size_t>(y) * gray.width(), image.constScanLine(y), gray.width());
                }
                const auto result = ml_odonto::run_pipeline(gray, *engine, options);
                const double factor = double(image.width()) / result.probability.width();
                QVector<NewDetection> lesions;
                for (const auto& l : lesionsFromMask(result.mask, result.detections, threshold, factor)) {
                    lesions.push_back({l.index, l.contour, l.center, l.area, l.confidence});
                }
                emit finished(i, lesions, image.width(), image.height(), timer.elapsed() / 1000.0);
            } catch (const std::exception& e) {
                emit failed(i, e.what());
            }
        }
    }
};

AplicarPage::AplicarPage(Context& ctx, QWidget* parent) : QWidget(parent), ctx_(ctx) {
    // ---- painel de entrada
    auto* input = new QWidget;
    auto* v = new QVBoxLayout(input);
    v->setContentsMargins(10, 8, 10, 8);
    auto* intro = new QLabel("Indique uma pasta com radiografias (JPG, PNG ou TIFF) ou selecione os arquivos. "
                             "As imagens não saem deste computador.");
    intro->setWordWrap(true);
    intro->setStyleSheet("color:#5f6b7a;");
    folder_ = new QLineEdit;
    folder_->setPlaceholderText("Ex.: D:\\Radiografias\\2026-11");
    auto* browse = new QPushButton(icons::glyph(icons::folder), "Pasta…");
    auto* files = new QPushButton("Selecionar arquivos…");
    auto* folderRow = new QHBoxLayout;
    folderRow->addWidget(folder_, 1);
    folderRow->addWidget(browse);
    found_ = new QLabel;
    found_->setWordWrap(true);
    found_->setStyleSheet("background:#eaf4fd; border:1px solid #b6d4f0; padding:4px 6px;");
    found_->hide();
    threshold_ = new QSlider(Qt::Horizontal);
    threshold_->setRange(1, 19);  // 0,05 a 0,95
    thresholdLabel_ = new QLabel;
    auto* note = new QLabel("0,5 é o limiar de operação validado (69% das lesões, sem falsos positivos nas 20 panorâmicas "
                            "marcadas). Valores menores encontram mais lesões e mais alarmes falsos.");
    note->setWordWrap(true);
    note->setStyleSheet("color:#5f6b7a; font-size:8.5pt;");
    skip_ = new QCheckBox("Pular radiografias que já estão no banco");
    skip_->setChecked(true);
    apply_ = new QPushButton(icons::glyph(icons::play, Qt::white), "Aplicar o modelo");
    apply_->setProperty("primary", true);
    cancel_ = new QPushButton(icons::glyph(icons::stop, QColor("#b42318")), "Cancelar");
    cancel_->setProperty("danger", true);
    cancel_->setEnabled(false);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(apply_);
    buttons->addWidget(cancel_);
    buttons->addStretch(1);
    v->addWidget(intro);
    v->addWidget(new QLabel("<b>Pasta</b>"));
    v->addLayout(folderRow);
    v->addWidget(files);
    v->addWidget(found_);
    v->addSpacing(8);
    v->addWidget(thresholdLabel_);
    v->addWidget(threshold_);
    v->addWidget(note);
    v->addWidget(skip_);
    v->addLayout(buttons);
    v->addStretch(1);
    auto* inputPane = new Pane("Novas radiografias", input);

    // ---- andamento e tabela
    auto* progressBox = new QWidget;
    auto* p = new QVBoxLayout(progressBox);
    p->setContentsMargins(10, 8, 10, 8);
    status_ = new QLabel("Nenhuma aplicação em andamento.");
    status_->setStyleSheet("color:#5f6b7a;");
    progress_ = new QProgressBar;
    progress_->setTextVisible(false);
    progress_->setFixedHeight(10);
    warnings_ = new QLabel;
    warnings_->setWordWrap(true);
    warnings_->setStyleSheet("color:#9a6b00; font-size:8.5pt;");
    p->addWidget(status_);
    p->addWidget(progress_);
    p->addWidget(warnings_);
    p->addStretch(1);
    auto* progressPane = new Pane("Andamento", progressBox);

    table_ = new QTableWidget;
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setStretchLastSection(true);
    auto* tablePane = new Pane("Radiografias processadas", table_);
    auto* csv = new QPushButton(icons::glyph(icons::download), "CSV");
    auto* review = new QPushButton("Revisar selecionada");
    review->setProperty("primary", true);
    tablePane->addTool(csv);
    tablePane->addTool(review);

    auto* right = splitter(Qt::Vertical, {progressPane, tablePane}, {1, 4});
    right->setSizes({170, 700});
    auto* split = splitter(Qt::Horizontal, {inputPane, right}, {1, 3});
    split->setSizes({380, 1100});
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->addWidget(split);

    auto updateThreshold = [this] { thresholdLabel_->setText("<b>Limiar de confiança:</b> " + decimal(threshold_->value() * 0.05)); };
    connect(threshold_, &QSlider::valueChanged, this, updateThreshold);
    threshold_->setValue(qRound((ctx_.model ? ctx_.model->threshold : 0.5) / 0.05));
    updateThreshold();

    auto listFolder = [this] {
        const QString dir = folder_->text().trimmed().remove('"');
        if (!QDir(dir).exists()) {
            QMessageBox::warning(this, "Aplicar", "Pasta não encontrada.");
            return;
        }
        QStringList found;
        QDirIterator it(dir, {"*.jpg", "*.jpeg", "*.png", "*.tif", "*.tiff", "*.bmp"}, QDir::Files);
        while (it.hasNext()) found << it.next();
        found.sort();
        setQueue(found);
    };
    connect(folder_, &QLineEdit::returnPressed, this, listFolder);
    connect(&ctx_, &Context::applyRequested, this, [this, listFolder](const QString& folder) {
        folder_->setText(QDir::toNativeSeparators(folder));
        listFolder();
        start();
    });
    connect(browse, &QPushButton::clicked, this, [this, listFolder] {
        const QString dir = QFileDialog::getExistingDirectory(this, "Pasta com radiografias", folder_->text());
        if (!dir.isEmpty()) {
            folder_->setText(QDir::toNativeSeparators(dir));
            listFolder();
        }
    });
    connect(files, &QPushButton::clicked, this, [this] {
        const auto chosen = QFileDialog::getOpenFileNames(this, "Radiografias", folder_->text(),
                                                          "Radiografias (*.jpg *.jpeg *.png *.tif *.tiff *.bmp)");
        if (!chosen.isEmpty()) setQueue(chosen);
    });
    connect(apply_, &QPushButton::clicked, this, &AplicarPage::start);
    connect(cancel_, &QPushButton::clicked, this, [this] {
        if (worker_) {
            worker_->cancelled = true;
            status_->setText("Cancelando… (termina a radiografia atual)");
        }
    });
    connect(review, &QPushButton::clicked, this, [this] {
        const int row = table_->currentRow();
        if (row < 0) {
            QMessageBox::information(this, "Aplicar", "Selecione uma linha da tabela.");
            return;
        }
        ctx_.openInReview(table_->item(row, 0)->text());
    });
    connect(csv, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getSaveFileName(
            this, "Exportar detecções", QStringLiteral("ml_odonto_aplicacoes_%1.csv").arg(QDate::currentDate().toString("yyyy-MM-dd")),
            "CSV (*.csv)");
        if (file.isEmpty()) return;
        QFile out(file);
        if (out.open(QIODevice::WriteOnly)) {
            out.write("\xEF\xBB\xBF");  // BOM: o Excel reconhece os acentos
            out.write(ctx_.db.detectionsCsv(ctx_.modelId()).toUtf8());
        }
    });
    connect(&ctx_, &Context::changed, this, &AplicarPage::reloadTable);
    reloadTable();
}

AplicarPage::~AplicarPage() {
    if (worker_) {
        worker_->cancelled = true;
        worker_->wait();
    }
}

void AplicarPage::setQueue(const QStringList& files) {
    queue_ = files;
    if (files.isEmpty()) {
        found_->setText("Nenhuma radiografia encontrada.");
    } else {
        const int seconds = files.size() * 14;
        found_->setText(QStringLiteral("%1 radiografia(s) prontas. Tempo estimado: cerca de %2.<br>Motor de inferência: C++ (ONNX Runtime).")
                            .arg(files.size())
                            .arg(seconds < 90 ? QStringLiteral("%1 s").arg(seconds) : QStringLiteral("%1 min").arg(qRound(seconds / 60.0))));
    }
    found_->show();
}

void AplicarPage::start() {
    if (worker_) {
        QMessageBox::information(this, "Aplicar", "Já há uma aplicação em andamento.");
        return;
    }
    if (queue_.isEmpty()) {
        QMessageBox::information(this, "Aplicar", "Indique uma pasta ou selecione arquivos primeiro.");
        return;
    }
    if (!ctx_.model) {
        QMessageBox::warning(this, "Aplicar", "Nenhum modelo ativo no banco.");
        return;
    }
    const QString onnx = findModel(QFileInfo(ctx_.model->file).completeBaseName() + ".onnx", ctx_.db.config("projeto"));
    if (onnx.isEmpty()) {
        QMessageBox::warning(this, "Aplicar", "O modelo ONNX não foi encontrado. Gere-o com: python cpp/tools/build.py");
        return;
    }

    // o que processar (como processar_lote: pula as do banco e protege as já revisadas)
    QStringList pending;
    QVector<std::optional<QString>> existing;
    QStringList warnings;
    for (const auto& file : queue_) {
        const auto id = ctx_.db.imageIdByPath(QFileInfo(file).absoluteFilePath());
        if (id && skip_->isChecked()) continue;
        if (id && ctx_.db.hasReview(*id, ctx_.modelId())) {
            warnings << QFileInfo(file).fileName() + ": já revisada; detecções mantidas";
            continue;
        }
        pending << QFileInfo(file).absoluteFilePath();
        existing << id;
    }
    const int total = static_cast<int>(queue_.size());
    const int skipped = total - static_cast<int>(pending.size());
    warnings_->setText(warnings.join(" · "));
    progress_->setRange(0, std::max(1, total));
    progress_->setValue(skipped);
    if (pending.isEmpty()) {
        status_->setText(QStringLiteral("Nada a processar: %1 radiografia(s) já estão no banco.").arg(total));
        return;
    }

    const double threshold = threshold_->value() * 0.05;
    worker_ = new Worker;
    worker_->files = pending;
    worker_->model = onnx;
    worker_->modelWidth = ctx_.model->width;
    worker_->threshold = static_cast<float>(threshold);
    apply_->setEnabled(false);
    cancel_->setEnabled(true);
    status_->setText("Carregando o modelo…");
    auto done = std::make_shared<int>(skipped);
    auto errors = std::make_shared<QStringList>(warnings);
    QElapsedTimer* clock = new QElapsedTimer;
    clock->start();

    connect(worker_, &Worker::started, this, [this, pending, done, total, clock](int i) {
        QString eta;
        const int processed = *done;
        if (i > 0) {
            const double perImage = clock->elapsed() / 1000.0 / i;
            eta = QStringLiteral(" · faltam cerca de %1 min").arg(std::max(1, qRound(perImage * (total - processed) / 60.0)));
        }
        status_->setText(QStringLiteral("<b>%1 de %2</b> · processando · %3%4").arg(processed).arg(total)
                             .arg(QFileInfo(pending[i]).fileName(), eta));
    });
    connect(worker_, &Worker::finished, this,
            [this, pending, existing, threshold, done](int i, const QVector<NewDetection>& lesions, int w, int h, double seconds) {
                ctx_.db.saveApplication(existing[i], pending[i], w, h, ctx_.modelId(), lesions, threshold, seconds);
                progress_->setValue(++*done);
                ctx_.dataChanged();
            });
    connect(worker_, &Worker::failed, this, [this, pending, done, errors](int i, const QString& message) {
        *errors << QFileInfo(pending[i]).fileName() + ": " + message;
        warnings_->setText(errors->join(" · "));
        progress_->setValue(++*done);
    });
    connect(worker_, &Worker::fatal, this, [this](const QString& message) { QMessageBox::critical(this, "Aplicar", message); });
    connect(worker_, &QThread::finished, this, [this, pending, total, done, clock] {
        const bool cancelled = worker_->cancelled;
        worker_->deleteLater();
        worker_ = nullptr;
        delete clock;
        apply_->setEnabled(true);
        cancel_->setEnabled(false);
        ctx_.db.logEvent("", "aplicacao", QStringLiteral("%1 radiografias, modelo %2, limiar %3, motor C++ nativo")
                                              .arg(total).arg(ctx_.model->name, decimal(threshold_->value() * 0.05)));
        status_->setText(cancelled ? QStringLiteral("Cancelada. As radiografias já processadas foram mantidas.")
                                   : QStringLiteral("<b>Concluída:</b> %1 radiografia(s). As detecções já estão na fila de revisão.")
                                         .arg(total));
        ctx_.dataChanged();
    });
    worker_->start();
}

void AplicarPage::reloadTable() {
    const auto rows = ctx_.db.applications(ctx_.modelId());
    const QStringList headers{"ID", "Arquivo", "Lesões", "Segundos", "Limiar", "Processada em", "Confiança máx."};
    table_->setColumnCount(headers.size());
    table_->setHorizontalHeaderLabels(headers);
    table_->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        const QStringList cells{r.id, r.file, QString::number(r.lesions), decimal(r.seconds, 1), decimal(r.threshold),
                                r.at, r.maxConfidence ? decimal(*r.maxConfidence) : QStringLiteral("—")};
        for (int c = 0; c < cells.size(); ++c) {
            table_->setItem(i, c, new QTableWidgetItem(cells[c]));
        }
    }
    table_->resizeColumnsToContents();
}

}  // namespace odonto

#include "AplicarPage.moc"
