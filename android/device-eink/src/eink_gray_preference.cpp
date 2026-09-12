#include "eink_gray_preference.h"

#include <algorithm>
#include <array>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace neo2::eink {
namespace {
template<int Low, int High>
constexpr auto Weights() {
  std::array<int, 256> result{};
  constexpr int width = High - Low;
  for (int spread = 0; spread < 256; ++spread) {
    const int t = std::clamp(spread - Low, 0, width);
    result[spread] = t * t * (3 * width - 2 * t);
  }
  return result;
}

#if defined(__aarch64__)
// A vector table lookup avoids scalar per-pixel gathers. Both supported fades
// end below 64 and their integer smoothstep numerators fit in 16 bits.
template<int Low, int High, int Shift>
constexpr auto WeightBytes() {
  constexpr auto weights = Weights<Low, High>();
  std::array<std::uint8_t, 64> result{};
  for (int i = 0; i < 64; ++i) result[i] = (weights[i] >> Shift) & 255;
  return result;
}

uint8x16x4_t LoadTable(const std::array<std::uint8_t, 64>& table) {
  return {{vld1q_u8(table.data()), vld1q_u8(table.data() + 16),
           vld1q_u8(table.data() + 32), vld1q_u8(table.data() + 48)}};
}

template<int Denominator>
uint16x8_t Blend(uint16x8_t channel, uint16x8_t luma, uint16x8_t weight) {
  const auto neutral_weight = vsubq_u16(vdupq_n_u16(Denominator), weight);
  auto lower = vmull_u16(vget_low_u16(luma), vget_low_u16(neutral_weight));
  auto upper = vmull_u16(vget_high_u16(luma), vget_high_u16(neutral_weight));
  lower = vmlal_u16(lower, vget_low_u16(channel), vget_low_u16(weight));
  upper = vmlal_u16(upper, vget_high_u16(channel), vget_high_u16(weight));
  lower = vaddq_u32(lower, vdupq_n_u32(Denominator / 2));
  upper = vaddq_u32(upper, vdupq_n_u32(Denominator / 2));
  // Clang lowers division by the compile-time constant to exact shifts and
  // multiplies. No floating-point approximation at quantization boundaries.
  lower = lower / vdupq_n_u32(Denominator);
  upper = upper / vdupq_n_u32(Denominator);
  return vcombine_u16(vmovn_u32(lower), vmovn_u32(upper));
}

template<int Low, int High>
std::size_t ApplyVectors(std::span<std::uint8_t> rgba) {
  static constexpr auto lower_bytes = WeightBytes<Low, High, 0>();
  static constexpr auto upper_bytes = WeightBytes<Low, High, 8>();
  const auto lower_table = LoadTable(lower_bytes), upper_table = LoadTable(upper_bytes);
  constexpr int width = High - Low;
  constexpr int denominator = width * width * width;
  std::size_t i = 0;
  const auto bytes = static_cast<std::size_t>(rgba.size());
  for (; bytes - i >= 64; i += 64) {
    auto pixels = vld4q_u8(rgba.data() + i);
    const auto maximum = vmaxq_u8(vmaxq_u8(pixels.val[0], pixels.val[1]), pixels.val[2]);
    const auto minimum = vminq_u8(vminq_u8(pixels.val[0], pixels.val[1]), pixels.val[2]);
    const auto spread = vsubq_u8(maximum, minimum);
    const auto max_spread = vmaxvq_u8(spread);
    if (max_spread == 0 || vminvq_u8(spread) >= High) continue;
    const auto mask = vcltq_u8(spread, vdupq_n_u8(High));
    auto luma0 = vmull_u8(vget_low_u8(pixels.val[0]), vdup_n_u8(38));
    auto luma1 = vmull_u8(vget_high_u8(pixels.val[0]), vdup_n_u8(38));
    luma0 = vmlal_u8(luma0, vget_low_u8(pixels.val[1]), vdup_n_u8(75));
    luma1 = vmlal_u8(luma1, vget_high_u8(pixels.val[1]), vdup_n_u8(75));
    luma0 = vmlal_u8(luma0, vget_low_u8(pixels.val[2]), vdup_n_u8(15));
    luma1 = vmlal_u8(luma1, vget_high_u8(pixels.val[2]), vdup_n_u8(15));
    luma0 = vshrq_n_u16(vaddq_u16(luma0, vdupq_n_u16(64)), 7);
    luma1 = vshrq_n_u16(vaddq_u16(luma1, vdupq_n_u16(64)), 7);
    if (max_spread <= Low) {
      const auto gray = vcombine_u8(vmovn_u16(luma0), vmovn_u16(luma1));
      pixels.val[0] = pixels.val[1] = pixels.val[2] = gray;
      vst4q_u8(rgba.data() + i, pixels);
      continue;
    }
    const auto weight_low = vqtbl4q_u8(lower_table, spread);
    const auto weight_high = vqtbl4q_u8(upper_table, spread);
    const auto weights0 = vorrq_u16(vmovl_u8(vget_low_u8(weight_low)),
            vshlq_n_u16(vmovl_u8(vget_low_u8(weight_high)), 8));
    const auto weights1 = vorrq_u16(vmovl_u8(vget_high_u8(weight_low)),
            vshlq_n_u16(vmovl_u8(vget_high_u8(weight_high)), 8));
    for (int c = 0; c < 3; ++c) {
      const auto lower = Blend<denominator>(vmovl_u8(vget_low_u8(pixels.val[c])), luma0, weights0);
      const auto upper = Blend<denominator>(vmovl_u8(vget_high_u8(pixels.val[c])), luma1, weights1);
      const auto blended = vcombine_u8(vmovn_u16(lower), vmovn_u16(upper));
      pixels.val[c] = vbslq_u8(mask, blended, pixels.val[c]);
    }
    vst4q_u8(rgba.data() + i, pixels);
  }
  return i;
}
#endif

template<int Low, int High>
void Apply(std::span<std::uint8_t> rgba) {
  static constexpr auto weights = Weights<Low, High>();
  constexpr int width = High - Low;
  constexpr int denominator = width * width * width;
  const auto bytes = static_cast<std::size_t>(rgba.size());
  std::size_t i = 0;
#if defined(__aarch64__)
  i = ApplyVectors<Low, High>(rgba);
#endif
  for (; i < bytes; i += 4) {
    const int red = rgba[i], green = rgba[i + 1], blue = rgba[i + 2];
    const int spread = std::max({red, green, blue}) - std::min({red, green, blue});
    if (spread == 0 || spread >= High) continue;
    const int luma = (38 * red + 75 * green + 15 * blue + 64) / 128;
    const int weight = weights[spread];
    const int neutral = (denominator - weight) * luma + denominator / 2;
    for (int channel = 0; channel < 3; ++channel) {
      rgba[i + channel] = static_cast<std::uint8_t>(
              (neutral + weight * rgba[i + channel]) / denominator);
    }
  }
}
}  // namespace

void ApplyGrayPreference(std::span<std::uint8_t> rgba, GrayPreference preference) {
  if (rgba.size() % 4 != 0) return;
  switch (preference) {
    case GrayPreference::kBalanced: Apply<8, 24>(rgba); break;
    case GrayPreference::kStronger: Apply<16, 40>(rgba); break;
    default: break;
  }
}
// Reduce the shared RGB component while keeping the strongest channel fixed.
// Fade in above the neutral range so faint tints and text remain untouched.
// This operates on source RGB, never on the vendor's CFA-mapped output.
void ApplyVividColor(std::span<std::uint8_t> rgba) {
  if (rgba.size() % 4 != 0) return;
  const auto bytes = static_cast<std::size_t>(rgba.size());
  for (std::size_t i = 0; i < bytes; i += 4) {
    const int maximum = std::max({rgba[i], rgba[i + 1], rgba[i + 2]});
    const int minimum = std::min({rgba[i], rgba[i + 1], rgba[i + 2]});
    const int spread = maximum - minimum;
    if (spread <= 24 || minimum == 0) continue;
    // At full strength remove one quarter of the common component. Scaling
    // by distance from the maximum preserves channel ratios around that peak.
    const int strength = std::min(spread - 24, 40);
    const int denominator = 160 * spread;
    for (int c = 0; c < 3; ++c) {
      const int reduction = (minimum * strength * (maximum - rgba[i + c]) +
                             denominator / 2) / denominator;
      rgba[i + c] = static_cast<std::uint8_t>(rgba[i + c] - reduction);
    }
  }
}
}  // namespace neo2::eink
