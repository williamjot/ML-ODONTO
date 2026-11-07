#pragma once

#include "ml_odonto/detection.hpp"
#include "ml_odonto/image.hpp"
#include "ml_odonto/inference.hpp"
#include "ml_odonto/preprocessing.hpp"

#include <cstddef>
#include <vector>

namespace ml_odonto {

// Mesmo fluxo de prever_radiografia() + detectar_lesoes() do aplicativo R:
// normaliza na largura do modelo, prevê (com espelhamento, dentro do grafo ONNX),
// reduz a probabilidade por área até a largura de trabalho e detecta as lesões nela.
struct PipelineOptions {
    std::size_t model_width{2000};
    std::size_t working_width{1400};
    float threshold{0.5F};
    PreprocessingOptions preprocessing{};
    DetectionOptions detection{};
};

struct PipelineResult {
    FloatImage probability;  // escala de trabalho
    std::vector<Detection> detections;
    Mask mask;               // escala de trabalho, limiar aplicado
    std::vector<Component> components;
};

[[nodiscard]] PipelineResult run_pipeline(const GrayImage& original, InferenceEngine& engine,
                                          const PipelineOptions& options = {});

}  // namespace ml_odonto
