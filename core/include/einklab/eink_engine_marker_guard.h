#pragma once

#include <cstdint>

namespace einklab {

// Opt-in backend contract, not a universal display API: the submission counter
// increments synchronously once per call, has one writer, and the display-start
// marker identifies an image which can no longer be merged out of the queue.
// The backend must establish these semantics and its reserved/reset range.
struct EngineMarkerContract {
  std::uint32_t last_trackable_marker;
  std::uint64_t timeout_ms;
};

enum class EngineMarkerProgress { kWaiting, kStarted, kDrift, kTimeout };

inline bool CanTrackNextEngineMarker(std::uint32_t before,
                                     EngineMarkerContract contract) {
  return before < contract.last_trackable_marker;
}

// A start is not waveform completion, buffer release, or optical completion.
// No modular ordering: rollover/reset and unexpected writers fail closed.
inline EngineMarkerProgress CheckEngineMarkerStart(
        std::uint32_t before, std::uint32_t submitted, std::uint32_t displaying,
        std::uint64_t elapsed_ms, EngineMarkerContract contract) {
  if (!CanTrackNextEngineMarker(before, contract) || submitted != before + 1 ||
      displaying > submitted) return EngineMarkerProgress::kDrift;
  if (elapsed_ms >= contract.timeout_ms) return EngineMarkerProgress::kTimeout;
  if (displaying == submitted) return EngineMarkerProgress::kStarted;
  return EngineMarkerProgress::kWaiting;
}

}  // namespace einklab
