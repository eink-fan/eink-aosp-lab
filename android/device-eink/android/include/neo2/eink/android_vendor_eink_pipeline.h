#pragma once

#include "neo2/eink/render_surface_capture.h"

#include <eink_presentation_adapter.h>

#include <cstdint>
#include <memory>

namespace neo2::eink::android {

// M4-only direct-vendor pipeline. It accepts only an owned GPU snapshot and
// passes its GraphicBuffer/fence unchanged to a serial Engine. It never maps
// pixels, initializes a controller, or arms the one-shot gate. The M4 graft
// must keep it unconstructed until the separately approved output path.
class AndroidVendorEinkPipeline final : public CaptureSink {
 public:
  struct Diagnostics {
    std::uint64_t accepted = 0;
    std::uint64_t coalesced = 0;
    std::uint64_t rejected = 0;
    std::uint64_t enqueued = 0;
  };

  explicit AndroidVendorEinkPipeline(Engine& engine);
  ~AndroidVendorEinkPipeline() override;

  AndroidVendorEinkPipeline(const AndroidVendorEinkPipeline&) = delete;
  AndroidVendorEinkPipeline& operator=(const AndroidVendorEinkPipeline&) = delete;

  void Start();
  void Stop();
  void SetEnabled(bool enabled);
  void OnCapturedComposition(CapturedComposition composition) override;
  [[nodiscard]] Diagnostics diagnostics() const;

 private:
  void WorkerMain();

  Engine& engine_;
  PresentationAdapter adapter_;
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace neo2::eink::android