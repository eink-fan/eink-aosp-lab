#include "eink_frame_demand_gate.h"

namespace neo2::eink {

DemandDecision FrameDemandGate::Evaluate(FrameSignature signature) {
  if (!has_previous_) {
    previous_ = signature;
    has_previous_ = true;
    if (next_frame_is_rearm_) {
      next_frame_is_rearm_ = false;
      ++diagnostics_.rearmed_frames;
      return DemandDecision::kRearmed;
    }
    ++diagnostics_.session_start_frames;
    return DemandDecision::kSessionStart;
  }
  if (previous_ == signature) {
    ++diagnostics_.unchanged_frames;
    return DemandDecision::kUnchanged;
  }
  previous_ = signature;
  ++diagnostics_.changed_frames;
  return DemandDecision::kPanelContentChanged;
}

void FrameDemandGate::Reset() {
  has_previous_ = false;
  next_frame_is_rearm_ = true;
}

}  // namespace neo2::eink
