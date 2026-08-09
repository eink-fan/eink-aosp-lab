#include "neo2/eink/gpu_snapshot_pool.h"

#include <renderengine/DisplaySettings.h>
#include <renderengine/LayerSettings.h>
#include <renderengine/RenderEngine.h>
#include <renderengine/impl/ExternalTexture.h>
#include <ui/PixelFormat.h>

#include <algorithm>
#include <mutex>
#include <unistd.h>
#include <utility>

namespace neo2::eink::android {

struct GpuSnapshotPool::Slot {
  Slot(std::shared_ptr<::android::renderengine::ExternalTexture> texture_in,
       ::android::sp<::android::GraphicBuffer> buffer_in)
      : texture(std::move(texture_in)), buffer(std::move(buffer_in)) {}

  std::shared_ptr<::android::renderengine::ExternalTexture> texture;
  ::android::sp<::android::GraphicBuffer> buffer;
  SlotState state = SlotState::kFree;
  std::uint64_t generation = 0;
};

std::unique_ptr<GpuSnapshotPool> GpuSnapshotPool::Create(
        ::android::renderengine::RenderEngine& render_engine, std::uint32_t width,
        std::uint32_t height, std::size_t slot_count) {
  if (width == 0 || height == 0 || slot_count == 0) {
    return nullptr;
  }

  constexpr std::uint64_t kUsage = GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE |
          GRALLOC_USAGE_SW_READ_OFTEN;
  std::vector<Slot> slots;
  slots.reserve(slot_count);
  for (std::size_t index = 0; index < slot_count; ++index) {
    auto buffer = ::android::sp<::android::GraphicBuffer>::make(
            width, height, HAL_PIXEL_FORMAT_RGBA_8888, 1u, kUsage,
            "Neo2EinkSnapshotPool");
    if (buffer == nullptr || buffer->initCheck() != ::android::NO_ERROR) {
      return nullptr;
    }
    auto texture = std::make_shared<::android::renderengine::impl::ExternalTexture>(
            buffer, render_engine,
            ::android::renderengine::impl::ExternalTexture::Usage::WRITEABLE);
    slots.emplace_back(std::move(texture), std::move(buffer));
  }
  return std::unique_ptr<GpuSnapshotPool>(
          new GpuSnapshotPool(render_engine, std::move(slots)));
}

GpuSnapshotPool::GpuSnapshotPool(::android::renderengine::RenderEngine& render_engine,
                                 std::vector<Slot> slots)
    : render_engine_(render_engine), slots_(std::move(slots)) {}

GpuSnapshotPool::~GpuSnapshotPool() = default;

std::optional<GpuSnapshotPool::Snapshot> GpuSnapshotPool::TryCopy(
        const std::shared_ptr<::android::renderengine::ExternalTexture>& source,
        const ::android::base::unique_fd& source_ready_fence) {
  if (source == nullptr || source->getWidth() == 0 || source->getHeight() == 0 ||
      (source->getBuffer()->getUsage() & GRALLOC_USAGE_PROTECTED) != 0) {
    std::lock_guard lock(mutex_);
    ++failed_submissions_;
    return std::nullopt;
  }

  std::size_t slot_index = 0;
  std::uint64_t generation = 0;
  std::shared_ptr<::android::renderengine::ExternalTexture> destination;
  ::android::sp<::android::GraphicBuffer> destination_buffer;
  {
    std::lock_guard lock(mutex_);
    const auto free_slot = std::find_if(slots_.begin(), slots_.end(), [](const Slot& slot) {
      return slot.state == SlotState::kFree;
    });
    if (free_slot == slots_.end()) {
      ++drops_;
      return std::nullopt;
    }
    slot_index = static_cast<std::size_t>(std::distance(slots_.begin(), free_slot));
    free_slot->state = SlotState::kCopying;
    generation = ++free_slot->generation;
    destination = free_slot->texture;
    destination_buffer = free_slot->buffer;
  }

  const auto width = source->getWidth();
  const auto height = source->getHeight();
  if (destination->getWidth() != width || destination->getHeight() != height) {
    std::lock_guard lock(mutex_);
    Slot& slot = slots_[slot_index];
    if (slot.state == SlotState::kCopying && slot.generation == generation) {
      slot.state = SlotState::kFree;
    }
    ++failed_submissions_;
    return std::nullopt;
  }

  const ::android::Rect display_rect(0, 0, static_cast<int32_t>(width),
                                     static_cast<int32_t>(height));
  const ::android::renderengine::DisplaySettings display{
          .namePlusId = "Neo2EinkSnapshot",
          .physicalDisplay = display_rect,
          .clip = display_rect,
          .maxLuminance = 500.0f,
  };
  ::android::sp<::android::Fence> source_fence = ::android::Fence::NO_FENCE;
  if (source_ready_fence.get() >= 0) {
    source_fence = ::android::sp<::android::Fence>::make(dup(source_ready_fence.get()));
  }
  const ::android::renderengine::LayerSettings layer{
          .geometry = {.boundaries = ::android::FloatRect(
                               0.0f, 0.0f, static_cast<float>(width),
                               static_cast<float>(height))},
          .source = {.buffer = {.buffer = source,
                                .fence = std::move(source_fence),
                                .useTextureFiltering = false,
                                .usePremultipliedAlpha = true,
                                .isOpaque = true}},
          .alpha = ::android::half(1.0f),
          .sourceDataspace = ::android::ui::Dataspace::UNKNOWN,
          .disableBlending = true,
          .name = "Neo2EinkSnapshotCopy",
  };

  // get() obtains the output fence handle. It does not wait for that fence;
  // the conversion worker does so via lockAsync after this function returns.
  const auto fence_result = render_engine_
                                    .drawLayers(display, {layer}, destination,
                                                ::android::base::unique_fd())
                                    .get();
  if (!fence_result.ok()) {
    std::lock_guard lock(mutex_);
    Slot& slot = slots_[slot_index];
    if (slot.state == SlotState::kCopying && slot.generation == generation) {
      slot.state = SlotState::kFree;
    }
    ++failed_submissions_;
    return std::nullopt;
  }

  {
    std::lock_guard lock(mutex_);
    Slot& slot = slots_[slot_index];
    if (slot.state != SlotState::kCopying || slot.generation != generation) {
      ++failed_submissions_;
      return std::nullopt;
    }
    slot.state = SlotState::kConverting;
  }
  return Snapshot{.slot = slot_index,
                  .generation = generation,
                  .buffer = std::move(destination_buffer),
                  .ready_fence = fence_result.value()};
}

bool GpuSnapshotPool::ReleaseAfterConversion(const Snapshot& snapshot) {
  std::lock_guard lock(mutex_);
  if (snapshot.slot >= slots_.size()) {
    return false;
  }
  Slot& slot = slots_[snapshot.slot];
  if (slot.state != SlotState::kConverting || slot.generation != snapshot.generation) {
    return false;
  }
  slot.state = SlotState::kFree;
  return true;
}

std::size_t GpuSnapshotPool::capacity() const {
  std::lock_guard lock(mutex_);
  return slots_.size();
}

std::size_t GpuSnapshotPool::busy_count() const {
  std::lock_guard lock(mutex_);
  return static_cast<std::size_t>(std::count_if(
          slots_.begin(), slots_.end(),
          [](const Slot& slot) { return slot.state != SlotState::kFree; }));
}

std::uint64_t GpuSnapshotPool::drop_count() const {
  std::lock_guard lock(mutex_);
  return drops_;
}

std::uint64_t GpuSnapshotPool::failed_submission_count() const {
  std::lock_guard lock(mutex_);
  return failed_submissions_;
}

}  // namespace neo2::eink::android
