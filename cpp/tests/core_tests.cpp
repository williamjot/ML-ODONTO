#include "ml_odonto/detection.hpp"
#include "ml_odonto/image.hpp"
#include "ml_odonto/io.hpp"
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

}  // namespace

int main() {
    test_reflection();
    test_components_use_eight_connectivity();
    test_area_resize();
    test_blur_and_maximum();
    test_detection_matches_r_synthetic_case();
    test_clahe_range();
    test_pfm_roundtrip();
    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "all core tests passed\n";
    return EXIT_SUCCESS;
}
