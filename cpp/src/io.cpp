#include "ml_odonto/io.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace ml_odonto {
namespace {

std::string next_token(std::istream& input) {
    std::string token;
    while (input >> token) {
        if (!token.empty() && token.front() == '#') {
            input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            continue;
        }
        return token;
    }
    throw std::runtime_error("unexpected end of image header");
}

template <typename T>
T byte_swap(T value) {
    static_assert(std::is_trivially_copyable_v<T>);
    unsigned char bytes[sizeof(T)]{};
    std::memcpy(bytes, &value, sizeof(T));
    std::reverse(std::begin(bytes), std::end(bytes));
    std::memcpy(&value, bytes, sizeof(T));
    return value;
}

}  // namespace

GrayImage read_pgm(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open PGM file: " + path.string());
    }
    if (next_token(input) != "P5") {
        throw std::runtime_error("only binary 8-bit PGM (P5) is supported");
    }
    const auto width = static_cast<std::size_t>(std::stoull(next_token(input)));
    const auto height = static_cast<std::size_t>(std::stoull(next_token(input)));
    if (std::stoi(next_token(input)) != 255) {
        throw std::runtime_error("only 8-bit PGM is supported");
    }
    input.get();
    GrayImage result(height, width, 0);
    input.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size()));
    if (!input) {
        throw std::runtime_error("truncated PGM file");
    }
    return result;
}

void write_pgm(const std::filesystem::path& path, const GrayImage& image) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("cannot create PGM file: " + path.string());
    }
    output << "P5\n" << image.width() << ' ' << image.height() << "\n255\n";
    output.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
}

FloatImage read_pfm(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open PFM file: " + path.string());
    }
    if (next_token(input) != "Pf") {
        throw std::runtime_error("only greyscale PFM (Pf) is supported");
    }
    const auto width = static_cast<std::size_t>(std::stoull(next_token(input)));
    const auto height = static_cast<std::size_t>(std::stoull(next_token(input)));
    const float scale = std::stof(next_token(input));
    input.get();
    const bool file_little_endian = scale < 0.0F;
    const bool native_little_endian = std::endian::native == std::endian::little;
    FloatImage result(height, width, 0.0F);
    for (std::size_t file_y = 0; file_y < height; ++file_y) {
        const auto y = height - file_y - 1;
        for (std::size_t x = 0; x < width; ++x) {
            float value = 0.0F;
            input.read(reinterpret_cast<char*>(&value), sizeof(value));
            if (file_little_endian != native_little_endian) {
                value = byte_swap(value);
            }
            result(y, x) = value * std::abs(scale);
        }
    }
    if (!input) {
        throw std::runtime_error("truncated PFM file");
    }
    return result;
}

void write_pfm(const std::filesystem::path& path, const FloatImage& image) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("cannot create PFM file: " + path.string());
    }
    const bool little_endian = std::endian::native == std::endian::little;
    output << "Pf\n" << image.width() << ' ' << image.height() << '\n'
           << (little_endian ? -1.0F : 1.0F) << '\n';
    for (std::size_t file_y = 0; file_y < image.height(); ++file_y) {
        const auto y = image.height() - file_y - 1;
        output.write(reinterpret_cast<const char*>(image.data() + y * image.width()),
                     static_cast<std::streamsize>(image.width() * sizeof(float)));
    }
}

void write_detections_json(const std::filesystem::path& path,
                           const std::vector<Detection>& detections,
                           const std::vector<Component>& components, float threshold) {
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("cannot create JSON file: " + path.string());
    }
    output << std::fixed << std::setprecision(6);
    output << "{\n  \"schema_version\": 1,\n  \"threshold\": " << threshold
           << ",\n  \"candidate_count\": " << detections.size() << ",\n  \"components\": [";
    for (std::size_t index = 0; index < components.size(); ++index) {
        const auto& component = components[index];
        output << (index == 0 ? "" : ",") << "\n    {\"label\": " << component.label
               << ", \"x\": " << component.x << ", \"y\": " << component.y
               << ", \"width\": " << component.width << ", \"height\": " << component.height
               << ", \"area\": " << component.area << ", \"center_x\": " << component.center_x
               << ", \"center_y\": " << component.center_y << '}';
    }
    output << (components.empty() ? "" : "\n") << "  ],\n  \"candidates\": [";
    for (std::size_t index = 0; index < detections.size(); ++index) {
        const auto& detection = detections[index];
        output << (index == 0 ? "" : ",") << "\n    {\"peak_x\": " << detection.peak_x
               << ", \"peak_y\": " << detection.peak_y << ", \"confidence\": "
               << detection.confidence << ", \"accepted\": "
               << (detection.confidence >= threshold ? "true" : "false") << '}';
    }
    output << (detections.empty() ? "" : "\n") << "  ]\n}\n";
}

}  // namespace ml_odonto
