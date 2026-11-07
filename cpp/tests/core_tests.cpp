#include "ml_odonto/detection.hpp"
#include "ml_odonto/image.hpp"
#include "ml_odonto/io.hpp"
#include "ml_odonto/pipeline.hpp"
#include "ml_odonto/preprocessing.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void test_reflection() {
    check(ml_odonto::reflect(-1, 4) == 0, "reflect left border");
    check(ml_odonto::reflect(4, 4) == 3, "reflect right border");
    check(ml_odonto::reflect101(-1, 4) == 1, "reflect101 left border");
    check(ml_odonto::reflect101(4, 4) == 2, "reflect101 right border");
}

void test_components_use_eight_connectivity() {
    ml_odonto::Mask mask(6, 6, 0);
    mask(1, 1) = 1;
    mask(2, 2) = 1;
    mask(4, 4) = 1;
    int count = 0;
    const auto labels = ml_odonto::connected_components8(mask, &count);
    check(count == 2, "diagonal pixels form one 8-connected component");
    check(labels(1, 1) == labels(2, 2), "diagonal labels match");
}

void test_area_resize() {
    ml_odonto::GrayImage input(40, 50, 0);
    for (std::size_t y = 0; y < input.height(); ++y) {
        for (std::size_t x = 0; x < input.width(); ++x) {
            input(y, x) = static_cast<unsigned char>((y * 7 + x * 3) % 256);
        }
    }
    const auto output = ml_odonto::resize_to_width(input, 20);
    check(output.height() == 16 && output.width() == 20, "area resize dimensions");
    const auto mean = [](const ml_odonto::GrayImage& image) {
        return std::accumulate(image.pixels().begin(), image.pixels().end(), 0.0) /
               static_cast<double>(image.size());
    };
    check(std::abs(mean(input) - mean(output)) < 1.0, "area resize approximately preserves mean");
}

void test_blur_and_maximum() {
    ml_odonto::FloatImage constant(30, 40, 0.3F);
    const auto blurred = ml_odonto::gaussian_blur(constant);
    float error = 0.0F;
    for (const float value : blurred.pixels()) {
        error = std::max(error, std::abs(value - 0.3F));
    }
    check(error < 1.0e-6F, "Gaussian blur preserves a constant image");

    ml_odonto::FloatImage impulse(50, 50, 0.0F);
    impulse(24, 24) = 1.0F;
    const auto maximum = ml_odonto::maximum_filter(impulse, 3);
    check(maximum(21, 21) == 1.0F && maximum(27, 27) == 1.0F,
          "maximum filter fills the expected window");
    check(maximum(20, 24) == 0.0F, "maximum filter stops outside the window");
}

void test_detection_matches_r_synthetic_case() {
    ml_odonto::FloatImage probability(80, 120, 0.0F);
    for (std::size_t y = 19; y <= 29; ++y) {
        for (std::size_t x = 19; x <= 29; ++x) {
            probability(y, x) = 0.9F;
        }
    }
    for (std::size_t y = 49; y <= 59; ++y) {
        for (std::size_t x = 79; x <= 93; ++x) {
            probability(y, x) = 0.2F;
        }
    }
    ml_odonto::DetectionOptions options;
    options.region_radius = 40;
    const auto detections = ml_odonto::detect(probability, options);
    check(detections.size() == 2, "synthetic probability map has two candidates");
    check(detections.size() < 2 || detections[0].confidence > detections[1].confidence,
          "candidates are sorted by confidence");
    const auto mask = ml_odonto::build_mask(detections, 0.5F, 80, 120);
    check(ml_odonto::describe_components(mask).size() == 1,
          "threshold accepts only the high-confidence lesion");
}

void test_clahe_range() {
    ml_odonto::GrayImage input(130, 170, 0);
    for (std::size_t y = 0; y < input.height(); ++y) {
        for (std::size_t x = 0; x < input.width(); ++x) {
            input(y, x) = static_cast<unsigned char>(60 + (y * 13 + x * 17) % 121);
        }
    }
    const auto output = ml_odonto::clahe(input);
    check(output.height() == input.height() && output.width() == input.width(),
          "CLAHE preserves dimensions");
}

void test_pfm_roundtrip() {
    ml_odonto::FloatImage input(3, 4, 0.0F);
    for (std::size_t index = 0; index < input.size(); ++index) {
        input.pixels()[index] = static_cast<float>(index) / 11.0F;
    }
    const auto path = std::filesystem::temp_directory_path() / "ml_odonto_core_test.pfm";
    ml_odonto::write_pfm(path, input);
    const auto output = ml_odonto::read_pfm(path);
    std::filesystem::remove(path);
    check(output.height() == input.height() && output.width() == input.width(),
          "PFM roundtrip dimensions");
    for (std::size_t index = 0; index < input.size(); ++index) {
        check(std::abs(output.pixels()[index] - input.pixels()[index]) < 1.0e-7F,
              "PFM roundtrip value");
    }
}

void test_float_resize_and_rounding() {
    ml_odonto::FloatImage input(20, 30, 0.0F);
    for (std::size_t y = 0; y < input.height(); ++y) {
        for (std::size_t x = 0; x < input.width(); ++x) {
            input(y, x) = static_cast<float>(y * x) / 600.0F;
        }
    }
    const auto output = ml_odonto::resize_area(input, 14, 21);
    const auto mean = [](const ml_odonto::FloatImage& image) {
        return std::accumulate(image.pixels().begin(), image.pixels().end(), 0.0) /
               static_cast<double>(image.size());
    };
    check(output.height() == 14 && output.width() == 21, "float area resize dimensions");
    check(std::abs(mean(input) - mean(output)) < 1.0e-5, "float area resize preserves the mean");
    check(output(0, 0) < 0.002F, "float area resize keeps fractional values");

    // round() do R e cvRound arredondam metade para o par
    check(ml_odonto::height_for_width(3, 2, 1) == 2, "height 1.5 rounds to 2");
    check(ml_odonto::height_for_width(5, 2, 1) == 2, "height 2.5 rounds to 2");
    check(ml_odonto::height_for_width(1297, 2573, 1400) == 706, "reference radiograph height");
}

void test_png_roundtrip() {
    ml_odonto::GrayImage input(5, 7, 0);
    for (std::size_t index = 0; index < input.size(); ++index) {
        input.pixels()[index] = static_cast<unsigned char>(index * 7);
    }
    const auto path = std::filesystem::temp_directory_path() / "ml_odonto_core_test.png";
    ml_odonto::write_png(path, input);
    const auto output = ml_odonto::read_image(path);
    std::filesystem::remove(path);
    check(output.height() == 5 && output.width() == 7 && output.pixels() == input.pixels(),
          "PNG roundtrip is lossless");
}

// Motor falso: uma mancha de probabilidade alta no centro, como a U-Net devolveria.
class BlobEngine final : public ml_odonto::InferenceEngine {
public:
    ml_odonto::FloatImage predict(const ml_odonto::GrayImage& normalized) override {
        last_width = normalized.width();
        ml_odonto::FloatImage result(normalized.height(), normalized.width(), 0.0F);
        for (std::size_t y = normalized.height() / 2 - 15; y < normalized.height() / 2 + 15; ++y) {
            for (std::size_t x = normalized.width() / 2 - 15; x < normalized.width() / 2 + 15; ++x) {
                result(y, x) = 0.95F;
            }
        }
        return result;
    }
    std::size_t last_width{0};
};

void test_pipeline_scales() {
    ml_odonto::GrayImage original(120, 260, 0);
    for (std::size_t index = 0; index < original.size(); ++index) {
        original.pixels()[index] = static_cast<unsigned char>(40 + index % 150);
    }
    BlobEngine engine;
    ml_odonto::PipelineOptions options;
    options.model_width = 200;
    options.working_width = 140;
    options.detection.region_radius = 30;
    const auto result = ml_odonto::run_pipeline(original, engine, options);
    check(engine.last_width == 200, "pipeline predicts at the model width");
    check(result.probability.width() == 140 && result.probability.height() == 65,
          "pipeline reduces the probability to the working width");
    check(result.mask.width() == 140 && result.components.size() == 1,
          "pipeline finds the single lesion");
}

}  // namespace

int main() {
    test_reflection();
    test_components_use_eight_connectivity();
    test_area_resize();
    test_blur_and_maximum();
    test_detection_matches_r_synthetic_case();
    test_clahe_range();
    test_pfm_roundtrip();
    test_float_resize_and_rounding();
    test_png_roundtrip();
    test_pipeline_scales();
    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "all core tests passed\n";
    return EXIT_SUCCESS;
}
