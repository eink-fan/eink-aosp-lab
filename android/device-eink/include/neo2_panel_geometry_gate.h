#pragma once

#include "eink_device_profile.h"

#include <cstdint>

namespace neo2::eink {

inline constexpr std::uint32_t kNeo2PanelWidth = kNeo2DeviceProfile.panel_width;
inline constexpr std::uint32_t kNeo2PanelHeight = kNeo2DeviceProfile.panel_height;

// The first same-version RenderSurface proof must establish that it is seeing
// the stock panel-native target. A mismatch is sticky for the session: later
// matching frames must not accidentally enable a pixel/backend path on a
// target whose transform was not understood.
enum class PanelGeometryStatus {
  kUnverified,
  kVerified,
  kRejected,
};

class EinkPanelGeometryGate final {
 public:
  explicit EinkPanelGeometryGate(const EinkDeviceProfile& profile = BuildEinkDeviceProfile())
      : expected_width_(profile.panel_width), expected_height_(profile.panel_height) {}
  EinkPanelGeometryGate(std::uint32_t expected_width, std::uint32_t expected_height);

  [[nodiscard]] PanelGeometryStatus Observe(std::uint32_t width, std::uint32_t height);

  [[nodiscard]] PanelGeometryStatus Status() const { return status_; }
  [[nodiscard]] bool IsVerified() const { return status_ == PanelGeometryStatus::kVerified; }
  [[nodiscard]] std::uint64_t SampleCount() const { return sample_count_; }
  [[nodiscard]] std::uint64_t MismatchCount() const { return mismatch_count_; }
  [[nodiscard]] std::uint32_t ExpectedWidth() const { return expected_width_; }
  [[nodiscard]] std::uint32_t ExpectedHeight() const { return expected_height_; }
  [[nodiscard]] bool HasRecognizedProfile() const {
    return expected_width_ != 0 && expected_height_ != 0;
  }

 private:
  std::uint32_t expected_width_ = 0;
  std::uint32_t expected_height_ = 0;
  PanelGeometryStatus status_ = PanelGeometryStatus::kUnverified;
  std::uint64_t sample_count_ = 0;
  std::uint64_t mismatch_count_ = 0;
};

// Source compatibility for B038 callers. New code should use the neutral
// class name because the same gate now recognizes the Ocean build profile.
using Neo2PanelGeometryGate = EinkPanelGeometryGate;

}  // namespace neo2::eink
