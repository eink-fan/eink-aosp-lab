#pragma once

#include "neo2/eink/capture_metadata_sink.h"
#include "neo2/eink/eink_sleep_image_catalog.h"
#include "neo2/eink/render_surface_capture.h"

#include <renderengine/RenderEngine.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace neo2::eink::android {

// Compact, redacted process-local counters for the device collector. This is
// intentionally independent of logcat's rolling buffer and never exposes
// pixels, calibration data, endpoint paths, or requested/resolved light codes.
[[nodiscard]] std::string GetNeo2EinkDiagnosticsSnapshot();

// SurfaceFlinger-owned, metadata-only session for the first same-version
// integration. It neither starts workers nor maps pixels, so no vendor e-ink
// library, DRM node, controller state, or visible panel submission can enter
// through this object.
class RenderSurfaceCaptureSession final {
 public:
  using CompositeRequest = std::function<bool()>;

  RenderSurfaceCaptureSession();
  // M3 constructor. The caller is RenderSurface, which already owns the only
  // permitted RenderEngine caller context for TryCopy().
  RenderSurfaceCaptureSession(::android::renderengine::RenderEngine& render_engine,
                              std::uint32_t width, std::uint32_t height,
                              CompositeRequest request_composite = {});
  ~RenderSurfaceCaptureSession();

  RenderSurfaceCaptureSession(const RenderSurfaceCaptureSession&) = delete;
  RenderSurfaceCaptureSession& operator=(const RenderSurfaceCaptureSession&) = delete;

  // SurfaceFlinger lifecycle calls these. Start enables only metadata records;
  // it starts no thread and does not retain frames after Capture returns.
  void StartMetadataOnly();
  void Stop();

  // SurfaceFlinger's future private endpoint calls these from its Binder
  // thread. They retain only copied, prevalidated panel-gray bytes and never
  // perform a Binder request, file operation, or image decode on Capture().
  [[nodiscard]] EinkSleepImageCatalog::PublishResult PublishSleepImageCatalog(
          std::uint64_t epoch, int width, int height,
          std::vector<EinkSleepImageCatalog::Entry> entries, std::size_t selected_index);
  void SetScreenOffEpoch(std::uint64_t epoch);
  void SetScreenOnEpoch(std::uint64_t epoch);

  // Safe at the RenderSurface queueBuffer seam. It passes the texture through
  // a synchronous metadata sink and does not map or retain pixel content.
  void Capture(const std::shared_ptr<::android::renderengine::ExternalTexture>& texture,
               const ::android::base::unique_fd& ready_fence);

  [[nodiscard]] const CaptureMetadataSink& sink_for_diagnostics() const { return sink_; }
  // This is deliberately only a fail-closed future-pixel-work gate. The
  // metadata-only session has no pixel path to enable.
  [[nodiscard]] bool PixelWorkPermitted() const {
    return started_ && sink_.IsPanelGeometryVerified();
  }

 private:
  void MaybeStartM3();
  void CaptureM3(const std::shared_ptr<::android::renderengine::ExternalTexture>& texture,
                 const ::android::base::unique_fd& ready_fence);
  void MaybeLogM3Diagnostics();

  CaptureMetadataSink sink_;
  RenderSurfaceCapture capture_;
  ::android::renderengine::RenderEngine* render_engine_ = nullptr;
  std::uint32_t expected_width_ = 0;
  std::uint32_t expected_height_ = 0;
  CompositeRequest request_composite_;
  class M3State;
  std::unique_ptr<M3State> m3_;
  bool started_ = false;
  PanelGeometryStatus reported_geometry_status_ = PanelGeometryStatus::kUnverified;
};

}  // namespace neo2::eink::android
