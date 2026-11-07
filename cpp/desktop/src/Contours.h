#pragma once

// Lesões de uma máscara (porte de lesoes_da_mascara() em fct_aplicar.R) e a cópia reduzida da
// radiografia para o visor (imagem_visor() em fct_imagem.R).

#include "ml_odonto/detection.hpp"

#include <QImage>
#include <QPolygonF>
#include <QString>
#include <QVector>

#include <vector>

namespace odonto {

struct MaskLesion {
    int index{0};          // 1, 2, ... na ordem de varredura por linhas (como o OpenCV)
    QPolygonF contour;     // pixels da imagem original
    QPointF center;
    double area{0};
    double confidence{0};
};

// Contorno externo (Moore) de cada componente com vizinhança 8, em coordenadas da máscara
QVector<QPolygon> traceContours(const ml_odonto::LabelImage& labels, int count);

// Componentes da máscara na escala de trabalho, convertidos para a imagem original por `factor`
QVector<MaskLesion> lesionsFromMask(const ml_odonto::Mask& mask, const std::vector<ml_odonto::Detection>& detections,
                                    float threshold, double factor);

// Radiografia em cinza de 8 bits; TIFF e demais formatos que o Qt lê
QImage readRadiograph(const QString& path, QString* error = nullptr);

// Cópia reduzida (2.000 px, JPEG) em <cache>/<id>.jpg, criada na primeira vez; a mesma do app R
QImage viewerImage(const QString& cacheDir, const QString& imageId, const QString& path);

}  // namespace odonto
