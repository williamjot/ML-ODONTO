#include "ml_odonto/pipeline.hpp"

#include <stdexcept>
#include <utility>

namespace ml_odonto {

PipelineResult run_pipeline(const GrayImage& original, InferenceEngine& engine,
                            const PipelineOptions& options) {
    if (original.empty()) {
        throw std::invalid_argument("cannot process an empty radiograph");
    }
    auto preprocessing = options.preprocessing;
    preprocessing.target_width = options.model_width;
    const auto normalized = preprocess(original, preprocessing);

    PipelineResult result;
    result.probability = engine.predict(normalized);
    if (result.probability.width() != options.working_width) {
        const auto height =
            height_for_width(original.height(), original.width(), options.working_width);
        result.probability = resize_area(result.probability, height, options.working_width);
    }
    result.detections = detect(result.probability, options.detection);
    result.mask = build_mask(result.detections, options.threshold, result.probability.height(),
                             result.probability.width());
    result.components = describe_components(result.mask);
    return result;
}

}  // namespace ml_odonto
