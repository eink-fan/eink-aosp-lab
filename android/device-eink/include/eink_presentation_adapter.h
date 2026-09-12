#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace neo2::eink {

// Values observed in android.eink.api.EinkMode on stock Neo 2. These are
// vendor engine mode values, not Rockchip EPD ioctl values.
enum class RefreshMode : int {
  // Stock IIreaderManager reports 0 for the inherited/automatic default.
  kAutomatic = 0,
  kDuPartial = 1,
  kGc16Partial = 2,
  kGl16Partial = 3,
  kGlr16Partial = 4,
  kAnimationPartial = 6,
  kAutoPartial = 15,
  kFullUpdate = 32,
};

constexpr int ToEngineMode(RefreshMode mode) {
  return static_cast<int>(mode);
}

struct Rect {
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;

  [[nodiscard]] bool IsValid() const {
    return left >= 0 && top >= 0 && right > left && bottom > top;
  }
};

// Android-specific backends may keep an sp<GraphicBuffer> in a derived
// object. The scheduler owns this opaque reference until Submit returns.
class ComposeBuffer {
 public:
  virtual ~ComposeBuffer() = default;

  // Android's production SurfaceFlinger target is built without RTTI. A
  // backend that needs a platform-derived ComposeBuffer must therefore verify
  // an explicit type tag before using static_pointer_cast; zero is reserved for
  // portable/unknown buffers and fails closed in platform-specific backends.
  [[nodiscard]] virtual std::uint32_t TypeTag() const { return 0; }
};

// The worker retains this reference until the engine submission is complete.
// Android backends can use an allocated staging buffer or a vendor-owned
// grayscale mapping; callers must not hand the scheduler a borrowed pointer.
class GrayscaleBuffer {
 public:
  virtual ~GrayscaleBuffer() = default;
  [[nodiscard]] virtual const std::uint8_t* Data() const = 0;
  [[nodiscard]] virtual std::size_t ByteCount() const = 0;
};

enum class DiagnosticRefresh : std::uint8_t { kNone, kDifferential, kFull, kWhiteThenTarget, kColorCleanup };

struct Frame {
  std::uint64_t sequence = 0;
  // Nonzero only for a lifecycle-driven sleep-image submission. It allows an
  // aborted OFF intent to withdraw that exact pending candidate without
  // disturbing ordinary presentation work.
  std::uint64_t sleep_image_epoch = 0;
  // Optional monotonic timestamps supplied by a capture owner. They measure
  // queue acceptance only; neither one represents physical panel completion.
  std::chrono::steady_clock::time_point captured_at{};
  std::chrono::steady_clock::time_point enqueued_at{};
  std::shared_ptr<ComposeBuffer> compose_buffer;
  std::shared_ptr<ComposeBuffer> source_buffer;
  // Optional. The generic CPU-staging path supplies it. The observed Neo
  // IdisplayEpdcDeviceSync backend instead owns conversion internally from
  // compose_buffer + its captured fence, so it must be allowed to omit this.
  std::shared_ptr<GrayscaleBuffer> grayscale_buffer;
  // Owned, composited RGBA only for a color sleep-image frame.
  std::shared_ptr<const std::vector<std::uint8_t>> sleep_image_rgba;
  Rect dirty_rect;
  int engine_mode = ToEngineMode(RefreshMode::kGl16Partial);
  int policy_flag = 0;
  bool handwriting = false;
  DiagnosticRefresh diagnostic_refresh = DiagnosticRefresh::kNone;
  std::uint64_t diagnostic_request = 0;
};

// The only hardware-specific seam. A staging backend consumes grayscale_buffer;
// a vendor-wrapper backend may consume only compose_buffer plus a captured
// acquire fence carried by its Android-derived ComposeBuffer.
class Engine {
 public:
  virtual ~Engine() = default;
  virtual bool Submit(const Frame& frame) = 0;
};

class PresentationAdapter {
 public:
  struct Diagnostics {
    std::uint64_t enqueues_accepted = 0;
    std::uint64_t pending_replaced = 0;
    std::uint64_t pending_sleep_images_cancelled = 0;
    std::uint64_t trailing_deferred = 0;
    std::uint64_t trailing_submitted = 0;
    std::uint64_t submit_calls = 0;
    std::uint64_t submit_rejected = 0;
  };

  // A queue-acceptance scheduling view for capture admission. It deliberately
  // says nothing about physical panel completion or safe buffer reuse.
  struct OutputSlotState {
    bool has_retained_candidate = false;
    std::optional<std::chrono::steady_clock::time_point> next_eligible;
  };

  // Runs on the serial engine worker after Submit returns. The duration ends
  // at the engine's return/queue-acceptance boundary, never panel completion.
  using SubmissionObserver = std::function<void(const Frame&, bool, std::chrono::steady_clock::duration)>;
  // Limits panel submission, not capture. A pending changed frame is retained
  // and submitted at the next eligible time even if no later UI interaction
  // produces another frame. The default has no limit.
  using SubmissionIntervalProvider = std::function<std::chrono::steady_clock::duration()>;

  explicit PresentationAdapter(Engine& engine, SubmissionObserver observer = {},
                               SubmissionIntervalProvider interval_provider = {});
  ~PresentationAdapter();

  PresentationAdapter(const PresentationAdapter&) = delete;
  PresentationAdapter& operator=(const PresentationAdapter&) = delete;

  void Start();
  void Stop();

  // Returns false only after Stop. If a frame is already pending, it is
  // replaced by this newer frame; an in-flight submission is never cancelled.
  bool Enqueue(Frame frame);

  // Withdraws only a not-yet-submitted lifecycle candidate for this epoch.
  // An in-flight engine submission is never interrupted.
  bool CancelPendingSleepImage(std::uint64_t epoch);
  bool CancelPendingDiagnosticRefresh();

  [[nodiscard]] Diagnostics diagnostics() const;
  [[nodiscard]] OutputSlotState output_slot_state() const;

 private:
  void WorkerMain();

  Engine& engine_;
  SubmissionObserver submission_observer_;
  SubmissionIntervalProvider interval_provider_;
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace neo2::eink
