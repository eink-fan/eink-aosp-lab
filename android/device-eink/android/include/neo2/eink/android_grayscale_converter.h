#pragma once

#include "neo2/eink/render_surface_capture.h"

#include <eink_software_grayscale.h>

#include <memory>

#include <chrono>

namespace neo2::eink::android {

// Worker-only CPU fallback. It consumes a completed captured composition,
// obtains a temporary software-read mapping, and returns owned grayscale bytes.
// Protected and unsupported pixel formats intentionally return nullptr.
class AndroidGrayscaleConverter {
 public:
  enum class BoundedConversionStatus : std::uint8_t {
    kSuccess,
    kFenceNotReady,
    kConversionFailed,
  };

  struct BoundedConversion {
    BoundedConversionStatus status = BoundedConversionStatus::kConversionFailed;
    std::shared_ptr<OwnedGrayscaleBuffer> grayscale;
  };

  [[nodiscard]] std::shared_ptr<OwnedGrayscaleBuffer> Convert(
          const CapturedComposition& composition, Rotation rotation,
          std::uint64_t* rgb_signature = nullptr, RgbFrameTracker* rgb_tracker = nullptr) const;

  // Composition-thread-only R1 source copy. It consumes the supplied fence
  // within a finite bound, maps/copies before queueBuffer may recycle the
  // source, and returns only owned grayscale bytes. It is intentionally not a
  // general worker conversion path.
  [[nodiscard]] BoundedConversion ConvertBeforeQueue(
          const CapturedComposition& composition, Rotation rotation,
          std::chrono::milliseconds fence_wait_limit) const;
};

}  // namespace neo2::eink::android
