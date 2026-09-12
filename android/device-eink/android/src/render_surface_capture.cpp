#include "neo2/eink/render_surface_capture.h"

#include <unistd.h>

#include <utility>

namespace neo2::eink::android {

void RenderSurfaceCapture::Capture(
        const std::shared_ptr<::android::renderengine::ExternalTexture>& texture,
        const ::android::base::unique_fd& ready_fence) const {
  if (!texture || !texture->getBuffer()) {
    return;
  }

  ::android::sp<::android::Fence> copied_fence = ::android::Fence::NO_FENCE;
  if (ready_fence.get() >= 0) {
    copied_fence = ::android::sp<::android::Fence>::make(dup(ready_fence.get()));
  }

  CapturedComposition composition;
  composition.compose_buffer = texture->getBuffer();
  composition.ready_fence = std::move(copied_fence);
  composition.buffer_id = texture->getId();
  sink_.OnCapturedComposition(std::move(composition));
}

}  // namespace neo2::eink::android
