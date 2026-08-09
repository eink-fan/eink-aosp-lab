#include "eink_micro_delta_policy.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace einklab {

namespace {

constexpr std::uint32_t kPpm = 1'000'000;

std::uint32_t RatioPpm(std::uint64_t numerator, std::uint64_t denominator) {
  if (denominator == 0) return 0;
  const auto value = (numerator * kPpm) / denominator;
  return static_cast<std::uint32_t>(std::min<std::uint64_t>(value, kPpm));
}

bool Intersects(const GrayscaleDelta& left, const GrayscaleDelta& right) {
  return left.left < right.right && right.left < left.right && left.top < right.bottom &&
          right.top < left.bottom;
}

bool EdgeMovedMaterially(const GrayscaleDelta& left, const GrayscaleDelta& right,
                         std::uint32_t limit) {
  const auto within_limit = [limit](int before, int after) {
    return std::abs(before - after) <= static_cast<int>(limit);
  };
  return !within_limit(left.left, right.left) || !within_limit(left.top, right.top) ||
          !within_limit(left.right, right.right) || !within_limit(left.bottom, right.bottom);
}

}  // namespace

MicroDeltaConfig MicroDeltaConfig::Defaults() {
  return {};
}

bool MicroDeltaConfig::IsValid() const {
  return changed_ppm >= 100 && changed_ppm <= 10'000 && bounding_box_ppm >= 100 &&
          bounding_box_ppm <= 25'000 && edge_band_ppm >= 10'000 && edge_band_ppm <= 250'000 &&
          (chrome_edge == MicroChromeEdge::kTop || chrome_edge == MicroChromeEdge::kBottom) &&
          material_move_pixels > 0 && settle >= std::chrono::milliseconds(100) &&
          settle <= std::chrono::seconds(1) && repeat >= std::chrono::milliseconds(500) &&
          repeat <= std::chrono::seconds(5);
}

MicroDeltaPolicy::MicroDeltaPolicy(MicroDeltaConfig config) : config_(config) {
  if (!config_.IsValid()) config_ = MicroDeltaConfig::Defaults();
}

bool MicroDeltaPolicy::Reconfigure(const MicroDeltaConfig& config) {
  if (!config.IsValid()) return false;
  const bool changed = config_.changed_ppm != config.changed_ppm ||
          config_.bounding_box_ppm != config.bounding_box_ppm ||
          config_.edge_band_ppm != config.edge_band_ppm ||
          config_.chrome_edge != config.chrome_edge ||
          config_.material_move_pixels != config.material_move_pixels ||
          config_.settle != config.settle || config_.repeat != config.repeat;
  if (changed) config_ = config;
  return changed;
}

bool MicroDeltaPolicy::IsMicro(const GrayscaleDelta& delta, int width, int height) const {
  if (width <= 0 || height <= 0 || delta.changed_pixels == 0 ||
      delta.total_pixels != static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height)) {
    return false;
  }
  const int box_width = delta.right - delta.left;
  const int box_height = delta.bottom - delta.top;
  if (box_width <= 0 || box_height <= 0 || delta.left < 0 || delta.top < 0 ||
      delta.right > width || delta.bottom > height) {
    return false;
  }
  const std::uint64_t box_area = static_cast<std::uint64_t>(box_width) * box_height;
  const std::uint64_t edge_distance = config_.chrome_edge == MicroChromeEdge::kTop
          ? static_cast<std::uint64_t>(delta.bottom)
          : static_cast<std::uint64_t>(height - delta.top);
  return delta.CoveragePpm() <= config_.changed_ppm &&
          RatioPpm(box_area, delta.total_pixels) <= config_.bounding_box_ppm &&
          RatioPpm(edge_distance, static_cast<std::uint64_t>(height)) <= config_.edge_band_ppm;
}

bool MicroDeltaPolicy::IsEquivalent(const Signature& previous, const GrayscaleDelta& current,
                                    int width, int height) const {
  return previous.width == width && previous.height == height && IsMicro(current, width, height) &&
          Intersects(previous.delta, current) &&
          !EdgeMovedMaterially(previous.delta, current, config_.material_move_pixels);
}

void MicroDeltaPolicy::ClearExpiredRepeat(std::chrono::steady_clock::time_point now) {
  if (!repeat_guard_ || !deadline_ || now < *deadline_) return;
  repeat_guard_ = false;
  deadline_.reset();
  signature_.reset();
}

MicroDeltaDecision MicroDeltaPolicy::Evaluate(const std::optional<GrayscaleDelta>& delta, int width,
                                              int height,
                                              std::chrono::steady_clock::time_point now) {
  ClearExpiredRepeat(now);
  if (!delta || !IsMicro(*delta, width, height)) {
    if (deferred_ || repeat_guard_) Reset();
    ++diagnostics_.substantive_bypass;
    return MicroDeltaDecision::kSubmitSubstantive;
  }

  if (!signature_) {
    signature_ = Signature{.delta = *delta, .width = width, .height = height};
    deadline_ = now + config_.settle;
    deferred_ = true;
    ++diagnostics_.held;
    return MicroDeltaDecision::kHeld;
  }

  if (!IsEquivalent(*signature_, *delta, width, height)) {
    Reset();
    ++diagnostics_.promoted;
    return MicroDeltaDecision::kSubmitPromoted;
  }

  signature_ = Signature{.delta = *delta, .width = width, .height = height};
  deferred_ = true;
  if (repeat_guard_) {
    ++diagnostics_.repeat_suppressed;
    return MicroDeltaDecision::kRepeatSuppressed;
  }
  ++diagnostics_.held;
  return MicroDeltaDecision::kHeld;
}

bool MicroDeltaPolicy::HasDeferredCandidate() const {
  return deferred_;
}

std::optional<std::chrono::steady_clock::time_point> MicroDeltaPolicy::Deadline() const {
  return deferred_ ? deadline_ : std::nullopt;
}

void MicroDeltaPolicy::MarkExpirySubmitted(std::chrono::steady_clock::time_point now) {
  if (!deferred_) return;
  deferred_ = false;
  repeat_guard_ = true;
  deadline_ = now + config_.repeat;
  ++diagnostics_.expiry_submitted;
}

void MicroDeltaPolicy::Reset() {
  signature_.reset();
  deadline_.reset();
  deferred_ = false;
  repeat_guard_ = false;
}

MicroDeltaPolicy::Diagnostics MicroDeltaPolicy::diagnostics() const {
  return diagnostics_;
}

}  // namespace einklab
