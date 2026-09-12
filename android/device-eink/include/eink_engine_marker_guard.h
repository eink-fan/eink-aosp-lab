#pragma once

#include <cstdint>
#include <limits>

namespace neo2::eink {

// Start-of-waveform evidence only. A started image has left the pending-image
// coalescer; this does not assert that its waveform or the panel has completed.
enum class EngineMarkerProgress { kWaiting, kStarted, kDrift, kTimeout };

inline bool CanTrackNextEngineMarker(std::uint32_t before) {
  // The retained engine special-cases UINT32_MAX. Do not guess across reset.
  return before < std::numeric_limits<std::uint32_t>::max() - 1;
}

inline EngineMarkerProgress CheckEngineMarkerStart(
        std::uint32_t before, std::uint32_t submitted, std::uint32_t displaying,
        std::uint64_t elapsed_ms, std::uint64_t timeout_ms) {
  if (!CanTrackNextEngineMarker(before) || submitted != before + 1 ||
      displaying > submitted) return EngineMarkerProgress::kDrift;
  if (elapsed_ms >= timeout_ms) return EngineMarkerProgress::kTimeout;
  if (displaying == submitted) return EngineMarkerProgress::kStarted;
  return EngineMarkerProgress::kWaiting;
}

}  // namespace neo2::eink
