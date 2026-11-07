#pragma once

#include "ml_odonto/detection.hpp"
#include "ml_odonto/image.hpp"

#include <filesystem>
#include <vector>

namespace ml_odonto {

// Radiografia em PNG, JPEG, BMP, TGA ou PGM, convertida para cinza de 8 bits
// (PNG de 16 bits é reduzido para 8). TIFF não é lido: converta antes para PNG.
[[nodiscard]] GrayImage read_image(const std::filesystem::path& path);
void write_png(const std::filesystem::path& path, const GrayImage& image);
[[nodiscard]] GrayImage read_pgm(const std::filesystem::path& path);
void write_pgm(const std::filesystem::path& path, const GrayImage& image);
[[nodiscard]] FloatImage read_pfm(const std::filesystem::path& path);
void write_pfm(const std::filesystem::path& path, const FloatImage& image);
void write_detections_json(const std::filesystem::path& path,
                           const std::vector<Detection>& detections,
                           const std::vector<Component>& components,
                           float threshold);
// Mesmo contrato, com o tamanho da imagem original e da escala de trabalho (comando run).
void write_detections_json(const std::filesystem::path& path,
                           const std::vector<Detection>& detections,
                           const std::vector<Component>& components, float threshold,
                           std::size_t original_height, std::size_t original_width,
                           double seconds);

}  // namespace ml_odonto
