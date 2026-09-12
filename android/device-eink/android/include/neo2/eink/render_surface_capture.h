#pragma once

#include <android-base/unique_fd.h>
#include <renderengine/ExternalTexture.h>
#include <ui/Fence.h>
#include <ui/GraphicBuffer.h>

#include <eink_software_grayscale.h>

#include <chrono>
#include <cstdint>
#include <memory>

namespace neo2::eink::android {

// This owns references safe to pass from RenderSurface's composition thread to
// a worker. The fence fd is duplicated; queueBuffer retains its own fd. The
// strong GraphicBuffer reference prevents object destruction, but does *not*
// reserve the buffer's pixels against BufferQueue reuse after normal present.
// A worker-side CPU mapping is therefore an instrumentation probe until the
// target branch's producer/consumer lifetime proves it is a stable snapshot.
struct CapturedComposition {
  ::android::sp<::android::GraphicBuffer> compose_buffer;
  ::android::sp<::android::Fence> ready_fence;
  std::uint64_t buffer_id = 0;
  // Optional RAII lease for a pool-owned destination snapshot. The conversion
  // worker must let this go only after it has copied pixels into owned
  // grayscale storage (or discarded the capture). Normal RenderSurface
  // handoffs leave this empty because they do not own/reuse the display buffer.
  std::shared_ptr<void> ownership_lease;
  // R1 only: an owned grayscale copy made at the queueBuffer seam, before
  // this composed source may be reused. It never carries the source buffer
  // beyond that seam. The worker may compare it with its owned GPU snapshot
  // conversion or select it as the policy input, but may not map the source.
  std::shared_ptr<OwnedGrayscaleBuffer> direct_grayscale;
  bool direct_grayscale_selected = false;
  bool direct_grayscale_audit_requested = false;
  std::chrono::steady_clock::time_point captured_at = std::chrono::steady_clock::now();
};

class CaptureSink {
 public:
  virtual ~CaptureSink() = default;
  virtual void OnCapturedComposition(CapturedComposition composition) = 0;
};

class RenderSurfaceCapture {
 public:
  explicit RenderSurfaceCapture(CaptureSink& sink) : sink_(sink) {}

  RenderSurfaceCapture(const RenderSurfaceCapture&) = delete;
  RenderSurfaceCapture& operator=(const RenderSurfaceCapture&) = delete;

  // Non-blocking and safe on SurfaceFlinger's composition thread. The sink
  // must defer fence waiting, conversion, and vendor-engine submission. This
  // is a reference/fence handoff, not a reservation of pixel contents.
  void Capture(const std::shared_ptr<::android::renderengine::ExternalTexture>& texture,
               const ::android::base::unique_fd& ready_fence) const;

 private:
  CaptureSink& sink_;
};

}  // namespace neo2::eink::android
