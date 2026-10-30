#pragma once

#include "ml_odonto/image.hpp"

#include <filesystem>
#include <memory>

namespace ml_odonto {

class InferenceEngine {
public:
    virtual ~InferenceEngine() = default;
    [[nodiscard]] virtual FloatImage predict(const GrayImage& normalized) = 0;
};

#ifdef ML_ODONTO_WITH_ONNX
class OnnxEngine final : public InferenceEngine {
public:
    explicit OnnxEngine(const std::filesystem::path& model_path);
    ~OnnxEngine() override;
    OnnxEngine(OnnxEngine&&) noexcept;
    OnnxEngine& operator=(OnnxEngine&&) noexcept;
    OnnxEngine(const OnnxEngine&) = delete;
    OnnxEngine& operator=(const OnnxEngine&) = delete;

    [[nodiscard]] FloatImage predict(const GrayImage& normalized) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
#endif

}  // namespace ml_odonto
