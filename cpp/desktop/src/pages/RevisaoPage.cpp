// Revisão das detecções (mod_revisao.R): lista de imagens por grupo, visor e rodapé. Tudo é gravado na hora.

#include "Contours.h"
#include "ImageViewer.h"
#include "Pane.h"
#include "pages/Pages.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QRadioButton>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

namespace odonto {
namespace {
const char* kGroups[] = {"", "1º Amostra aleatória", "4 ou mais detecções", "Nenhuma detecção", "Demais imagens"};
}

RevisaoPage::RevisaoPage(Context& ctx, QWidget* parent) : QWidget(parent), ctx_(ctx) {
    // lista
    auto* listBody = new QWidget;
    auto* listLayout = new QVBoxLayout(listBody);
    listLayout->setContentsMargins(8, 6, 8, 4);
    listLayout->setSpacing(4);
    sampleLabel_ = new QLabel;
    sampleBar_ = new QProgressBar;
    sampleBar_->setTextVisible(false);
    sampleBar_->setFixedHeight(6);
    allLabel_ = new QLabel;
    allLabel_->setStyleSheet("color:#5f6b7a;");
    auto* filters = new QHBoxLayout;
    filter_ = new QButtonGroup(this);
    int id = 0;
    for (const char* name : {"Todas", "Pendentes", "Concluídas"}) {
        auto* r = new QRadioButton(name);
        filter_->addButton(r, id++);
        filters->addWidget(r);
    }
    filter_->button(0)->setChecked(true);
    list_ = new QListWidget;
    list_->setFrameShape(QFrame::NoFrame);
    listLayout->addWidget(sampleLabel_);
    listLayout->addWidget(sampleBar_);
    listLayout->addWidget(allLabel_);
    listLayout->addLayout(filters);
    listLayout->addWidget(list_, 1);
    auto* listPane = new Pane("Imagens", listBody);

    // visor e rodapé
    viewer_ = new ImageViewer;
    auto* body = new QWidget;
    auto* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    auto* footer = new QWidget;
    footer->setObjectName("reviewFooter");
    footer->setStyleSheet("#reviewFooter{background:#f7f9fb; border-top:1px solid #c9d1db;}");
    auto* f = new QHBoxLayout(footer);
    f->setContentsMargins(8, 5, 8, 5);
    observations_ = new QLineEdit;
    observations_->setPlaceholderText("Observações (opcional)");
    inadequate_ = new QCheckBox("Imagem inadequada");
    auto* previous = new QPushButton("←");
    previous->setToolTip("Imagem anterior (←)");
    auto* next = new QPushButton("→");
    next->setToolTip("Próxima imagem (→)");
    auto* done = new QPushButton("✓ Concluir e ir para a próxima");
    done->setProperty("success", true);
    for (auto* b : {previous, next, done}) {
        b->setFocusPolicy(Qt::NoFocus);
    }
    f->addWidget(observations_, 1);
    f->addWidget(inadequate_);
    f->addWidget(previous);
    f->addWidget(next);
    f->addWidget(done);
    bodyLayout->addWidget(viewer_, 1);
    bodyLayout->addWidget(footer);
    auto* viewerPane = new Pane("Radiografia", body);
    saved_ = new QLabel;
    saved_->setStyleSheet("color:#5f6b7a; font-weight:400;");
    viewerPane->addTool(saved_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(5, 5, 5, 5);
    auto* split = splitter(Qt::Horizontal, {listPane, viewerPane}, {1, 4});
    split->setSizes({280, 1200});
    layout->addWidget(split);

    observationsTimer_ = new QTimer(this);
    observationsTimer_->setSingleShot(true);
    observationsTimer_->setInterval(800);

    connect(filter_, &QButtonGroup::idClicked, this, [this] { reloadList(); });
    connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (!item->data(Qt::UserRole).toString().isEmpty()) {
            open(item->data(Qt::UserRole).toString());
        }
    });
    connect(previous, &QPushButton::clicked, this, [this] { navigate(-1); });
    connect(next, &QPushButton::clicked, this, [this] { navigate(1); });
    connect(done, &QPushButton::clicked, this, [this] { complete(); });
    connect(viewer_, &ImageViewer::navigateRequested, this, &RevisaoPage::navigate);
    connect(viewer_, &ImageViewer::completeRequested, this, [this] { complete(); });
    connect(viewer_, &ImageViewer::decisionChanged, this, [this](int index, const QString& decision) {
        ctx_.db.setDecision(reviewId(), current_, ctx_.modelId(), index, decision);
        markSaved();
    });
    connect(viewer_, &ImageViewer::lesionAdded, this, [this](int k, const QPolygonF& contour) {
        const int id = ctx_.db.addLesion(reviewId(), contour);
        viewer_->setAddedId(k, id);
        ctx_.db.logEvent(ctx_.reviewer, "lesao_adicionada", current_);
        markSaved();
    });
    connect(viewer_, &ImageViewer::lesionRemoved, this, [this](int lesionId) {
        ctx_.db.removeLesion(reviewId(), lesionId);
        markSaved();
    });
    connect(observations_, &QLineEdit::textEdited, observationsTimer_, qOverload<>(&QTimer::start));
    connect(observationsTimer_, &QTimer::timeout, this, [this] {
        if (current_.isEmpty() || (!reviewId_ && observations_->text().isEmpty())) {
            return;
        }
        ctx_.db.setObservations(reviewId(), observations_->text());
        saved_->setText("Salvo às " + QTime::currentTime().toString("HH:mm:ss"));
    });
    connect(inadequate_, &QCheckBox::toggled, this, [this](bool on) {
        if (loading_ || current_.isEmpty() || (!reviewId_ && !on)) {
            return;
        }
        ctx_.db.setInadequate(reviewId(), on);
        markSaved();
    });
    connect(&ctx_, &Context::changed, this, &RevisaoPage::reloadList);
    connect(&ctx_, &Context::reviewerChanged, this, [this] {
        current_.clear();
        reloadList();
    });
    connect(&ctx_, &Context::reviewRequested, this, [this](const QString& id) {
        filter_->button(0)->setChecked(true);
        reloadList();
        open(id);
    });
    reloadList();
}

QVector<ReviewItem> RevisaoPage::filtered() const {
    QVector<ReviewItem> out;
    const int f = filter_->checkedId();
    for (const auto& item : items_) {
        if (f == 0 || (f == 1 && item.status != "concluida") || (f == 2 && item.status == "concluida")) {
            out.push_back(item);
        }
    }
    return out;
}

void RevisaoPage::reloadList() {
    if (ctx_.reviewer.isEmpty() || !ctx_.model) {
        list_->clear();
        viewer_->clear("Defina o nome do revisor em Configurações.");
        return;
    }
    items_ = ctx_.db.reviewList(ctx_.reviewer, ctx_.modelId());
    int done = 0, sample = 0, sampleDone = 0;
    for (const auto& i : items_) {
        done += i.status == "concluida";
        sample += i.group == 1;
        sampleDone += i.group == 1 && i.status == "concluida";
    }
    sampleLabel_->setText(QStringLiteral("Amostra aleatória: %1 de %2").arg(sampleDone).arg(sample));
    sampleBar_->setRange(0, std::max(1, sample));
    sampleBar_->setValue(sampleDone);
    allLabel_->setText(QStringLiteral("Todas: %1 de %2 concluídas").arg(done).arg(items_.size()));

    const int scroll = list_->verticalScrollBar() ? list_->verticalScrollBar()->value() : 0;
    list_->clear();
    int group = 0;
    for (const auto& i : filtered()) {
        if (i.group != group) {
            group = i.group;
            auto* header = new QListWidgetItem(kGroups[group]);
            header->setFlags(Qt::NoItemFlags);
            header->setForeground(QColor("#1d4f86"));
            QFont bold = header->font();
            bold.setBold(true);
            header->setFont(bold);
            header->setBackground(QColor("#eef1f5"));
            list_->addItem(header);
        }
        const QString dot = i.status == "concluida" ? "🟢" : i.status == "parcial" ? "🟡" : "⚪";
        auto* item = new QListWidgetItem(QStringLiteral("%1  %2   %3").arg(dot, i.id).arg(i.detections) +
                                         "   " + i.file.left(22));
        item->setToolTip(i.file);
        item->setData(Qt::UserRole, i.id);
        if (i.id == current_) {
            item->setBackground(QColor("#d5e2f1"));
        }
        list_->addItem(item);
    }
    list_->verticalScrollBar()->setValue(scroll);

    if (current_.isEmpty() && !items_.isEmpty()) {
        auto pending = std::find_if(items_.begin(), items_.end(), [](const ReviewItem& i) { return i.status != "concluida"; });
        open(pending != items_.end() ? pending->id : items_.front().id);
    }
}

void RevisaoPage::open(const QString& imageId) {
    if (ctx_.reviewer.isEmpty() || !ctx_.model) {
        return;
    }
    observationsTimer_->stop();
    current_ = imageId;
    const ImageData d = ctx_.db.imageData(imageId, ctx_.modelId(), ctx_.reviewer);
    reviewId_ = d.reviewId;
    const QImage image = viewerImage(QDir(ctx_.db.dataDir()).filePath("cache"), d.id, d.path);
    const bool blind = !d.reviewId && ctx_.db.config("modo_cego_inicial", "FALSE") == "TRUE";
    viewer_->load(d, image, QStringLiteral("%1 · %2 detecção(ões)").arg(d.id).arg(d.detections.size()), blind);
    loading_ = true;
    observations_->setText(d.observations);
    inadequate_->setChecked(d.inadequate);
    loading_ = false;
    saved_->setText(d.reviewId ? "Salvo em " + QDateTime::fromString(d.updatedAt, "yyyy-MM-dd HH:mm:ss").toString("dd/MM/yyyy HH:mm")
                               : QString());
    for (int r = 0; r < list_->count(); ++r) {
        auto* item = list_->item(r);
        const bool selected = item->data(Qt::UserRole).toString() == imageId;
        if (!item->data(Qt::UserRole).toString().isEmpty()) {
            item->setBackground(selected ? QColor("#d5e2f1") : QColor(Qt::transparent));
        }
        if (selected) {
            list_->scrollToItem(item);
        }
    }
}

int RevisaoPage::reviewId() {
    if (!reviewId_) {
        reviewId_ = ctx_.db.obtainReview(current_, ctx_.modelId(), ctx_.reviewer);
    }
    return *reviewId_;
}

void RevisaoPage::markSaved() {
    saved_->setText("Salvo às " + QTime::currentTime().toString("HH:mm:ss"));
    ctx_.dataChanged();
}

void RevisaoPage::navigate(int step) {
    const auto l = filtered();
    if (l.isEmpty()) {
        return;
    }
    int i = 0;
    for (int k = 0; k < l.size(); ++k) {
        if (l[k].id == current_) {
            i = k;
        }
    }
    open(l[std::clamp(i + step, 0, static_cast<int>(l.size()) - 1)].id);
}

void RevisaoPage::complete(bool confirmed) {
    if (current_.isEmpty()) {
        return;
    }
    if (observationsTimer_->isActive()) {
        observationsTimer_->stop();
        ctx_.db.setObservations(reviewId(), observations_->text());
    }
    const ImageData d = ctx_.db.imageData(current_, ctx_.modelId(), ctx_.reviewer);
    const int missing = static_cast<int>(d.detections.size() - d.decisions.size());
    if (missing > 0 && !inadequate_->isChecked() && !confirmed) {
        const auto answer = QMessageBox::question(
            this, "Detecções sem decisão",
            QStringLiteral("%1 detecção(ões) desta imagem ainda não têm decisão. Concluir mesmo assim?").arg(missing),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
    }
    ctx_.db.setCompleted(reviewId(), true);
    ctx_.db.logEvent(ctx_.reviewer, "revisao_concluida", current_);
    const QString finished = current_;
    ctx_.dataChanged();
    auto next = std::find_if(items_.begin(), items_.end(),
                             [&](const ReviewItem& i) { return i.status != "concluida" && i.id != finished; });
    if (next != items_.end()) {
        open(next->id);
    } else {
        QMessageBox::information(this, "Revisão", "Todas as imagens foram revisadas. Obrigado!");
    }
}

}  // namespace odonto
