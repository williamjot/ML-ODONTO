#pragma once

#include "ml_odonto/image.hpp"

#include <cstddef>
#include <vector>

namespace ml_odonto {

struct DetectionOptions {
    std::size_t peak_radius{15};
    std::size_t region_radius{100};
    double gaussian_sigma{4.0};
    float minimum_peak{0.05F};
};

struct Detection {
    std::size_t peak_y{};
    std::size_t peak_x{};
    std::size_t y0{};
    std::size_t x0{};
    float confidence{};
    Mask region;
};

struct Component {
    int label{};
    std::size_t x{};
    std::size_t y{};
    std::size_t width{};
    std::size_t height{};
    std::size_t area{};
    double center_x{};
    double center_y{};
};

[[nodiscard]] FloatImage gaussian_blur(const FloatImage& input, double sigma = 4.0);
[[nodiscard]] FloatImage maximum_filter(const FloatImage& input, std::size_t radius = 15);
[[nodiscard]] LabelImage connected_components8(const Mask& input, int* component_count = nullptr);
[[nodiscard]] std::vector<Detection> detect(const FloatImage& probability,
                                            const DetectionOptions& options = {});
[[nodiscard]] Mask build_mask(const std::vector<Detection>& detections, float threshold,
                              std::size_t height, std::size_t width);
[[nodiscard]] std::vector<Component> describe_components(const Mask& mask);

}  // namespace ml_odonto
