#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace ml_odonto {

template <typename T>
class Image {
public:
    Image() = default;
    Image(std::size_t height, std::size_t width, T value = {})
        : height_(height), width_(width), pixels_(height * width, std::move(value)) {}

    [[nodiscard]] std::size_t height() const noexcept { return height_; }
    [[nodiscard]] std::size_t width() const noexcept { return width_; }
    [[nodiscard]] bool empty() const noexcept { return pixels_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return pixels_.size(); }

    T& operator()(std::size_t row, std::size_t column) { return pixels_.at(row * width_ + column); }
    const T& operator()(std::size_t row, std::size_t column) const {
        return pixels_.at(row * width_ + column);
    }

    [[nodiscard]] T* data() noexcept { return pixels_.data(); }
    [[nodiscard]] const T* data() const noexcept { return pixels_.data(); }
    [[nodiscard]] std::vector<T>& pixels() noexcept { return pixels_; }
    [[nodiscard]] const std::vector<T>& pixels() const noexcept { return pixels_; }

private:
    std::size_t height_{0};
    std::size_t width_{0};
    std::vector<T> pixels_;
};

using GrayImage = Image<unsigned char>;
using FloatImage = Image<float>;
using Mask = Image<unsigned char>;
using LabelImage = Image<int>;

[[nodiscard]] std::size_t reflect(std::ptrdiff_t index, std::size_t length);
[[nodiscard]] std::size_t reflect101(std::ptrdiff_t index, std::size_t length);

}  // namespace ml_odonto
