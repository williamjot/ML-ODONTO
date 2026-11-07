// Auditoria das lesões de referência que o modelo não encontrou (mod_auditoria.R e cartao_auditoria()).

#include "Contours.h"
#include "Metrics.h"
#include "Pane.h"
#include "pages/Pages.h"

#include "ml_odonto/detection.hpp"

#include <QButtonGroup>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QProgressBar>
#include <QRadioButton>
#include <QScrollArea>
#include <QApplication>
#include <QVBoxLayout>

#include <array>
#include <cmath>

namespace odonto {
namespace {

struct Answer { const char* label; const char* value; };
constexpr std::array<Answer, 4> kAnswers{{{"Lesão visível na panorâmica", "visivel"},
                                          {"Duvidosa", "duvidosa"},
                                          {"Não visível na panorâmica", "nao_visivel"},
                                          {"Não é lesão / erro de marcação", "erro_marcacao"}}};

QColor viridis(double t) {
    static const std::array<QColor, 9> stops{QColor("#440154"), QColor("#472D7B"), QColor("#3B528B"), QColor("#2C728E"),
                                             QColor("#21918C"), QColor("#28AE80"), QColor("#5EC962"), QColor("#ADDC30"),
                                             QColor("#FDE725")};
    t = std::clamp(t, 0.0, 1.0) * (stops.size() - 1);
    const int i = std::min(static_cast<int>(t), static_cast<int>(stops.size()) - 2);
    const double f = t - i;
    const QColor& a = stops[i];
    const QColor& b = stops[i + 1];
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * f, a.greenF() + (b.greenF() - a.greenF()) * f,
                            a.blueF() + (b.blueF() - a.blueF()) * f);
}

ml_odonto::Mask toMask(const QImage& image) {
    const QImage g = image.convertToFormat(QImage::Format_Grayscale8);
    ml_odonto::Mask m(static_cast<std::size_t>(g.height()), static_cast<std::size_t>(g.width()), 0);
    for (int y = 0; y < g.height(); ++y) {
        const uchar* row = g.constScanLine(y);
        for (int x = 0; x < g.width(); ++x) {
            m(static_cast<std::size_t>(y), static_cast<std::size_t>(x)) = row[x] > 127 ? 1 : 0;
        }
    }
    return m;
}

// Recorte [x0,x1]x[y0,y1] de uma máscara, redimensionado (vizinho mais próximo) para w x h
QVector<bool> cropMask(const QVector<bool>& m, int W, int x0, int y0, int x1, int y1, int w, int h) {
    QVector<bool> out(w * h);
    for (int y = 0; y < h; ++y) {
        const int sy = y0 + std::min(y1 - y0, y * (y1 - y0 + 1) / h);
        for (int x = 0; x < w; ++x) {
            const int sx = x0 + std::min(x1 - x0, x * (x1 - x0 + 1) / w);
            out[y * w + x] = m[sy * W + sx];
        }
    }
    return out;
}

QVector<bool> dilateBox(const QVector<bool>& m, int W, int H, int r) {
    QVector<bool> horizontal(W * H, false), out(W * H, false);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (m[y * W + x]) {
                for (int dx = -r; dx <= r; ++dx) {
                    if (x + dx >= 0 && x + dx < W) horizontal[y * W + x + dx] = true;
                }
            }
        }
    }
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (horizontal[y * W + x]) {
                for (int dy = -r; dy <= r; ++dy) {
                    if (y + dy >= 0 && y + dy < H) out[(y + dy) * W + x] = true;
                }
            }
        }
    }
    return out;
}

QVector<bool> border(const QVector<bool>& m, int w, int h) {
    QVector<bool> out(w * h, false);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!m[y * w + x]) continue;
            bool edge = false;
            for (int dy = -1; dy <= 1 && !edge; ++dy) {
                for (int dx = -1; dx <= 1 && !edge; ++dx) {
                    const int yy = y + dy, xx = x + dx;
                    edge = yy < 0 || xx < 0 || yy >= h || xx >= w || !m[yy * w + xx];
                }
            }
            out[y * w + x] = edge;
        }
    }
    return out;
}

void paint(QImage& rgb, const QVector<bool>& mask, const QColor& c) {
    for (int y = 0; y < rgb.height(); ++y) {
        auto* row = reinterpret_cast<QRgb*>(rgb.scanLine(y));
        for (int x = 0; x < rgb.width(); ++x) {
            if (mask[y * rgb.width() + x]) row[x] = c.rgb();
        }
    }
}

// As quatro imagens do cartão (panorâmica, limpo, marcado, probabilidade), com cache
QStringList auditImages(Database& db, const MissedLesion& l, const QString& cacheDir) {
    QStringList names;
    for (const char* kind : {"pano", "limpo", "marcado", "prob"}) {
        names << QDir(cacheDir).filePath(QStringLiteral("aud_%1_%2.png").arg(l.id).arg(kind));
    }
    if (std::all_of(names.begin(), names.end(), [](const QString& f) { return QFileInfo::exists(f); })) {
        return names;
    }
    const auto ref = db.referenceImage(l.patient);
    if (!ref) return {};
    const QImage img = readRadiograph(ref->image);
    const QImage maskImg = readRadiograph(ref->mask);
    const QImage traceImg = readRadiograph(ref->trace);
    if (img.isNull() || maskImg.size() != img.size()) return {};
    const int W = img.width(), H = img.height();
    const double f = W / 1400.0;
    const int half = qRound(150 * f);
    const int x0 = std::max(0, qRound(l.center.x()) - half), x1 = std::min(W - 1, qRound(l.center.x()) + half);
    const int y0 = std::max(0, qRound(l.center.y()) - half), y1 = std::min(H - 1, qRound(l.center.y()) + half);

    // só a janela em volta da lesão (com margem para a dilatação): rotular a panorâmica inteira é lento
    const int m = 8;
    const int bx0 = std::max(0, x0 - m), by0 = std::max(0, y0 - m);
    const int bw = std::min(W - 1, x1 + m) - bx0 + 1, bh = std::min(H - 1, y1 + m) - by0 + 1;
    int count = 0;
    const auto labels = ml_odonto::connected_components8(toMask(maskImg.copy(bx0, by0, bw, bh)), &count);
    auto labelAt = [&](int x, int y) {
        x = std::clamp(x - bx0, 0, bw - 1);
        y = std::clamp(y - by0, 0, bh - 1);
        return labels(static_cast<std::size_t>(y), static_cast<std::size_t>(x));
    };
    QSet<int> target;
    if (const int c = labelAt(qRound(l.center.x()), qRound(l.center.y())); c > 0) {
        target.insert(c);
    } else {
        for (const auto& p : l.contour) {
            if (const int c2 = labelAt(qRound(p.x()), qRound(p.y())); c2 > 0) target.insert(c2);
        }
    }
    QVector<bool> alvo(bw * bh), trace(bw * bh);
    for (int y = 0; y < bh; ++y) {
        const uchar* t = traceImg.isNull() ? nullptr : traceImg.constScanLine(by0 + y);
        for (int x = 0; x < bw; ++x) {
            alvo[y * bw + x] = target.contains(labels(static_cast<std::size_t>(y), static_cast<std::size_t>(x)));
            trace[y * bw + x] = t != nullptr && t[bx0 + x] > 127;
        }
    }
    const int wc = 520, hc = qRound(wc * double(y1 - y0 + 1) / (x1 - x0 + 1));
    const QImage base = img.copy(x0, y0, x1 - x0 + 1, y1 - y0 + 1).scaled(wc, hc, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const QImage baseRgb = base.convertToFormat(QImage::Format_RGB32);
    baseRgb.save(names[1]);

    const auto near = dilateBox(alvo, bw, bh, 4);
    QVector<bool> thisTrace(bw * bh), others(bw * bh);
    for (int i = 0; i < bw * bh; ++i) {
        thisTrace[i] = trace[i] && near[i];
        others[i] = trace[i] && !near[i];
    }
    const int cx0 = x0 - bx0, cy0 = y0 - by0, cx1 = x1 - bx0, cy1 = y1 - by0;
    const auto edge = border(cropMask(alvo, bw, cx0, cy0, cx1, cy1, wc, hc), wc, hc);
    QImage marked = baseRgb;
    paint(marked, cropMask(others, bw, cx0, cy0, cx1, cy1, wc, hc), QColor::fromRgbF(.92, .92, .92));
    paint(marked, edge, QColor::fromRgbF(0, .78, 1));
    paint(marked, cropMask(thisTrace, bw, cx0, cy0, cx1, cy1, wc, hc), QColor::fromRgbF(1, .55, 0));
    marked.save(names[2]);

    QImage overlay = baseRgb;
    if (!ref->probability.isEmpty() && QFileInfo::exists(ref->probability)) {
        const QImage prob = readRadiograph(ref->probability);
        const double ps = prob.width() / double(W);
        const QImage pc = prob.copy(QRectF(x0 * ps, y0 * ps, (x1 - x0 + 1) * ps, (y1 - y0 + 1) * ps).toAlignedRect())
                              .scaled(wc, hc, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        for (int y = 0; y < hc; ++y) {
            auto* row = reinterpret_cast<QRgb*>(overlay.scanLine(y));
            const uchar* p = pc.constScanLine(y);
            const uchar* b = base.constScanLine(y);
            for (int x = 0; x < wc; ++x) {
                const QColor c = viridis(p[x] / 255.0);
                const double g = b[x] / 255.0;
                row[x] = QColor::fromRgbF(.55 * g + .45 * c.redF(), .55 * g + .45 * c.greenF(), .55 * g + .45 * c.blueF()).rgb();
            }
        }
    }
    paint(overlay, edge, Qt::white);
    overlay.save(names[3]);

    QImage pano = img.scaledToWidth(640, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
    QPainter painter(&pano);
    painter.setPen(QPen(QColor::fromRgbF(1, .55, 0), 3));
    const double s = 640.0 / W;
    painter.drawRect(QRectF(x0 * s, y0 * s, (x1 - x0) * s, (y1 - y0) * s));
    painter.end();
    pano.save(names[0]);
    return names;
}

}  // namespace

AuditoriaPage::AuditoriaPage(Context& ctx, QWidget* parent) : QWidget(parent), ctx_(ctx) {
    auto* side = new QWidget;
    auto* s = new QVBoxLayout(side);
    s->setContentsMargins(10, 8, 10, 8);
    model_ = new QComboBox;
    filter_ = new QButtonGroup(this);
    auto* filters = new QHBoxLayout;
    int id = 0;
    for (const char* name : {"Todas", "Sem resposta"}) {
        auto* r = new QRadioButton(name);
        filter_->addButton(r, id++);
        filters->addWidget(r);
    }
    filter_->button(0)->setChecked(true);
    summary_ = new QLabel;
    summary_->setWordWrap(true);
    bar_ = new QProgressBar;
    bar_->setTextVisible(false);
    bar_->setFixedHeight(6);
    auto* help = new QLabel(
        "Cada cartão é uma lesão marcada pelo especialista que o modelo não encontrou. Julgue se ela é visível "
        "<b>nesta panorâmica</b>, independentemente do que o modelo fez.<br><br>"
        "<span style='color:#ff8c00'>■</span> traço desta lesão · <span style='color:#00c7ff'>■</span> máscara usada na "
        "avaliação · branco: outras lesões marcadas");
    help->setWordWrap(true);
    help->setStyleSheet("color:#5f6b7a; font-size:8.5pt;");
    s->addWidget(new QLabel("Modelo avaliado"));
    s->addWidget(model_);
    s->addLayout(filters);
    s->addWidget(bar_);
    s->addWidget(summary_);
    s->addWidget(help);
    s->addStretch(1);
    auto* sidePane = new Pane("Auditoria", side);

    cards_ = new QWidget;
    cardsLayout_ = new QVBoxLayout(cards_);
    cardsLayout_->setContentsMargins(8, 8, 8, 8);
    cardsLayout_->setSpacing(8);
    auto* scroll = new QScrollArea;
    scroll->setWidget(cards_);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* cardsPane = new Pane("Lesões não encontradas", scroll);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(5, 5, 5, 5);
    auto* split = splitter(Qt::Horizontal, {sidePane, cardsPane}, {1, 4});
    split->setSizes({300, 1200});
    layout->addWidget(split);

    connect(model_, &QComboBox::currentTextChanged, this, [this] { reload(); });
    connect(filter_, &QButtonGroup::idClicked, this, [this] { rebuildCards(); });
    connect(&ctx_, &Context::reviewerChanged, this, [this] {
        if (loaded_) reload();
    });
}

void AuditoriaPage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (!loaded_) {
        loaded_ = true;
        reload();
    }
}

void AuditoriaPage::reload() {
    if (model_->count() == 0) {
        const auto available = ctx_.db.validationModels();
        model_->blockSignals(true);
        for (const auto& m : modelOrder()) {
            if (available.contains(m)) model_->addItem(m);
        }
        model_->setCurrentText("U-Net v2");
        model_->blockSignals(false);
    }
    missed_ = ctx_.reviewer.isEmpty() ? QVector<MissedLesion>{} : ctx_.db.missedLesions(model_->currentText(), ctx_.reviewer);
    updateSummary();
    rebuildCards();
}

void AuditoriaPage::updateSummary() {
    int answered = 0;
    QMap<QString, int> byAnswer;
    for (const auto& l : missed_) {
        if (!l.answer.isEmpty()) {
            ++answered;
            ++byAnswer[l.answer];
        }
    }
    QString html = QStringLiteral("<b>%1 de %2 respondidas</b><table width='100%'>").arg(answered).arg(missed_.size());
    for (const auto& a : kAnswers) {
        html += QStringLiteral("<tr><td>%1</td><td align='right'>%2</td></tr>").arg(a.label).arg(byAnswer.value(a.value));
    }
    summary_->setText(html + "</table>");
    bar_->setRange(0, std::max<int>(1, missed_.size()));
    bar_->setValue(answered);
}

void AuditoriaPage::rebuildCards() {
    while (auto* item = cardsLayout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const bool onlyPending = filter_->checkedId() == 1;
    const QString cache = QDir(ctx_.db.dataDir()).filePath("cache");
    int k = 0;
    for (int i = 0; i < missed_.size(); ++i) {
        const auto& l = missed_[i];
        if (onlyPending && !l.answer.isEmpty()) continue;
        ++k;  // numeração dentro do filtro, como no R
        auto* card = new QFrame;
        card->setObjectName("card");
        card->setStyleSheet(QStringLiteral("#card{background:#fff; border:%1;}")
                                .arg(l.answer.isEmpty() ? "1px solid #c9d1db" : "2px solid #2fb36b"));
        auto* v = new QVBoxLayout(card);
        QString info = QStringLiteral("<b>%1.</b> Paciente %2, lesão %3 · %4 · diâmetro %5 px · área %6 px")
                           .arg(k).arg(l.patient).arg(l.lesion)
                           .arg(l.arch == "superior" ? "maxila" : "mandíbula")
                           .arg(qRound(l.diameter))
                           .arg(QLocale(QLocale::Portuguese, QLocale::Brazil).toString(qRound(l.area)));
        if (!std::isnan(l.distanceToFound)) {
            info += QStringLiteral(" · a %1 px da lesão encontrada mais próxima").arg(qRound(l.distanceToFound));
        }
        QStringList alerts;
        if (!std::isnan(l.distanceToFound) && l.distanceToFound <= 40) alerts << "vizinha de uma lesão encontrada (pode ser a mesma lesão)";
        if (l.diameter <= 12) alerts << "muito pequena (pode ser resíduo do traço)";
        if (!alerts.isEmpty()) {
            info += QStringLiteral(" <span style='background:#E8C33A; padding:0 4px'>%1</span>").arg(alerts.join("; "));
        }
        auto* header = new QLabel(info);
        header->setWordWrap(true);
        v->addWidget(header);
        auto* figures = new QHBoxLayout;
        const auto images = auditImages(ctx_.db, l, cache);
        const char* captions[] = {"Panorâmica: posição", "Ampliado, sem marcação", "Traço e máscara",
                                  "Probabilidade do modelo (amarelo = alta)"};
        for (int f = 0; f < 4; ++f) {
            auto* box = new QVBoxLayout;
            auto* picture = new QLabel;
            picture->setMinimumSize(160, 110);
            picture->setAlignment(Qt::AlignCenter);
            if (f < images.size()) {
                picture->setPixmap(QPixmap(images[f]).scaledToWidth(260, Qt::SmoothTransformation));
            }
            picture->setStyleSheet("border:1px solid #dde3ea;");
            auto* caption = new QLabel(captions[f]);
            caption->setStyleSheet("color:#5f6b7a; font-size:8.5pt;");
            box->addWidget(picture);
            box->addWidget(caption);
            figures->addLayout(box);
        }
        v->addLayout(figures);
        auto* answers = new QHBoxLayout;
        auto* group = new QButtonGroup(card);
        for (const auto& a : kAnswers) {
            auto* r = new QRadioButton(a.label);
            r->setChecked(l.answer == a.value);
            group->addButton(r);
            answers->addWidget(r);
            const QString value = a.value;
            connect(r, &QRadioButton::clicked, this, [this, i, value, card] {
                ctx_.db.saveAudit(missed_[i].id, model_->currentText(), ctx_.reviewer, value, std::nullopt);
                ctx_.db.logEvent(ctx_.reviewer, "auditoria", QStringLiteral("referencia %1: %2").arg(missed_[i].id).arg(value));
                missed_[i].answer = value;
                card->setStyleSheet("#card{background:#fff; border:2px solid #2fb36b;}");
                updateSummary();
                ctx_.dataChanged();
            });
        }
        answers->addStretch(1);
        v->addLayout(answers);
        auto* note = new QLineEdit(l.observation);
        note->setPlaceholderText("Observação (opcional)");
        connect(note, &QLineEdit::editingFinished, this, [this, i, note] {
            if (note->text() == missed_[i].observation) return;
            ctx_.db.saveAudit(missed_[i].id, model_->currentText(), ctx_.reviewer, std::nullopt, note->text());
            missed_[i].observation = note->text();
            ctx_.dataChanged();
        });
        v->addWidget(note);
        cardsLayout_->addWidget(card);
    }
    if (cardsLayout_->count() == 0) {
        auto* empty = new QLabel(ctx_.reviewer.isEmpty() ? "Defina o nome do revisor em Configurações."
                                                         : "Nenhuma lesão neste filtro.");
        empty->setStyleSheet("color:#5f6b7a; padding:12px;");
        cardsLayout_->addWidget(empty);
    }
    cardsLayout_->addStretch(1);
}

}  // namespace odonto
