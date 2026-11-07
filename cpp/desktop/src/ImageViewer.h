#pragma once

// Visor de radiografias (porte de inst/app/www/visor.js): zoom, arraste, contraste e brilho, contornos
// numerados por decisão, desenho de lesões não detectadas, modo cego e os mesmos atalhos de teclado.
// As coordenadas trocadas com o banco estão sempre em pixels da imagem ORIGINAL.

#include "Database.h"

#include <QImage>
#include <QWidget>

class QLabel;
class QPushButton;
class QSlider;
class QVBoxLayout;

namespace odonto {

class ViewerCanvas;

class ImageViewer : public QWidget {
    Q_OBJECT
public:
    explicit ImageViewer(QWidget* parent = nullptr);

    // `image` é a cópia reduzida para exibição; `data` traz as dimensões originais e as marcações
    void load(const ImageData& data, const QImage& image, const QString& title, bool blind);
    void clear(const QString& message);
    void setAddedId(int k, int lesionId);

signals:
    void decisionChanged(int index, const QString& decision);  // decisão vazia = desfeita
    void lesionAdded(int k, const QPolygonF& contour);
    void lesionRemoved(int lesionId);
    void completeRequested();
    void navigateRequested(int step);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    friend class ViewerCanvas;
    void decide(const QString& decision);
    void select(int index);
    void rebuildPanel();
    void setDrawing(bool on);
    void setBlind(bool on);
    void finishStroke(const QPolygonF& imagePoints);
    void removeAdded(int k);
    [[nodiscard]] QColor colorFor(int detectionIndex) const;

    ImageData data_;
    int selected_{0};
    bool blind_{false};
    bool drawing_{false};

    ViewerCanvas* canvas_{nullptr};
    QLabel* title_{nullptr};
    QLabel* hint_{nullptr};
    QSlider* contrast_{nullptr};
    QSlider* brightness_{nullptr};
    QPushButton* blindButton_{nullptr};
    QPushButton* drawButton_{nullptr};
    QWidget* detectionsBox_{nullptr};
    QVBoxLayout* detectionsLayout_{nullptr};
    QLabel* noDetections_{nullptr};
    QWidget* addedBox_{nullptr};
    QVBoxLayout* addedLayout_{nullptr};
};

}  // namespace odonto
