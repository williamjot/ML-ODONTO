#include "ml_odonto/preprocessing.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace ml_odonto {
namespace {

unsigned char to_byte(double value) {
    return static_cast<unsigned char>(std::clamp(std::lround(value), 0L, 255L));
}

struct Weight {
    std::size_t index;
    double value;
};

using WeightTable = std::vector<std::vector<Weight>>;

WeightTable area_weights(std::size_t input_size, std::size_t output_size) {
    if (input_size == 0 || output_size == 0) {
        throw std::invalid_argument("resize dimensions must be positive");
    }
    const double scale = static_cast<double>(input_size) / static_cast<double>(output_size);
    WeightTable result(output_size);
    for (std::size_t output = 0; output < output_size; ++output) {
        const double begin = static_cast<double>(output) * scale;
        const double end = static_cast<double>(output + 1) * scale;
        const auto first = static_cast<std::size_t>(std::floor(begin));
        const auto last = std::min(input_size, static_cast<std::size_t>(std::ceil(end)));
        for (std::size_t input = first; input < last; ++input) {
            const double overlap = std::min(end, static_cast<double>(input + 1)) -
                                   std::max(begin, static_cast<double>(input));
            if (overlap > 0.0) {
                result[output].push_back({input, overlap / scale});
            }
        }
    }
    return result;
}

}  // namespace

GrayImage resize_area(const GrayImage& input, std::size_t output_height, std::size_t output_width) {
    if (input.empty()) {
        throw std::invalid_argument("cannot resize an empty image");
    }
    const auto vertical = area_weights(input.height(), output_height);
    const auto horizontal = area_weights(input.width(), output_width);

    Image<double> intermediate(output_height, input.width(), 0.0);
    for (std::size_t y = 0; y < output_height; ++y) {
        for (std::size_t x = 0; x < input.width(); ++x) {
            double value = 0.0;
            for (const auto& weight : vertical[y]) {
                value += weight.value * static_cast<double>(input(weight.index, x));
            }
            intermediate(y, x) = value;
        }
    }

    GrayImage result(output_height, output_width, 0);
    for (std::size_t y = 0; y < output_height; ++y) {
        for (std::size_t x = 0; x < output_width; ++x) {
            double value = 0.0;
            for (const auto& weight : horizontal[x]) {
                value += weight.value * intermediate(y, weight.index);
            }
            result(y, x) = to_byte(value);
        }
    }
    return result;
}

GrayImage resize_to_width(const GrayImage& input, std::size_t target_width) {
    if (input.empty() || target_width == 0) {
        throw std::invalid_argument("image and target width must be non-empty");
    }
    const auto target_height = static_cast<std::size_t>(std::lround(
        static_cast<double>(input.height()) * static_cast<double>(target_width) /
        static_cast<double>(input.width())));
    if (target_width > input.width()) {
        throw std::invalid_argument("the equivalence core currently supports area reduction only");
    }
    return resize_area(input, std::max<std::size_t>(1, target_height), target_width);
}

GrayImage clahe(const GrayImage& input, double clip_limit, std::size_t grid) {
    if (input.empty() || clip_limit <= 0.0 || grid == 0) {
        throw std::invalid_argument("invalid CLAHE input or options");
    }

    const std::size_t tile_height = (input.height() + grid - 1) / grid;
    const std::size_t tile_width = (input.width() + grid - 1) / grid;
    const std::size_t tile_area = tile_height * tile_width;
    const int clip = std::max(1, static_cast<int>(std::floor(
                                     clip_limit * static_cast<double>(tile_area) / 256.0)));

    std::vector<std::array<unsigned char, 256>> lookup(grid * grid);
    for (std::size_t tile_y = 0; tile_y < grid; ++tile_y) {
        for (std::size_t tile_x = 0; tile_x < grid; ++tile_x) {
            std::array<int, 256> histogram{};
            for (std::size_t local_y = 0; local_y < tile_height; ++local_y) {
                const auto source_y = reflect101(
                    static_cast<std::ptrdiff_t>(tile_y * tile_height + local_y), input.height());
                for (std::size_t local_x = 0; local_x < tile_width; ++local_x) {
                    const auto source_x = reflect101(
                        static_cast<std::ptrdiff_t>(tile_x * tile_width + local_x), input.width());
                    ++histogram[input(source_y, source_x)];
                }
            }

            int excess = 0;
            for (auto& count : histogram) {
                if (count > clip) {
                    excess += count - clip;
                    count = clip;
                }
            }
            const int redistribute = excess / 256;
            const int remainder = excess - redistribute * 256;
            for (auto& count : histogram) {
                count += redistribute;
            }
            if (remainder > 0) {
                const int step = std::max(256 / remainder, 1);
                for (int index = 0, added = 0; index < 256 && added < remainder;
                     index += step, ++added) {
                    ++histogram[static_cast<std::size_t>(index)];
                }
            }

            auto& lut = lookup[tile_y * grid + tile_x];
            int cumulative = 0;
            for (std::size_t value = 0; value < 256; ++value) {
                cumulative += histogram[value];
                lut[value] = to_byte(static_cast<double>(cumulative) * 255.0 /
                                     static_cast<double>(tile_area));
            }
        }
    }

    GrayImage result(input.height(), input.width(), 0);
    const auto maximum_tile = static_cast<std::ptrdiff_t>(grid - 1);
    for (std::size_t y = 0; y < input.height(); ++y) {
        const double fy = static_cast<double>(y) / static_cast<double>(tile_height) - 0.5;
        const auto raw_y1 = static_cast<std::ptrdiff_t>(std::floor(fy));
        const double ay = fy - std::floor(fy);
        const auto y1 = static_cast<std::size_t>(std::clamp(raw_y1, std::ptrdiff_t{0}, maximum_tile));
        const auto y2 = static_cast<std::size_t>(std::clamp(raw_y1 + 1, std::ptrdiff_t{0}, maximum_tile));
        for (std::size_t x = 0; x < input.width(); ++x) {
            const double fx = static_cast<double>(x) / static_cast<double>(tile_width) - 0.5;
            const auto raw_x1 = static_cast<std::ptrdiff_t>(std::floor(fx));
            const double ax = fx - std::floor(fx);
            const auto x1 = static_cast<std::size_t>(std::clamp(raw_x1, std::ptrdiff_t{0}, maximum_tile));
            const auto x2 = static_cast<std::size_t>(std::clamp(raw_x1 + 1, std::ptrdiff_t{0}, maximum_tile));
            const auto value = input(y, x);
            const double top = static_cast<double>(lookup[y1 * grid + x1][value]) * (1.0 - ax) +
                               static_cast<double>(lookup[y1 * grid + x2][value]) * ax;
            const double bottom = static_cast<double>(lookup[y2 * grid + x1][value]) * (1.0 - ax) +
                                  static_cast<double>(lookup[y2 * grid + x2][value]) * ax;
            result(y, x) = to_byte(top * (1.0 - ay) + bottom * ay);
        }
    }
    return result;
}

GrayImage preprocess(const GrayImage& input, const PreprocessingOptions& options) {
    return clahe(resize_to_width(input, options.target_width), options.clahe_clip_limit,
                 options.clahe_grid);
}

}  // namespace ml_odonto
