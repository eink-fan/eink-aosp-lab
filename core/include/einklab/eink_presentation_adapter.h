#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace einklab {

struct Rect {
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;

  [[nodiscard]] bool IsValid() const {
    return left >= 0 && top >= 0 && right > left && bottom > top;
  }
};

// A backend may derive a frame payload type. The scheduler retains this opaque
// reference until Submit returns.
class ComposeBuffer {
 public:
  virtual ~ComposeBuffer() = default;
};

// The worker retains this reference for the duration of a submission. Callers
// must not hand the scheduler borrowed pixel memory.
class GrayscaleBuffer {
 public:
  virtual ~GrayscaleBuffer() = default;
  [[nodiscard]] virtual const std::uint8_t* Data() const = 0;
  [[nodiscard]] virtual std::size_t ByteCount() const = 0;
};

struct Frame {
  std::uint64_t sequence = 0;
  // Optional monotonic timestamps supplied by a capture owner. They measure
  // queue acceptance only; neither one represents physical panel completion.
  std::chrono::steady_clock::time_point captured_at{};
  std::chrono::steady_clock::time_point enqueued_at{};
  std::shared_ptr<ComposeBuffer> compose_buffer;
  std::shared_ptr<ComposeBuffer> source_buffer;
  // Optional. A backend may convert the opaque source internally instead.
  std::shared_ptr<GrayscaleBuffer> grayscale_buffer;
  Rect dirty_rect;
  // Device-specific modes are deliberately represented only as an opaque hint.
  // This project does not assign or interpret its values.
  std::uint32_t presentation_hint = 0;
};

// The only hardware-specific seam. A staging backend may consume
// grayscale_buffer; another backend may consume an opaque source payload.
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

}  // namespace einklab
