#include "Contours.h"

#include <QDir>
#include <QFileInfo>
#include <QImageReader>

#include <algorithm>
#include <array>
#include <cmath>

namespace odonto {
namespace {

// Vizinhos em sentido horário a partir do oeste: (dy, dx)
constexpr std::array<std::array<int, 2>, 8> kDirections{{{0, -1}, {-1, -1}, {-1, 0}, {-1, 1}, {0, 1}, {1, 1}, {1, 0}, {1, -1}}};

int directionOf(int dy, int dx) {
    for (int i = 0; i < 8; ++i) {
        if (kDirections[i][0] == dy && kDirections[i][1] == dx) {
            return i;
        }
    }
    return 0;
}

}  // namespace

QVector<QPolygon> traceContours(const ml_odonto::LabelImage& labels, int count) {
    const int h = static_cast<int>(labels.height());
    const int w = static_cast<int>(labels.width());
    QVector<QPolygon> contours(count);
    QVector<bool> started(count + 1, false);
    auto inside = [&](int y, int x, int label) {
        return y >= 0 && x >= 0 && y < h && x < w && labels(static_cast<std::size_t>(y), static_cast<std::size_t>(x)) == label;
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int label = labels(static_cast<std::size_t>(y), static_cast<std::size_t>(x));
            if (label == 0 || started[label]) {
                continue;
            }
            started[label] = true;
            // traçado de Moore a partir do primeiro pixel na varredura (o vizinho a oeste é fundo)
            QPolygon contour;
            int cy = y, cx = x, from = 0;  // direção do pixel de fundo de onde viemos
            int firstMove = -1;
            for (int guard = 0; guard < 4 * h * w + 8; ++guard) {
                contour << QPoint(cx, cy);
                int move = -1;
                for (int k = 1; k <= 8; ++k) {
                    const int d = (from + k) % 8;
                    if (inside(cy + kDirections[d][0], cx + kDirections[d][1], label)) {
                        move = d;
                        break;
                    }
                }
                if (move < 0) {
                    break;  // pixel isolado
                }
                if (cy == y && cx == x && firstMove >= 0 && move == firstMove && contour.size() > 1) {
                    contour.removeLast();
                    break;  // critério de Jacob: voltou ao início pelo mesmo caminho
                }
                if (firstMove < 0) {
                    firstMove = move;
                }
                const int prev = (move + 7) % 8;  // último vizinho de fundo examinado
                const int ny = cy + kDirections[move][0];
                const int nx = cx + kDirections[move][1];
                from = directionOf(cy + kDirections[prev][0] - ny, cx + kDirections[prev][1] - nx);
                cy = ny;
                cx = nx;
            }
            contours[label - 1] = contour;
        }
    }
    return contours;
}

QVector<MaskLesion> lesionsFromMask(const ml_odonto::Mask& mask, const std::vector<ml_odonto::Detection>& detections,
                                    float threshold, double factor) {
    int count = 0;
    const auto labels = ml_odonto::connected_components8(mask, &count);
    QVector<MaskLesion> lesions;
    if (count == 0) {
        return lesions;
    }
    // os rótulos já seguem a ordem de varredura por linhas
    QVector<double> sumX(count + 1, 0), sumY(count + 1, 0), area(count + 1, 0), conf(count + 1, 0);
    for (std::size_t y = 0; y < labels.height(); ++y) {
        for (std::size_t x = 0; x < labels.width(); ++x) {
            const int l = labels(y, x);
            if (l > 0) {
                sumX[l] += static_cast<double>(x);
                sumY[l] += static_cast<double>(y);
                area[l] += 1;
            }
        }
    }
    for (const auto& d : detections) {
        if (d.confidence >= threshold) {
            const int l = labels(d.peak_y, d.peak_x);
            if (l > 0) {
                conf[l] = std::max(conf[l], static_cast<double>(d.confidence));
            }
        }
    }
    const auto contours = traceContours(labels, count);
    for (int l = 1; l <= count; ++l) {
        QPolygon c = contours[l - 1];
        if (c.size() > 6) {  // como no R: um ponto a cada dois
            QPolygon thin;
            for (int i = 0; i < c.size(); i += 2) {
                thin << c[i];
            }
            c = thin;
        }
        MaskLesion lesion;
        lesion.index = l;
        for (const auto& p : c) {
            lesion.contour << QPointF(std::round(p.x() * factor), std::round(p.y() * factor));
        }
        lesion.center = {sumX[l] / area[l] * factor, sumY[l] / area[l] * factor};
        lesion.area = area[l] * factor * factor;
        lesion.confidence = conf[l];
        lesions.push_back(lesion);
    }
    return lesions;
}

QImage readRadiograph(const QString& path, QString* error) {
    QImageReader reader(path);
    reader.setAutoTransform(false);
    reader.setAllocationLimit(1024);  // MB: panorâmicas grandes
    QImage image = reader.read();
    if (image.isNull()) {
        if (error != nullptr) {
            *error = reader.errorString();
        }
        return {};
    }
    return image.convertToFormat(QImage::Format_Grayscale8);
}

QImage viewerImage(const QString& cacheDir, const QString& imageId, const QString& path) {
    QDir().mkpath(cacheDir);
    const QString cached = QDir(cacheDir).filePath(imageId + ".jpg");
    if (QFileInfo::exists(cached)) {
        QImage image(cached);
        if (!image.isNull()) {
            return image;
        }
    }
    QImage image = readRadiograph(path);
    if (image.isNull()) {
        return {};
    }
    if (image.width() > 2000) {
        image = image.scaledToWidth(2000, Qt::SmoothTransformation);
    }
    image.save(cached, "JPEG", 92);
    return image;
}

}  // namespace odonto
