#include "neo2/eink/android_grayscale_converter.h"

#include <ui/PixelFormat.h>

namespace neo2::eink::android {

std::shared_ptr<OwnedGrayscaleBuffer> AndroidGrayscaleConverter::Convert(
        const CapturedComposition& composition, Rotation rotation) const {
  const auto& buffer = composition.compose_buffer;
  if (!buffer || (buffer->getUsage() & ::android::GraphicBuffer::USAGE_PROTECTED) != 0) {
    return nullptr;
  }

  RgbaLayout layout;
  switch (buffer->getPixelFormat()) {
    case ::android::PIXEL_FORMAT_RGBA_8888:
    case ::android::PIXEL_FORMAT_RGBX_8888:
      layout = RgbaLayout::kRgba8888;
      break;
    case ::android::PIXEL_FORMAT_BGRA_8888:
      layout = RgbaLayout::kBgra8888;
      break;
    default:
      return nullptr;
  }

  void* address = nullptr;
  int32_t bytes_per_pixel = -1;
  int32_t bytes_per_stride = -1;
  const int acquire_fence_fd = composition.ready_fence ? composition.ready_fence->dup() : -1;
  const auto status = buffer->lockAsync(::android::GraphicBuffer::USAGE_SW_READ_OFTEN,
                                        &address, acquire_fence_fd, &bytes_per_pixel,
                                        &bytes_per_stride);
  if (status != ::android::NO_ERROR || address == nullptr || bytes_per_pixel != 4 ||
      bytes_per_stride <= 0 || bytes_per_stride % bytes_per_pixel != 0) {
    if (status == ::android::NO_ERROR && address != nullptr) {
      static_cast<void>(buffer->unlock());
    }
    return nullptr;
  }

  const RgbaSource source{
          .pixels = static_cast<const std::uint8_t*>(address),
          .width = static_cast<int>(buffer->getWidth()),
          .height = static_cast<int>(buffer->getHeight()),
          .stride_pixels = bytes_per_stride / bytes_per_pixel,
          .layout = layout,
  };
  const auto grayscale = ConvertToGrayscale(source, rotation);
  // This is CPU read-only use. The mapper handles any implementation-specific
  // unlock synchronization; no fence is exported from this capture-only path.
  // A strong GraphicBuffer ref does not itself prevent BufferQueue reuse, so
  // callers must treat this as diagnostic data until the target display path's
  // post-queue buffer lifetime has been measured/proven.
  static_cast<void>(buffer->unlock());
  return grayscale;
}

AndroidGrayscaleConverter::BoundedConversion AndroidGrayscaleConverter::ConvertBeforeQueue(
        const CapturedComposition& composition, Rotation rotation,
        std::chrono::milliseconds fence_wait_limit) const {
  if (composition.ready_fence &&
      composition.ready_fence->wait(static_cast<int>(fence_wait_limit.count())) != 0) {
    return {.status = BoundedConversionStatus::kFenceNotReady};
  }
  // The wait above consumes the only readiness dependency. Do not pass the
  // source fence a second time to lockAsync; this remains a synchronous copy
  // at RenderSurface's queueBuffer seam and the local strong buffer reference
  // dies as this method returns.
  CapturedComposition ready = composition;
  ready.ready_fence = ::android::Fence::NO_FENCE;
  auto grayscale = Convert(ready, rotation);
  if (!grayscale) return {.status = BoundedConversionStatus::kConversionFailed};
  return {.status = BoundedConversionStatus::kSuccess, .grayscale = std::move(grayscale)};
}

}  // namespace neo2::eink::android
