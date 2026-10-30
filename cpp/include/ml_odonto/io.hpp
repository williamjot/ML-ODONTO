#pragma once

#include "ml_odonto/detection.hpp"
#include "ml_odonto/image.hpp"

#include <filesystem>
#include <vector>

namespace ml_odonto {

[[nodiscard]] GrayImage read_pgm(const std::filesystem::path& path);
void write_pgm(const std::filesystem::path& path, const GrayImage& image);
[[nodiscard]] FloatImage read_pfm(const std::filesystem::path& path);
void write_pfm(const std::filesystem::path& path, const FloatImage& image);
void write_detections_json(const std::filesystem::path& path,
                           const std::vector<Detection>& detections,
                           const std::vector<Component>& components,
                           float threshold);

}  // namespace ml_odonto
