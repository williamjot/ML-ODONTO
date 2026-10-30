#include "ml_odonto/image.hpp"

#include <cstddef>
#include <stdexcept>

namespace ml_odonto {
namespace {

void check_length(std::size_t length) {
    if (length == 0) {
        throw std::invalid_argument("image dimension must be positive");
    }
}

}  // namespace

std::size_t reflect(std::ptrdiff_t index, std::size_t length) {
    check_length(length);
    if (length == 1) {
        return 0;
    }
    const auto n = static_cast<std::ptrdiff_t>(length);
    while (index < 0 || index >= n) {
        index = index < 0 ? -index - 1 : 2 * n - index - 1;
    }
    return static_cast<std::size_t>(index);
}

std::size_t reflect101(std::ptrdiff_t index, std::size_t length) {
    check_length(length);
    if (length == 1) {
        return 0;
    }
    const auto n = static_cast<std::ptrdiff_t>(length);
    while (index < 0 || index >= n) {
        index = index < 0 ? -index : 2 * n - index - 2;
    }
    return static_cast<std::size_t>(index);
}

}  // namespace ml_odonto
