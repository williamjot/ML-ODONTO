#include "ml_odonto/detection.hpp"
#include "ml_odonto/io.hpp"
#include "ml_odonto/inference.hpp"
#include "ml_odonto/pipeline.hpp"
#include "ml_odonto/preprocessing.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

void usage() {
    std::cerr << "ML ODONTO C++ equivalence core\n\n"
              << "Usage:\n"
              << "  ml-odonto-cli detect <probability.pfm> <mask.pgm> <detections.json> [threshold]\n"
              << "  ml-odonto-cli preprocess <input.pgm> <output.pgm> [target-width]\n"
#ifdef ML_ODONTO_WITH_ONNX
              << "  ml-odonto-cli infer <model.onnx> <normalized.pgm> <probability.pfm>\n"
              << "  ml-odonto-cli run <model.onnx> <output-dir> <radiograph>... [--threshold t]\n"
              << "                [--model-width n] [--working-width n]\n"
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

// Argumentos como caminhos. No Windows, argv vem na página de código ANSI e estraga nomes com
// acentos fora dela; por isso os caminhos são relidos da linha de comando em UTF-16.
std::vector<std::filesystem::path> path_arguments(int argc, char** argv) {
#ifdef _WIN32
    int count = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (wide != nullptr) {
        std::vector<std::filesystem::path> result;
        if (count == argc) {
            result.assign(wide, wide + count);
        }
        LocalFree(wide);
        if (!result.empty()) {
            return result;
        }
    }
#endif
    return {argv, argv + argc};
}

ml_odonto::GrayImage to_display(const ml_odonto::Mask& mask) {
    ml_odonto::GrayImage output(mask.height(), mask.width(), 0);
    std::transform(mask.pixels().begin(), mask.pixels().end(), output.pixels().begin(),
                   [](unsigned char value) { return value == 0 ? 0 : 255; });
    return output;
}

#ifdef ML_ODONTO_WITH_ONNX
// Processa várias radiografias com o modelo carregado uma única vez. Para cada uma grava
// <nome>_probabilidade.pfm, <nome>_mascara.png e <nome>_deteccoes.json na pasta de saída.
// Uma radiografia com erro não interrompe as demais; o código de saída é 1 se alguma falhou.
int run(int argc, char** argv, const std::vector<std::filesystem::path>& paths) {
    ml_odonto::PipelineOptions options;
    std::vector<std::filesystem::path> inputs;
    for (int index = 4; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        const auto value = [&]() -> const char* {
            if (index + 1 >= argc) {
                throw std::invalid_argument(std::string(argument) + " needs a value");
            }
            return argv[++index];
        };
        if (argument == "--threshold") {
            options.threshold = parse_threshold(value());
        } else if (argument == "--model-width") {
            options.model_width = static_cast<std::size_t>(std::stoull(value()));
        } else if (argument == "--working-width") {
            options.working_width = static_cast<std::size_t>(std::stoull(value()));
        } else {
            inputs.push_back(paths[static_cast<std::size_t>(index)]);
        }
    }
    if (inputs.empty()) {
        usage();
        return 2;
    }
    const auto& output_directory = paths[3];
    std::filesystem::create_directories(output_directory);
    ml_odonto::OnnxEngine engine(paths[2]);

    int failed = 0;
    for (const auto& input : inputs) {
        const auto start = std::chrono::steady_clock::now();
        const auto output = [&](const char* suffix) {
            auto path = output_directory / input.stem();
            path += suffix;
            return path;
        };
        const auto name = input.stem().u8string();
        const std::string stem(name.begin(), name.end());
        try {
            const auto original = ml_odonto::read_image(input);
            const auto result = ml_odonto::run_pipeline(original, engine, options);
            const double seconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            ml_odonto::write_pfm(output("_probabilidade.pfm"), result.probability);
            ml_odonto::write_png(output("_mascara.png"), to_display(result.mask));
            ml_odonto::write_detections_json(output("_deteccoes.json"),
                                             result.detections, result.components, options.threshold,
                                             original.height(), original.width(), seconds);
            std::cout << "ok " << stem << " lesions=" << result.components.size()
                      << " seconds=" << seconds << std::endl;
        } catch (const std::exception& error) {
            ++failed;
            std::cout << "error " << stem << ": " << error.what() << std::endl;
        }
    }
    return failed == 0 ? 0 : 1;
}
#endif

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            usage();
            return 2;
        }
        const std::string_view command{argv[1]};
        const auto paths = path_arguments(argc, argv);
        if (command == "detect") {
            if (argc < 5 || argc > 6) {
                usage();
                return 2;
            }
            const float threshold = argc == 6 ? parse_threshold(argv[5]) : 0.5F;
            const auto probability = ml_odonto::read_pfm(paths[2]);
            const auto detections = ml_odonto::detect(probability);
            const auto mask = ml_odonto::build_mask(detections, threshold, probability.height(),
                                                    probability.width());
            const auto output = to_display(mask);
            const auto components = ml_odonto::describe_components(mask);
            ml_odonto::write_pgm(paths[3], output);
            ml_odonto::write_detections_json(paths[4], detections, components, threshold);
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
            const auto input = ml_odonto::read_pgm(paths[2]);
            ml_odonto::write_pgm(paths[3], ml_odonto::preprocess(input, options));
            return 0;
        }
#ifdef ML_ODONTO_WITH_ONNX
        if (command == "infer") {
            if (argc != 5) {
                usage();
                return 2;
            }
            ml_odonto::OnnxEngine engine(paths[2]);
            const auto input = ml_odonto::read_pgm(paths[3]);
            ml_odonto::write_pfm(paths[4], engine.predict(input));
            return 0;
        }
        if (command == "run") {
            if (argc < 5) {
                usage();
                return 2;
            }
            return run(argc, argv, paths);
        }
#endif
        usage();
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
