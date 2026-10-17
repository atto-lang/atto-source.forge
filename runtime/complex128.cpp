#include "runtime/complex128.hpp"

#include <stdexcept>

namespace oresforge::runtime {
namespace {
constexpr std::int64_t kExactBinary64Integer = 9'007'199'254'740'992LL;
}

Complex128 complex128_from_i64(std::int64_t value) {
  if (value < -kExactBinary64Integer || value > kExactBinary64Integer) {
    throw std::runtime_error(
        "complex128 integer promotion requires the exact binary64 range");
  }
  return {static_cast<double>(value), 0.0};
}

Complex128 complex128_add(Complex128 left, Complex128 right) noexcept {
  return {left.real + right.real, left.imaginary + right.imaginary};
}

Complex128 complex128_subtract(Complex128 left, Complex128 right) noexcept {
  return {left.real - right.real, left.imaginary - right.imaginary};
}

Complex128 complex128_conjugate(Complex128 value) noexcept {
  return {value.real, -value.imaginary};
}

}  // namespace oresforge::runtime
