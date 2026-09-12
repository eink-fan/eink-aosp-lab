#pragma once

#include <cstddef>
#include <cstdint>

namespace einklab {

// This gate compares signatures in the selected presentation domain. A gray
// backend may sign converted gray; a color backend must include RGB changes,
// even at equal luma. A signature may be a hash or an exact tracker's generation.
// Treatment changes require invalidation even if source pixels are unchanged.
// This is capture demand, not accepted output or physical panel completion;
// explicit refresh requests must bypass ordinary unchanged-content suppression.
struct FrameSignature {
  std::uint64_t hash = 0;
  std::size_t bytes = 0;
  int width = 0;
  int height = 0;

  [[nodiscard]] bool operator==(const FrameSignature& other) const = default;
};

enum class DemandDecision : std::uint8_t {
  // The conservative full-frame policy for a new session.
  kSessionStart,
  // The conservative full-frame policy after an operator disarmed and later
  // rearmed presentation. A frame blocked while disarmed is never assumed to
  // be on the physical panel.
  kRearmed,
  kPanelContentChanged,
  kUnchanged,
};

class FrameDemandGate final {
 public:
  struct Diagnostics {
    std::uint64_t session_start_frames = 0;
    std::uint64_t rearmed_frames = 0;
    std::uint64_t changed_frames = 0;
    std::uint64_t unchanged_frames = 0;
  };

  // Records the latest converted panel image and returns whether it should be
  // presented. This class is single-worker owned; platform's conversion worker
  // is its sole caller.
  [[nodiscard]] DemandDecision Evaluate(FrameSignature signature);
  [[nodiscard]] Diagnostics diagnostics() const { return diagnostics_; }

  // An explicit disarm invalidates the panel-visible baseline. Resetting makes
  // the next conversion eligible for the conservative full-frame policy and
  // identifies that policy decision as a rearm in diagnostics.
  void Reset();

 private:
  bool has_previous_ = false;
  bool next_frame_is_rearm_ = false;
  FrameSignature previous_;
  Diagnostics diagnostics_;
};

}  // namespace einklab
