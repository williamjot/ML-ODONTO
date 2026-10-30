#include "ml_odonto/detection.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <deque>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ml_odonto {
namespace {

std::vector<double> gaussian_kernel(double sigma) {
    if (sigma <= 0.0) {
        throw std::invalid_argument("Gaussian sigma must be positive");
    }
    const auto radius = static_cast<std::ptrdiff_t>(std::lround(sigma * 4.0));
    std::vector<double> kernel(static_cast<std::size_t>(2 * radius + 1));
    double total = 0.0;
    for (std::ptrdiff_t offset = -radius; offset <= radius; ++offset) {
        const double value = std::exp(-static_cast<double>(offset * offset) / (2.0 * sigma * sigma));
        kernel[static_cast<std::size_t>(offset + radius)] = value;
        total += value;
    }
    for (auto& value : kernel) {
        value /= total;
    }
    return kernel;
}

FloatImage convolve_vertical(const FloatImage& input, const std::vector<double>& kernel) {
    const auto radius = static_cast<std::ptrdiff_t>((kernel.size() - 1) / 2);
    FloatImage result(input.height(), input.width(), 0.0F);
    for (std::size_t y = 0; y < input.height(); ++y) {
        for (std::size_t x = 0; x < input.width(); ++x) {
            double value = 0.0;
            for (std::ptrdiff_t offset = -radius; offset <= radius; ++offset) {
                const auto source_y = reflect101(static_cast<std::ptrdiff_t>(y) + offset, input.height());
                value += kernel[static_cast<std::size_t>(offset + radius)] * input(source_y, x);
            }
            result(y, x) = static_cast<float>(value);
        }
    }
    return result;
}

FloatImage convolve_horizontal(const FloatImage& input, const std::vector<double>& kernel) {
    const auto radius = static_cast<std::ptrdiff_t>((kernel.size() - 1) / 2);
    FloatImage result(input.height(), input.width(), 0.0F);
    for (std::size_t y = 0; y < input.height(); ++y) {
        for (std::size_t x = 0; x < input.width(); ++x) {
            double value = 0.0;
            for (std::ptrdiff_t offset = -radius; offset <= radius; ++offset) {
                const auto source_x = reflect101(static_cast<std::ptrdiff_t>(x) + offset, input.width());
                value += kernel[static_cast<std::size_t>(offset + radius)] * input(y, source_x);
            }
            result(y, x) = static_cast<float>(value);
        }
    }
    return result;
}

}  // namespace

FloatImage gaussian_blur(const FloatImage& input, double sigma) {
    if (input.empty()) {
        throw std::invalid_argument("cannot blur an empty image");
    }
    const auto kernel = gaussian_kernel(sigma);
    return convolve_horizontal(convolve_vertical(input, kernel), kernel);
}

FloatImage maximum_filter(const FloatImage& input, std::size_t radius) {
    if (input.empty()) {
        throw std::invalid_argument("cannot filter an empty image");
    }
    FloatImage horizontal(input.height(), input.width(), 0.0F);
    for (std::size_t y = 0; y < input.height(); ++y) {
        for (std::size_t x = 0; x < input.width(); ++x) {
            float maximum = -std::numeric_limits<float>::infinity();
            for (std::ptrdiff_t offset = -static_cast<std::ptrdiff_t>(radius);
                 offset <= static_cast<std::ptrdiff_t>(radius); ++offset) {
                const auto source_x = reflect(static_cast<std::ptrdiff_t>(x) + offset, input.width());
                maximum = std::max(maximum, input(y, source_x));
            }
            horizontal(y, x) = maximum;
        }
    }

    FloatImage result(input.height(), input.width(), 0.0F);
    for (std::size_t y = 0; y < input.height(); ++y) {
        for (std::size_t x = 0; x < input.width(); ++x) {
            float maximum = -std::numeric_limits<float>::infinity();
            for (std::ptrdiff_t offset = -static_cast<std::ptrdiff_t>(radius);
                 offset <= static_cast<std::ptrdiff_t>(radius); ++offset) {
                const auto source_y = reflect(static_cast<std::ptrdiff_t>(y) + offset, input.height());
                maximum = std::max(maximum, horizontal(source_y, x));
            }
            result(y, x) = maximum;
        }
    }
    return result;
}

LabelImage connected_components8(const Mask& input, int* component_count) {
    LabelImage labels(input.height(), input.width(), 0);
    int next_label = 0;
    constexpr std::ptrdiff_t offsets[8][2] = {
        {-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, {0, 1}, {1, -1}, {1, 0}, {1, 1}};

    for (std::size_t y = 0; y < input.height(); ++y) {
        for (std::size_t x = 0; x < input.width(); ++x) {
            if (input(y, x) == 0 || labels(y, x) != 0) {
                continue;
            }
            ++next_label;
            std::deque<std::pair<std::size_t, std::size_t>> queue;
            queue.emplace_back(y, x);
            labels(y, x) = next_label;
            while (!queue.empty()) {
                const auto [current_y, current_x] = queue.front();
                queue.pop_front();
                for (const auto& offset : offsets) {
                    const auto candidate_y = static_cast<std::ptrdiff_t>(current_y) + offset[0];
                    const auto candidate_x = static_cast<std::ptrdiff_t>(current_x) + offset[1];
                    if (candidate_y < 0 || candidate_x < 0 ||
                        candidate_y >= static_cast<std::ptrdiff_t>(input.height()) ||
                        candidate_x >= static_cast<std::ptrdiff_t>(input.width())) {
                        continue;
                    }
                    const auto neighbour_y = static_cast<std::size_t>(candidate_y);
                    const auto neighbour_x = static_cast<std::size_t>(candidate_x);
                    if (input(neighbour_y, neighbour_x) != 0 && labels(neighbour_y, neighbour_x) == 0) {
                        labels(neighbour_y, neighbour_x) = next_label;
                        queue.emplace_back(neighbour_y, neighbour_x);
                    }
                }
            }
        }
    }
    if (component_count != nullptr) {
        *component_count = next_label;
    }
    return labels;
}

std::vector<Detection> detect(const FloatImage& probability, const DetectionOptions& options) {
    if (probability.empty()) {
        return {};
    }
    const auto smoothed = gaussian_blur(probability, options.gaussian_sigma);
    const auto local_maximum = maximum_filter(smoothed, options.peak_radius);

    struct Peak {
        std::size_t y;
        std::size_t x;
        float confidence;
    };
    std::vector<Peak> peaks;
    for (std::size_t y = 0; y < probability.height(); ++y) {
        for (std::size_t x = 0; x < probability.width(); ++x) {
            if (smoothed(y, x) >= options.minimum_peak && smoothed(y, x) == local_maximum(y, x)) {
                peaks.push_back({y, x, smoothed(y, x)});
            }
        }
    }
    std::stable_sort(peaks.begin(), peaks.end(), [](const Peak& left, const Peak& right) {
        return left.confidence > right.confidence;
    });

    std::vector<Detection> detections;
    detections.reserve(peaks.size());
    for (const auto& peak : peaks) {
        const auto y0 = peak.y > options.region_radius ? peak.y - options.region_radius : 0;
        const auto x0 = peak.x > options.region_radius ? peak.x - options.region_radius : 0;
        const auto y1 = std::min(probability.height(), peak.y + options.region_radius + 1);
        const auto x1 = std::min(probability.width(), peak.x + options.region_radius + 1);
        Mask thresholded(y1 - y0, x1 - x0, 0);
        for (std::size_t y = y0; y < y1; ++y) {
            for (std::size_t x = x0; x < x1; ++x) {
                thresholded(y - y0, x - x0) = smoothed(y, x) >= 0.5F * peak.confidence ? 1 : 0;
            }
        }
        const auto labels = connected_components8(thresholded);
        const int selected_label = labels(peak.y - y0, peak.x - x0);
        Mask region(thresholded.height(), thresholded.width(), 0);
        for (std::size_t y = 0; y < region.height(); ++y) {
            for (std::size_t x = 0; x < region.width(); ++x) {
                region(y, x) = labels(y, x) == selected_label ? 1 : 0;
            }
        }
        detections.push_back({peak.y, peak.x, y0, x0, peak.confidence, std::move(region)});
    }
    return detections;
}

Mask build_mask(const std::vector<Detection>& detections, float threshold, std::size_t height,
                std::size_t width) {
    Mask result(height, width, 0);
    for (const auto& detection : detections) {
        if (detection.confidence < threshold) {
            continue;
        }
        for (std::size_t y = 0; y < detection.region.height(); ++y) {
            for (std::size_t x = 0; x < detection.region.width(); ++x) {
                if (detection.region(y, x) != 0) {
                    result(detection.y0 + y, detection.x0 + x) = 1;
                }
            }
        }
    }
    return result;
}

std::vector<Component> describe_components(const Mask& mask) {
    int count = 0;
    const auto labels = connected_components8(mask, &count);
    std::vector<Component> result;
    result.reserve(static_cast<std::size_t>(count));
    for (int label = 1; label <= count; ++label) {
        std::size_t min_x = mask.width();
        std::size_t min_y = mask.height();
        std::size_t max_x = 0;
        std::size_t max_y = 0;
        std::size_t area = 0;
        double sum_x = 0.0;
        double sum_y = 0.0;
        for (std::size_t y = 0; y < mask.height(); ++y) {
            for (std::size_t x = 0; x < mask.width(); ++x) {
                if (labels(y, x) != label) {
                    continue;
                }
                min_x = std::min(min_x, x);
                min_y = std::min(min_y, y);
                max_x = std::max(max_x, x);
                max_y = std::max(max_y, y);
                ++area;
                sum_x += static_cast<double>(x);
                sum_y += static_cast<double>(y);
            }
        }
        result.push_back({label, min_x, min_y, max_x - min_x + 1, max_y - min_y + 1, area,
                          sum_x / static_cast<double>(area), sum_y / static_cast<double>(area)});
    }
    return result;
}

}  // namespace ml_odonto
