#include "ImageViewer.h"

#include "Metrics.h"
#include "Pane.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace odonto {

// ------------------------------------------------------------------ tela de desenho

class ViewerCanvas : public QWidget {
public:
    explicit ViewerCanvas(ImageViewer* owner) : QWidget(owner), owner_(owner) {
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setMinimumSize(300, 200);
        setAttribute(Qt::WA_OpaquePaintEvent);
    }

    void setImage(const QImage& image) {
        original_ = image.convertToFormat(QImage::Format_Grayscale8);
        applyLevels(100, 100);
        autoFit_ = true;
        fit();
    }
    void clearImage() {
        original_ = {};
        shown_ = {};
        update();
    }

    // Contraste e brilho como o filtro CSS do visor: contraste em torno de 50% e brilho multiplicativo
    void applyLevels(int contrast, int brightness) {
        if (original_.isNull()) {
            return;
        }
        uchar lut[256];
        for (int v = 0; v < 256; ++v) {
            double x = v / 255.0;
            x = (x - 0.5) * contrast / 100.0 + 0.5;
            x *= brightness / 100.0;
            lut[v] = static_cast<uchar>(std::clamp(std::lround(x * 255), 0L, 255L));
        }
        shown_ = original_.copy();
        for (int y = 0; y < shown_.height(); ++y) {
            uchar* row = shown_.scanLine(y);
            for (int x = 0; x < shown_.width(); ++x) {
                row[x] = lut[row[x]];
            }
        }
        update();
    }

    void fit() {
        if (original_.isNull()) {
            return;
        }
        const double s = std::min(width() / double(original_.width()), height() / double(original_.height())) * 0.97;
        scale_ = s;
        offset_ = QPointF((width() - original_.width() * s) / 2, (height() - original_.height() * s) / 2);
        update();
    }

    // fator da imagem exibida em relação à original
    [[nodiscard]] double k() const {
        return (original_.isNull() || owner_->data_.width == 0) ? 1.0 : double(original_.width()) / owner_->data_.width;
    }
    [[nodiscard]] QPointF toImage(QPointF widgetPoint) const { return ((widgetPoint - offset_) / scale_) / k(); }
    [[nodiscard]] QPainterPath path(const QPolygonF& contour) const {
        QPainterPath p;
        for (int i = 0; i < contour.size(); ++i) {
            const QPointF q = contour[i] * k();
            i == 0 ? p.moveTo(q) : p.lineTo(q);
        }
        p.closeSubpath();
        return p;
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor("#05080c"));
        if (shown_.isNull()) {
            painter.setPen(QColor("#8fa3b8"));
            painter.drawText(rect(), Qt::AlignCenter, message_);
            return;
        }
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.translate(offset_);
        painter.scale(scale_, scale_);
        painter.drawImage(QPointF(0, 0), shown_);
        if (owner_->blind_) {
            return;
        }
        const double lw = 2.2 / scale_;
        const auto& dets = owner_->data_.detections;
        for (int i = 0; i < dets.size(); ++i) {
            const QColor c = owner_->colorFor(dets[i].index);
            QPen pen(c, i == owner_->selected_ ? lw * 2 : lw);
            if (owner_->data_.decisions.value(dets[i].index) == "rejeitada") {
                pen.setDashPattern({6 / (lw * scale_), 5 / (lw * scale_)});
            }
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(path(dets[i].contour));
            QFont f("Segoe UI");
            f.setBold(true);
            f.setPixelSize(std::max(1, int(16 / scale_)));
            painter.setFont(f);
            painter.setPen(c);
            painter.drawText(dets[i].center * k() + QPointF(14 / scale_, -14 / scale_), QString::number(dets[i].index));
        }
        QColor fill(color::added);
        fill.setAlpha(0x33);
        painter.setPen(QPen(QColor(color::added), lw));
        painter.setBrush(fill);
        for (const auto& a : owner_->data_.added) {
            painter.drawPath(path(a.contour));
        }
        if (stroke_.size() > 1) {
            painter.setBrush(Qt::NoBrush);
            QPolygonF line;
            for (const auto& p : stroke_) {
                line << p * k();
            }
            painter.drawPolyline(line);
        }
    }

    void resizeEvent(QResizeEvent*) override {
        if (autoFit_) {
            fit();
        }
    }

    void wheelEvent(QWheelEvent* event) override {
        if (original_.isNull()) {
            return;
        }
        const QPointF p = event->position();
        const double f = std::exp(event->angleDelta().y() * 0.0015);
        offset_ = p - (p - offset_) * f;
        scale_ *= f;
        autoFit_ = false;
        update();
    }

    void mousePressEvent(QMouseEvent* event) override {
        setFocus();
        if (original_.isNull()) {
            return;
        }
        if (owner_->drawing_) {
            stroke_ = {toImage(event->position())};
            return;
        }
        if (!owner_->blind_) {
            const QPointF q = toImage(event->position()) * k();
            const auto& dets = owner_->data_.detections;
            for (int i = 0; i < dets.size(); ++i) {
                if (path(dets[i].contour).contains(q)) {
                    owner_->select(i);
                    return;
                }
            }
        }
        dragStart_ = event->position();
        dragOffset_ = offset_;
        dragging_ = true;
        autoFit_ = false;
        setCursor(Qt::ClosedHandCursor);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (!stroke_.isEmpty()) {
            stroke_ << toImage(event->position());
            update();
        } else if (dragging_) {
            offset_ = dragOffset_ + (event->position() - dragStart_);
            update();
        }
    }

    // Tab troca de detecção, não de controle
    bool focusNextPrevChild(bool) override { return false; }

    void mouseReleaseEvent(QMouseEvent*) override {
        if (!stroke_.isEmpty()) {
            const auto points = stroke_;
            stroke_.clear();
            owner_->finishStroke(points);
        }
        dragging_ = false;
        unsetCursor();
    }

public:
    QString message_{"Nenhuma imagem"};
    QPolygonF stroke_;

private:
    ImageViewer* owner_;
    QImage original_;
    QImage shown_;
    double scale_{1};
    QPointF offset_;
    bool autoFit_{true};
    bool dragging_{false};
    QPointF dragStart_;
    QPointF dragOffset_;
};

// ------------------------------------------------------------------ visor

namespace {
const char* kHint = "Roda do mouse: zoom · arrastar: mover · H ocultar marcações · 0 ajustar";
const char* kDrawHint = "Desenhe o contorno da lesão segurando o botão do mouse · Esc cancela";
}  // namespace

ImageViewer::ImageViewer(QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // palco: título, tela, ferramentas
    auto* stage = new QWidget;
    stage->setStyleSheet("background:#05080c; color:#d6e0ea;");
    auto* stageLayout = new QVBoxLayout(stage);
    stageLayout->setContentsMargins(0, 0, 0, 0);
    stageLayout->setSpacing(0);
    title_ = new QLabel("—");
    title_->setStyleSheet("color:#e6edf5; font-weight:600; padding:4px 8px; background:#0d1520;");
    canvas_ = new ViewerCanvas(this);
    auto* tools = new QWidget;
    tools->setStyleSheet("background:#0d1520; QLabel{color:#c3cfdb;}");
    auto* toolsLayout = new QHBoxLayout(tools);
    toolsLayout->setContentsMargins(8, 3, 8, 3);
    hint_ = new QLabel(kHint);
    hint_->setStyleSheet("color:#8fa3b8; font-size:8.5pt;");
    contrast_ = new QSlider(Qt::Horizontal);
    contrast_->setRange(60, 220);
    contrast_->setValue(100);
    contrast_->setFixedWidth(100);
    brightness_ = new QSlider(Qt::Horizontal);
    brightness_->setRange(60, 160);
    brightness_->setValue(100);
    brightness_->setFixedWidth(100);
    blindButton_ = new QPushButton("Ocultar marcações");
    auto* fitButton = new QPushButton("Ajustar");
    for (auto* b : {blindButton_, fitButton}) {
        b->setStyleSheet("QPushButton{background:transparent;color:#e6edf5;border:1px solid #5d6f82;padding:2px 8px;}"
                         "QPushButton:hover{background:#1c2a3a;}");
        b->setFocusPolicy(Qt::NoFocus);
    }
    auto label = [](const QString& t) {
        auto* l = new QLabel(t);
        l->setStyleSheet("color:#c3cfdb;");
        return l;
    };
    toolsLayout->addWidget(hint_, 1);
    toolsLayout->addWidget(label("Contraste"));
    toolsLayout->addWidget(contrast_);
    toolsLayout->addWidget(label("Brilho"));
    toolsLayout->addWidget(brightness_);
    toolsLayout->addWidget(blindButton_);
    toolsLayout->addWidget(fitButton);
    stageLayout->addWidget(title_);
    stageLayout->addWidget(canvas_, 1);
    stageLayout->addWidget(tools);

    // painel lateral: legenda, detecções, lesões adicionadas, atalhos
    auto* side = new QWidget;
    side->setFixedWidth(270);
    auto* sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(10, 8, 10, 8);
    auto* legend = new QLabel(QStringLiteral(
        "<span style='color:#8fa3b8'>■</span> a decidir &nbsp; <span style='color:%1'>■</span> confirmada &nbsp; "
        "<span style='color:%2'>■</span> rejeitada<br><span style='color:%3'>■</span> dúvida &nbsp; "
        "<span style='color:%4'>■</span> adicionada")
        .arg(color::confirmed, color::rejected, color::doubt, color::added));
    legend->setStyleSheet("font-size:8.5pt; color:#5f6b7a;");
    auto* h1 = new QLabel("<b>Detecções do modelo</b>");
    detectionsBox_ = new QWidget;
    detectionsLayout_ = new QVBoxLayout(detectionsBox_);
    detectionsLayout_->setContentsMargins(0, 0, 0, 0);
    detectionsLayout_->setSpacing(6);
    noDetections_ = new QLabel("Nenhuma detecção nesta imagem. Se houver lesão, marque abaixo.");
    noDetections_->setWordWrap(true);
    noDetections_->setStyleSheet("color:#5f6b7a; font-size:8.5pt;");
    auto* h2 = new QLabel("<b>Lesões não detectadas</b>");
    drawButton_ = new QPushButton("Marcar lesão não detectada  (M)");
    drawButton_->setCheckable(true);
    drawButton_->setFocusPolicy(Qt::NoFocus);
    addedBox_ = new QWidget;
    addedLayout_ = new QVBoxLayout(addedBox_);
    addedLayout_->setContentsMargins(0, 0, 0, 0);
    auto* keys = new QLabel("Na lesão selecionada: <b>1</b> confirma · <b>2</b> rejeita · <b>3</b> dúvida · "
                            "<b>Tab</b> próxima · <b>Enter</b> concluir · <b>←/→</b> imagem anterior/próxima");
    keys->setWordWrap(true);
    keys->setStyleSheet("color:#5f6b7a; font-size:8.5pt;");
    auto* scrollContent = new QWidget;
    auto* scrollLayout = new QVBoxLayout(scrollContent);
    scrollLayout->setContentsMargins(0, 0, 0, 0);
    scrollLayout->addWidget(h1);
    scrollLayout->addWidget(detectionsBox_);
    scrollLayout->addWidget(noDetections_);
    scrollLayout->addSpacing(10);
    scrollLayout->addWidget(h2);
    scrollLayout->addWidget(drawButton_);
    scrollLayout->addWidget(addedBox_);
    scrollLayout->addStretch(1);
    auto* scroll = new QScrollArea;
    scroll->setWidget(scrollContent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    sideLayout->addWidget(legend);
    sideLayout->addWidget(scroll, 1);
    sideLayout->addWidget(keys);

    layout->addWidget(stage, 1);
    layout->addWidget(side);

    connect(contrast_, &QSlider::valueChanged, this, [this] { canvas_->applyLevels(contrast_->value(), brightness_->value()); });
    connect(brightness_, &QSlider::valueChanged, this, [this] { canvas_->applyLevels(contrast_->value(), brightness_->value()); });
    connect(blindButton_, &QPushButton::clicked, this, [this] { setBlind(!blind_); });
    connect(fitButton, &QPushButton::clicked, this, [this] { canvas_->fit(); });
    connect(drawButton_, &QPushButton::clicked, this, [this](bool on) { setDrawing(on); });
}

void ImageViewer::load(const ImageData& data, const QImage& image, const QString& title, bool blind) {
    data_ = data;
    selected_ = 0;
    canvas_->stroke_.clear();
    setDrawing(false);
    title_->setText(title);
    contrast_->blockSignals(true);
    brightness_->blockSignals(true);
    contrast_->setValue(100);
    brightness_->setValue(100);
    contrast_->blockSignals(false);
    brightness_->blockSignals(false);
    if (image.isNull()) {
        canvas_->message_ = "Não foi possível abrir a radiografia:\n" + data.path;
        canvas_->clearImage();
    } else {
        canvas_->setImage(image);
    }
    blind_ = blind;
    blindButton_->setText(blind_ ? "Mostrar marcações" : "Ocultar marcações");
    rebuildPanel();
    canvas_->setFocus();
}

void ImageViewer::clear(const QString& message) {
    data_ = {};
    canvas_->message_ = message;
    canvas_->clearImage();
    title_->setText("—");
    rebuildPanel();
}

void ImageViewer::setAddedId(int k, int lesionId) {
    if (k >= 0 && k < data_.added.size()) {
        data_.added[k].id = lesionId;
    }
}

QColor ImageViewer::colorFor(int detectionIndex) const {
    const QString d = data_.decisions.value(detectionIndex);
    if (d == "confirmada") return QColor(color::confirmed);
    if (d == "rejeitada") return QColor(color::rejected);
    if (d == "duvida") return QColor(color::doubt);
    return Qt::white;
}

void ImageViewer::rebuildPanel() {
    while (auto* item = detectionsLayout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    while (auto* item = addedLayout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    noDetections_->setVisible(data_.detections.isEmpty());
    for (int i = 0; i < data_.detections.size(); ++i) {
        const auto& det = data_.detections[i];
        const QString decision = data_.decisions.value(det.index);
        auto* card = new QFrame;
        card->setObjectName("detCard");
        card->setStyleSheet(i == selected_ ? "#detCard{border:2px solid #2b6cb0; border-radius:4px; background:#f3f7fc;}"
                                           : "#detCard{border:1px solid #dde3ea; border-radius:4px;}");
        auto* v = new QVBoxLayout(card);
        v->setContentsMargins(8, 6, 8, 6);
        auto* head = new QHBoxLayout;
        auto* name = new QLabel(QStringLiteral("<b>Lesão %1</b>").arg(det.index));
        auto* conf = new QLabel("confiança " + (det.confidence ? decimal(*det.confidence) : QStringLiteral("—")));
        conf->setStyleSheet("color:#5f6b7a; font-size:8.5pt;");
        head->addWidget(name);
        head->addStretch(1);
        head->addWidget(conf);
        v->addLayout(head);
        auto* buttons = new QHBoxLayout;
        buttons->setSpacing(3);
        struct Choice { const char* text; const char* value; const char* color; };
        for (const Choice& c : {Choice{"✓ Confirma", "confirmada", color::confirmed}, Choice{"✗ Rejeita", "rejeitada", color::rejected},
                                Choice{"? Dúvida", "duvida", color::doubt}}) {
            auto* b = new QPushButton(c.text);
            b->setFocusPolicy(Qt::NoFocus);
            const bool active = decision == c.value;
            b->setStyleSheet(QStringLiteral("QPushButton{padding:2px 4px; border:1px solid %1; color:%2; background:%3;}")
                                 .arg(c.color, active ? "white" : c.color, active ? c.color : "white"));
            const QString value = c.value;
            connect(b, &QPushButton::clicked, this, [this, i, value] {
                select(i);
                decide(value);
            });
            buttons->addWidget(b);
        }
        v->addLayout(buttons);
        card->installEventFilter(this);
        card->setProperty("index", i);
        card->setCursor(Qt::PointingHandCursor);
        detectionsLayout_->addWidget(card);
    }
    for (int k = 0; k < data_.added.size(); ++k) {
        auto* row = new QWidget;
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        auto* l = new QLabel(QStringLiteral("<span style='color:%1'>●</span> Adicionada %2").arg(color::added).arg(k + 1));
        auto* remove = new QPushButton("remover");
        remove->setFlat(true);
        remove->setStyleSheet("QPushButton{color:#b42318; border:0; text-decoration:underline;}");
        remove->setFocusPolicy(Qt::NoFocus);
        connect(remove, &QPushButton::clicked, this, [this, k] { removeAdded(k); });
        h->addWidget(l);
        h->addStretch(1);
        h->addWidget(remove);
        addedLayout_->addWidget(row);
    }
    canvas_->update();
}

void ImageViewer::select(int index) {
    if (index < 0 || index >= data_.detections.size()) {
        return;
    }
    selected_ = index;
    rebuildPanel();
}

void ImageViewer::decide(const QString& decision) {
    if (selected_ < 0 || selected_ >= data_.detections.size()) {
        return;
    }
    const int index = data_.detections[selected_].index;
    const QString next = data_.decisions.value(index) == decision ? QString() : decision;
    if (next.isEmpty()) {
        data_.decisions.remove(index);
    } else {
        data_.decisions[index] = next;
    }
    rebuildPanel();
    emit decisionChanged(index, next);
}

void ImageViewer::setDrawing(bool on) {
    drawing_ = on;
    drawButton_->setChecked(on);
    hint_->setText(on ? kDrawHint : kHint);
    canvas_->setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
}

void ImageViewer::setBlind(bool on) {
    blind_ = on;
    blindButton_->setText(blind_ ? "Mostrar marcações" : "Ocultar marcações");
    canvas_->update();
}

void ImageViewer::finishStroke(const QPolygonF& imagePoints) {
    if (imagePoints.size() >= 5) {
        QPolygonF rounded;
        for (const auto& p : imagePoints) {
            rounded << QPointF(std::round(p.x()), std::round(p.y()));
        }
        data_.added.push_back({0, rounded});
        emit lesionAdded(static_cast<int>(data_.added.size()) - 1, rounded);
    }
    setDrawing(false);
    rebuildPanel();
}

void ImageViewer::removeAdded(int k) {
    if (k < 0 || k >= data_.added.size()) {
        return;
    }
    const int id = data_.added[k].id;
    data_.added.removeAt(k);
    rebuildPanel();
    if (id > 0) {
        emit lesionRemoved(id);
    }
}

bool ImageViewer::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress) {
        const QVariant index = watched->property("index");
        if (index.isValid()) {
            select(index.toInt());
            canvas_->setFocus();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ImageViewer::keyPressEvent(QKeyEvent* event) {
    if (data_.id.isEmpty()) {
        QWidget::keyPressEvent(event);
        return;
    }
    const int n = static_cast<int>(data_.detections.size());
    switch (event->key()) {
    case Qt::Key_1: decide("confirmada"); break;
    case Qt::Key_2: decide("rejeitada"); break;
    case Qt::Key_3: decide("duvida"); break;
    case Qt::Key_Tab:
        if (n) select((selected_ + 1) % n);
        break;
    case Qt::Key_Backtab:
        if (n) select((selected_ + n - 1) % n);
        break;
    case Qt::Key_H: setBlind(!blind_); break;
    case Qt::Key_0: canvas_->fit(); break;
    case Qt::Key_M: setDrawing(!drawing_); break;
    case Qt::Key_Escape:
        canvas_->stroke_.clear();
        setDrawing(false);
        canvas_->update();
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter: emit completeRequested(); break;
    case Qt::Key_Right: emit navigateRequested(1); break;
    case Qt::Key_Left: emit navigateRequested(-1); break;
    default: QWidget::keyPressEvent(event); return;
    }
    event->accept();
}

}  // namespace odonto
