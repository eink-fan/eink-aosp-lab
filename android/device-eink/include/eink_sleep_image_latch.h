#pragma once

#include "eink_presentation_adapter.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace neo2::eink {

struct SleepImageCandidate {
  std::shared_ptr<GrayscaleBuffer> buffer;
  std::shared_ptr<const std::vector<std::uint8_t>> alpha;
  std::shared_ptr<const std::vector<std::uint8_t>> rgba;
  int width = 0;
  int height = 0;
};

enum class SleepImageDecision : std::uint8_t {
  kPassThrough,
  kReplace,
  kSuppress,
};

enum class SleepImageArmResult : std::uint8_t {
  kAccepted,
  kRejectedZero,
  kRejectedStale,
  kRejectedDuplicate,
  kRejectedMismatched,
};

enum class SleepImageDisarmResult : std::uint8_t {
  kAccepted,
  kRejectedZero,
  kRejectedInactive,
  kRejectedMismatched,
};

enum class SleepImagePresentationResult : std::uint8_t {
  kAccepted,
  kRejectedZero,
  kRejectedStale,
  kRejectedInactive,
  kRejectedMismatched,
  kRejectedDuplicate,
  kRejectedCatalog,
  kRejectedUnavailable,
  kRejectedEnqueue,
  kRejectedAlpha,
  kRejectedBackground,
};

enum class SleepImagePresentationMode : std::uint8_t {
  kOpaque = 1,
  kOverlay = 2,
};

enum class SleepImageLatchState : std::uint8_t {
  kInactive,
  kArmed,
  kConsumed,
  kBlackFallback,
};

// This is deliberately aggregate-only state for the converted-frame seam.
// The fixed maximum classifies panel-gray samples in [0, 96] as the early
// dark-fade qualification range. It reads exactly the already-converted
// full-frame extent.
inline constexpr std::uint8_t kSleepImageMaximumGray = 96;

enum class TerminalBlackOutcome : std::uint8_t {
  kNone,
  kCandidateReplaced,
  kCatalogMissingFallback,
  kDuplicateSuppressed,
  kRearmed,
};

struct SleepImageEvaluationSummary {
  bool valid_full_frame = false;
  std::uint8_t minimum_gray = 0;
  std::uint8_t maximum_gray = 0;
  std::uint8_t mean_gray = 0;
  std::uint8_t threshold = kSleepImageMaximumGray;
  SleepImageLatchState state_before = SleepImageLatchState::kInactive;
  bool ready = false;
  bool holding = false;
  SleepImageDecision decision = SleepImageDecision::kPassThrough;
  TerminalBlackOutcome outcome = TerminalBlackOutcome::kNone;
};

class SleepImageLatch final {
 public:
  using Selector = std::function<SleepImageCandidate(int width, int height)>;

  // These methods preserve the private Binder ABI while making the accepted
  // lifecycle epoch authoritative for functional eligibility.
  [[nodiscard]] SleepImageArmResult ArmSleepCycle(std::uint64_t epoch);
  // Consumes the matching armed cycle before a prevalidated candidate is
  // directly queued. Once consumed, ordinary capture is held until disarm.
  [[nodiscard]] SleepImagePresentationResult BeginPresentation(std::uint64_t epoch);
  [[nodiscard]] SleepImageDisarmResult DisarmSleepCycle(std::uint64_t epoch);

  // A valid full frame is represented by an origin-aligned dirty_rect whose
  // exact area describes the already-converted grayscale source. The first
  // terminal-black full frame in an armed cycle uses the latest prepublished
  // selector once. Ordinary frame content never arms or rearms a cycle. After
  // a replacement, duplicate black frames are suppressed so the selected
  // image is retained. If selection is unavailable or invalid, black passes
  // through until the matching disarm. kReplace updates only
  // frame.grayscale_buffer; kSuppress leaves it intact.
  [[nodiscard]] SleepImageDecision Evaluate(Frame& frame, const Selector& selector);

  [[nodiscard]] bool selection_latched() const { return state_ == SleepImageLatchState::kConsumed; }
  [[nodiscard]] SleepImageLatchState state() const { return state_; }
  [[nodiscard]] std::uint64_t active_epoch() const { return active_epoch_; }
  [[nodiscard]] TerminalBlackOutcome last_outcome() const { return last_outcome_; }
  [[nodiscard]] SleepImageEvaluationSummary last_evaluation() const { return last_evaluation_; }

 private:
  SleepImageLatchState state_ = SleepImageLatchState::kInactive;
  std::uint64_t last_armed_epoch_ = 0;
  std::uint64_t active_epoch_ = 0;
  TerminalBlackOutcome last_outcome_ = TerminalBlackOutcome::kNone;
  SleepImageEvaluationSummary last_evaluation_;
};

}  // namespace neo2::eink
