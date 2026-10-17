#pragma once
#include <cstdint>
#include <type_traits>

namespace oresforge::runtime {

/// Portable in-process complex128 value carrier.
///
/// This is an ABI/value-category foundation, not a claim that the Forge source
/// compiler lowers complex expressions yet. The two binary64 components are
/// kept explicit so signed zero and NaN payload bits are not hidden behind
/// std::complex implementation details.
struct Complex128 final {
  double real;
  double imaginary;
};

static_assert(std::is_standard_layout_v<Complex128>);
static_assert(std::is_trivially_copyable_v<Complex128>);
static_assert(sizeof(Complex128) == 16);
static_assert(alignof(Complex128) == alignof(double));

Complex128 complex128_from_i64(std::int64_t value);
Complex128 complex128_add(Complex128 left, Complex128 right) noexcept;
Complex128 complex128_subtract(Complex128 left, Complex128 right) noexcept;
Complex128 complex128_conjugate(Complex128 value) noexcept;

}  // namespace oresforge::runtime
