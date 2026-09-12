#include "neo2_panel_geometry_gate.h"

namespace neo2::eink {

EinkPanelGeometryGate::EinkPanelGeometryGate(std::uint32_t expected_width,
                                             std::uint32_t expected_height) {
  const auto* profile = FindEinkDeviceProfile(expected_width, expected_height);
  if (profile != nullptr) {
    expected_width_ = profile->panel_width;
    expected_height_ = profile->panel_height;
  }
}

PanelGeometryStatus EinkPanelGeometryGate::Observe(std::uint32_t width, std::uint32_t height) {
  ++sample_count_;
  if (!HasRecognizedProfile() || width != expected_width_ || height != expected_height_) {
    ++mismatch_count_;
    status_ = PanelGeometryStatus::kRejected;
    return status_;
  }
  if (status_ == PanelGeometryStatus::kUnverified) {
    status_ = PanelGeometryStatus::kVerified;
  }
  return status_;
}

}  // namespace neo2::eink
