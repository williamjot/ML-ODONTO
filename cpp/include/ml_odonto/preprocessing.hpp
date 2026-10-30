#pragma once

#include "ml_odonto/image.hpp"

#include <cstddef>

namespace ml_odonto {

struct PreprocessingOptions {
    std::size_t target_width{2000};
    double clahe_clip_limit{2.0};
    std::size_t clahe_grid{8};
};

[[nodiscard]] GrayImage resize_area(const GrayImage& input, std::size_t output_height,
                                    std::size_t output_width);
[[nodiscard]] GrayImage resize_to_width(const GrayImage& input, std::size_t target_width);
[[nodiscard]] GrayImage clahe(const GrayImage& input, double clip_limit = 2.0,
                              std::size_t grid = 8);
[[nodiscard]] GrayImage preprocess(const GrayImage& input,
                                   const PreprocessingOptions& options = {});

}  // namespace ml_odonto
