#pragma once

#include <android-base/unique_fd.h>
#include <renderengine/ExternalTexture.h>
#include <ui/Fence.h>
#include <ui/GraphicBuffer.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace android::renderengine {
class RenderEngine;
}

namespace neo2::eink::android {

// Owns a small, fixed set of GPU-copy destinations for an eventual e-ink
// capture path. TryCopy() is for RenderSurface's existing RenderEngine caller
// only; it never waits for GPU completion. A worker receives the returned
// Snapshot and uses ready_fence with GraphicBuffer::lockAsync().
//
// This class is deliberately not constructed by the metadata-only session and
// is not part of the currently graftable path. It is an Android-14-specific
// implementation of the portable SnapshotPool contract.
class GpuSnapshotPool final {
 public:
  struct Snapshot {
    std::size_t slot = 0;
    std::uint64_t generation = 0;
    ::android::sp<::android::GraphicBuffer> buffer;
    ::android::sp<::android::Fence> ready_fence;
  };

  // Returns nullptr when allocation fails or a zero-sized/empty pool is
  // requested. Each destination is RGBA8888 and simultaneously GPU-renderable,
  // GPU-texture-readable, and CPU-readable after its draw fence fires.
  [[nodiscard]] static std::unique_ptr<GpuSnapshotPool> Create(
          ::android::renderengine::RenderEngine& render_engine, std::uint32_t width,
          std::uint32_t height, std::size_t slot_count);

  ~GpuSnapshotPool();

  GpuSnapshotPool(const GpuSnapshotPool&) = delete;
  GpuSnapshotPool& operator=(const GpuSnapshotPool&) = delete;

  // Issues one full-frame source-to-owned-buffer GPU copy. source_ready_fence
  // is duplicated into LayerSettings so RenderEngine does not bind the source
  // before its producer is finished. A full pool drops the capture rather than
  // blocking the composition thread. Calling this from any additional
  // RenderEngine caller would violate RenderEngine's single-caller contract.
  [[nodiscard]] std::optional<Snapshot> TryCopy(
          const std::shared_ptr<::android::renderengine::ExternalTexture>& source,
          const ::android::base::unique_fd& source_ready_fence);

  // Marks a converted (or discarded) snapshot reusable. A stale worker lease
  // cannot release a newer capture because generation must still match.
  [[nodiscard]] bool ReleaseAfterConversion(const Snapshot& snapshot);

  [[nodiscard]] std::size_t capacity() const;
  [[nodiscard]] std::size_t busy_count() const;
  [[nodiscard]] std::uint64_t drop_count() const;
  [[nodiscard]] std::uint64_t failed_submission_count() const;

 private:
  enum class SlotState { kFree, kCopying, kConverting };

  struct Slot;

  GpuSnapshotPool(::android::renderengine::RenderEngine& render_engine,
                  std::vector<Slot> slots);

  ::android::renderengine::RenderEngine& render_engine_;
  std::vector<Slot> slots_;
  mutable std::mutex mutex_;
  std::uint64_t drops_ = 0;
  std::uint64_t failed_submissions_ = 0;
};

}  // namespace neo2::eink::android
