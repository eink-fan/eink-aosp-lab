#include "neo2_panel_geometry_gate.h"

namespace neo2::eink {

PanelGeometryStatus Neo2PanelGeometryGate::Observe(std::uint32_t width, std::uint32_t height) {
  ++sample_count_;
  if (width != kNeo2PanelWidth || height != kNeo2PanelHeight) {
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
