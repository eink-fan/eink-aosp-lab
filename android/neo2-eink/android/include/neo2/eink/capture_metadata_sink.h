#pragma once

#include "neo2_panel_geometry_gate.h"
#include "neo2/eink/render_surface_capture.h"

#include <cstdint>
#include <mutex>
#include <optional>

namespace neo2::eink::android {

// Bounded, no-pixel-copy record for the first RenderSurface hook proof.
struct CaptureMetadata {
  std::uint64_t buffer_id = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::int32_t pixel_format = 0;
  PanelGeometryStatus geometry_status = PanelGeometryStatus::kUnverified;
};

// This sink deliberately drops the GraphicBuffer and fence as soon as it has
// recorded metadata. It cannot map, retain, or submit pixel content.
class CaptureMetadataSink final : public CaptureSink {
 public:
  void OnCapturedComposition(CapturedComposition composition) override;

  [[nodiscard]] std::uint64_t CaptureCount() const;
  [[nodiscard]] std::uint64_t MismatchedCaptureCount() const;
  [[nodiscard]] PanelGeometryStatus GeometryStatus() const;
  [[nodiscard]] bool IsPanelGeometryVerified() const;
  [[nodiscard]] std::optional<CaptureMetadata> LatestCapture() const;

 private:
  mutable std::mutex mutex_;
  Neo2PanelGeometryGate geometry_gate_;
  std::optional<CaptureMetadata> latest_capture_;
};

}  // namespace neo2::eink::android
