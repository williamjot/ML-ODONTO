#include "ml_odonto/detection.hpp"
#include "ml_odonto/io.hpp"
#include "ml_odonto/inference.hpp"
#include "ml_odonto/preprocessing.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

void usage() {
    std::cerr << "ML ODONTO C++ equivalence core\n\n"
              << "Usage:\n"
              << "  ml-odonto-cli detect <probability.pfm> <mask.pgm> <detections.json> [threshold]\n"
              << "  ml-odonto-cli preprocess <input.pgm> <output.pgm> [target-width]\n"
#ifdef ML_ODONTO_WITH_ONNX
              << "  ml-odonto-cli infer <model.onnx> <normalized.pgm> <probability.pfm>\n"
#endif
        ;
}

float parse_threshold(const char* text) {
    const float value = std::stof(text);
    if (value < 0.0F || value > 1.0F) {
        throw std::invalid_argument("threshold must be between 0 and 1");
    }
    return value;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            usage();
            return 2;
        }
        const std::string_view command{argv[1]};
        if (command == "detect") {
            if (argc < 5 || argc > 6) {
                usage();
                return 2;
            }
            const float threshold = argc == 6 ? parse_threshold(argv[5]) : 0.5F;
            const auto probability = ml_odonto::read_pfm(argv[2]);
            const auto detections = ml_odonto::detect(probability);
            const auto mask = ml_odonto::build_mask(detections, threshold, probability.height(),
                                                    probability.width());
            ml_odonto::GrayImage output(mask.height(), mask.width(), 0);
            std::transform(mask.pixels().begin(), mask.pixels().end(), output.pixels().begin(),
                           [](unsigned char value) { return value == 0 ? 0 : 255; });
            const auto components = ml_odonto::describe_components(mask);
            ml_odonto::write_pgm(argv[3], output);
            ml_odonto::write_detections_json(argv[4], detections, components, threshold);
            std::cout << "candidates=" << detections.size() << " components=" << components.size() << '\n';
            return 0;
        }
        if (command == "preprocess") {
            if (argc < 4 || argc > 5) {
                usage();
                return 2;
            }
            ml_odonto::PreprocessingOptions options;
            if (argc == 5) {
                options.target_width = static_cast<std::size_t>(std::stoull(argv[4]));
            }
            const auto input = ml_odonto::read_pgm(argv[2]);
            ml_odonto::write_pgm(argv[3], ml_odonto::preprocess(input, options));
            return 0;
        }
#ifdef ML_ODONTO_WITH_ONNX
        if (command == "infer") {
            if (argc != 5) {
                usage();
                return 2;
            }
            ml_odonto::OnnxEngine engine(argv[2]);
            const auto input = ml_odonto::read_pgm(argv[3]);
            ml_odonto::write_pfm(argv[4], engine.predict(input));
            return 0;
        }
#endif
        usage();
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
