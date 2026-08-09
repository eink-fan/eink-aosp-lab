#pragma once

#include <eink_presentation_adapter.h>
#include <eink_submission_observer.h>

#include <chrono>
#include <cstdint>
#include <memory>

namespace neo2::eink::android {

// RM06L Android-14-only representation of the *lower* retained engine's
// observed full-frame handoff. This is not a portable vendor API. The module
// that implements it is deliberately unreferenced by the M1-M3 capture graft.
// Its output-sensitive transitions are explicit so an ordinary M3 capture
// cannot accidentally turn into an engine call.
class LowerEngineDirectBackend final : public Engine {
 public:
  enum class State : std::uint8_t {
    // Construction has no side effects. This is the only state reachable in
    // the current M1-M3 capture-only image.
    kStopped,
    // The exact same-build library was loaded and its two required symbols
    // resolved, but iDisplayEngine_init has not been called.
    kResolved,
    // iDisplayEngine_init returned zero. The retained engine is process-
    // lifetime state from here onward: it must never be reinitialized,
    // deinitialized, or dlclosed. The M4-only executor defaults to a
    // rate-limited continuous guest loop; an operator may restore its finite
    // startup-frame budget from this state.
    kInitialized,
    // The finite startup-frame budget was consumed. This terminal state does
    // not claim physical waveform completion; it prevents further buffer use
    // when continuous guest mode has been explicitly disabled.
    kSubmitted,
    // Any failed preflight, loader, initializer, or update call is terminal.
    kPoisoned,
  };

  enum PreflightFailure : std::uint32_t {
    kNoPreflightFailure = 0,
    kWaveformReadUnavailable = 1U << 0,
    kSwtconAccessUnavailable = 1U << 1,
    kZhangyueAccessUnavailable = 1U << 2,
    kPixFormatWriteUnavailable = 1U << 3,
    kUnexpectedBridgeProperty = 1U << 4,
    kVcomReadUnavailable = 1U << 5,
    kTemperatureInfoWriteUnavailable = 1U << 6,
    kWaveformStorageReadUnavailable = 1U << 7,
  };

  struct Diagnostics {
    State state = State::kStopped;
    std::uint64_t rejected_frames = 0;
    std::uint64_t disarmed_frames = 0;
    std::uint64_t submissions = 0;
    std::uint64_t full_control_submissions = 0;
    std::uint64_t differential_submissions = 0;
    std::uint64_t differential_failures = 0;
    std::uint64_t idle_queries = 0;
    std::uint64_t idle_busy_retries = 0;
    std::uint64_t idle_ready = 0;
    std::uint64_t idle_timeouts = 0;
    std::uint32_t preflight_failures = kNoPreflightFailure;
    std::uint64_t completion_unknown_submissions = 0;
    std::uint64_t completion_timeout_observations = 0;
    std::uint64_t pending_submission_id = 0;
    std::uint64_t pending_timeout_elapsed_ns = 0;
    bool pending_completion_unknown = false;
    int initializer_result = 0;
    int update_result = 0;
  };

  LowerEngineDirectBackend();
  ~LowerEngineDirectBackend() override;

  LowerEngineDirectBackend(const LowerEngineDirectBackend&) = delete;
  LowerEngineDirectBackend& operator=(const LowerEngineDirectBackend&) = delete;

  // Output-sensitive, one-time process-lifetime transition. It checks the
  // exact waveform/controller/pix-format prerequisites and then calls only
  // iDisplayEngine_init(). It is intentionally not reachable from the M1-M3
  // capture graft. A nonzero initializer result poisons this instance.
  [[nodiscard]] bool InitializeForOneShot();

  // Requires InitializeForOneShot() and exactly one observed ed060kc1 raw
  // grayscale full-input envelope: either the known full control (`0x22`) or
  // the normal engine-differential cursor envelope (`0x4`). The direct lower
  // engine deep-copies the bytes before returning. Both normal routes report
  // `-3` for queue acceptance. A return from this function is queue
  // acceptance, not physical panel completion. By default the M4-only caller
  // is continuous;
  // setting its temporary-guest property false restores a live budget clamped
  // to a hard process maximum. This backend always enforces a 100-ms--5-second
  // property-controlled minimum separation.
  [[nodiscard]] bool Submit(const Frame& frame) override;
  // Records only that a configured elapsed interval has passed without a
  // recovered completion source. It never treats the observation as complete.
  void ObserveCompletionTimeout();
  [[nodiscard]] Diagnostics diagnostics() const;

  // Temporary M4 guest controls. Shell may adjust debug.neo2.eink.* without
  // changing stock: enabled defaults false and must be deliberately armed
  // after boot; it immediately blocks new lower-engine calls when false;
  // max_updates is finite and clamped to 1--24; continuous defaults true but
  // may be disabled to restore that finite budget; min_interval_ms defaults
  // to 100 and is clamped to 50--5000 for deliberate sub-second DSU
  // experiments.
  // They are deliberately not a general Android presenter configuration
  // surface.
  [[nodiscard]] static std::uint64_t ActiveSubmissionLimit();
  [[nodiscard]] static std::chrono::milliseconds MinimumSubmissionInterval();
  [[nodiscard]] static std::chrono::milliseconds CompletionObservationInterval();
  [[nodiscard]] static bool ContinuousModeEnabled();
  [[nodiscard]] static bool PresentationEnabled();

  // Encodes the stock direct-call preconditions before any Android/vendor ABI
  // is involved. `engine_mode` may carry only an original observed full-input
  // record word: full control `0x22`, mapped to `(2,1,1)`, or normal
  // differential `0x4`, mapped to `(3,0,0)`. R5's ordinary pipeline selects
  // only the latter; neither mapping accepts a caller-supplied partial
  // rectangle or handwriting path.
  [[nodiscard]] static bool IsObservedFullInput(const Frame& frame);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace neo2::eink::android
