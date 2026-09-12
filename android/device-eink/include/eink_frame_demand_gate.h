#pragma once

#include <cstddef>
#include <cstdint>

namespace neo2::eink {

// A panel update is useful only when the panel-visible grayscale pixels have
// changed. This small, portable gate deliberately works after conversion: a
// UI change that has the same e-ink grayscale result does not need a waveform
// submission. It does not claim that a changed signature is physical panel
// completion, nor does it select a refresh mode.
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
  // presented. This class is single-worker owned; Android's conversion worker
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

}  // namespace neo2::eink
