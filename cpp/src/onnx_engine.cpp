#include "ml_odonto/inference.hpp"

#include "ml_odonto/image.hpp"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace ml_odonto {

class OnnxEngine::Impl {
public:
    explicit Impl(const std::filesystem::path& model_path)
        : environment_(ORT_LOGGING_LEVEL_WARNING, "ml-odonto"),
          session_(create_session(environment_, model_path)) {
        Ort::AllocatorWithDefaultOptions allocator;
        const auto input = session_.GetInputNameAllocated(0, allocator);
        const auto output = session_.GetOutputNameAllocated(0, allocator);
        input_name_ = input.get();
        output_name_ = output.get();
    }

    FloatImage predict(const GrayImage& normalized) {
        if (normalized.empty()) {
            throw std::invalid_argument("cannot run inference on an empty image");
        }
        const auto padded_height = ((normalized.height() + 31) / 32) * 32;
        const auto padded_width = ((normalized.width() + 31) / 32) * 32;
        std::vector<float> tensor_data(padded_height * padded_width);
        for (std::size_t y = 0; y < padded_height; ++y) {
            const auto source_y = reflect101(static_cast<std::ptrdiff_t>(y), normalized.height());
            for (std::size_t x = 0; x < padded_width; ++x) {
                const auto source_x = reflect101(static_cast<std::ptrdiff_t>(x), normalized.width());
                tensor_data[y * padded_width + x] =
                    static_cast<float>(normalized(source_y, source_x)) / 255.0F;
            }
        }

        const std::array<std::int64_t, 4> shape{
            1, 1, static_cast<std::int64_t>(padded_height), static_cast<std::int64_t>(padded_width)};
        auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        auto tensor = Ort::Value::CreateTensor<float>(memory, tensor_data.data(), tensor_data.size(),
                                                      shape.data(), shape.size());
        const char* input_names[] = {input_name_.c_str()};
        const char* output_names[] = {output_name_.c_str()};
        auto outputs = session_.Run(Ort::RunOptions{nullptr}, input_names, &tensor, 1, output_names, 1);
        if (outputs.size() != 1 || !outputs.front().IsTensor()) {
            throw std::runtime_error("the ONNX model returned an invalid output");
        }
        const auto output_shape = outputs.front().GetTensorTypeAndShapeInfo().GetShape();
        if (output_shape.size() != 4 || output_shape[0] != 1 || output_shape[1] != 1 ||
            output_shape[2] < static_cast<std::int64_t>(normalized.height()) ||
            output_shape[3] < static_cast<std::int64_t>(normalized.width())) {
            throw std::runtime_error("unexpected ONNX output shape");
        }
        const auto output_width = static_cast<std::size_t>(output_shape[3]);
        const float* values = outputs.front().GetTensorData<float>();
        FloatImage result(normalized.height(), normalized.width(), 0.0F);
        for (std::size_t y = 0; y < result.height(); ++y) {
            std::copy_n(values + y * output_width, result.width(), result.data() + y * result.width());
        }
        return result;
    }

private:
    static Ort::Session create_session(Ort::Env& environment, const std::filesystem::path& model_path) {
        Ort::SessionOptions options;
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        const auto threads = std::max(1U, std::thread::hardware_concurrency());
        options.SetIntraOpNumThreads(static_cast<int>(threads));
#ifdef _WIN32
        return Ort::Session(environment, model_path.c_str(), options);
#else
        return Ort::Session(environment, model_path.string().c_str(), options);
#endif
    }

    Ort::Env environment_;
    Ort::Session session_;
    std::string input_name_;
    std::string output_name_;
};

OnnxEngine::OnnxEngine(const std::filesystem::path& model_path)
    : impl_(std::make_unique<Impl>(model_path)) {}
OnnxEngine::~OnnxEngine() = default;
OnnxEngine::OnnxEngine(OnnxEngine&&) noexcept = default;
OnnxEngine& OnnxEngine::operator=(OnnxEngine&&) noexcept = default;

FloatImage OnnxEngine::predict(const GrayImage& normalized) { return impl_->predict(normalized); }

}  // namespace ml_odonto
