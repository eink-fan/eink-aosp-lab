#pragma once

#include "eink_grayscale_delta.h"

#include <chrono>
#include <cstdint>
#include <optional>

namespace neo2::eink {

// The system-chrome edge in post-conversion capture coordinates. B001-R1
// locks the current RM06L SurfaceFlinger capture to its measured lower edge;
// this is deliberately not a permissive "either edge" classifier.
enum class MicroChromeEdge : std::uint8_t {
  kTop,
  kBottom,
};

[[nodiscard]] constexpr const char* MicroChromeEdgeName(MicroChromeEdge edge) {
  switch (edge) {
    case MicroChromeEdge::kTop:
      return "top";
    case MicroChromeEdge::kBottom:
      return "bottom";
  }
  return "invalid";
}

// B001's bounded full-frame micro-delta policy. This has no waveform or
// completion semantics: it decides only whether a converted candidate should
// be retained briefly, promoted, or passed to the existing scheduler.
struct MicroDeltaConfig {
  std::uint32_t changed_ppm = 2'000;
  std::uint32_t bounding_box_ppm = 5'000;
  std::uint32_t edge_band_ppm = 120'000;
  MicroChromeEdge chrome_edge = MicroChromeEdge::kBottom;
  std::uint32_t material_move_pixels = 32;
  std::chrono::milliseconds settle{300};
  std::chrono::milliseconds repeat{2'000};

  [[nodiscard]] static MicroDeltaConfig Defaults();
  [[nodiscard]] bool IsValid() const;
};

enum class MicroDeltaDecision : std::uint8_t {
  // A broad, moved, unavailable, or otherwise substantive change must not be
  // hidden behind a size heuristic.
  kSubmitSubstantive,
  kSubmitPromoted,
  // The caller retains this newest converted candidate until Deadline().
  kHeld,
  // The caller retains this newest equivalent candidate until the existing
  // repeat deadline, rather than spending another full-frame output now.
  kRepeatSuppressed,
};

class MicroDeltaPolicy final {
 public:
  struct Diagnostics {
    std::uint64_t held = 0;
    std::uint64_t promoted = 0;
    std::uint64_t expiry_submitted = 0;
    std::uint64_t repeat_suppressed = 0;
    std::uint64_t substantive_bypass = 0;
  };

  explicit MicroDeltaPolicy(MicroDeltaConfig config = MicroDeltaConfig::Defaults());

  // Returns false for an invalid contract and true only when a valid new
  // configuration differs from the active one. Callers clear any retained
  // candidate before applying a changed configuration.
  [[nodiscard]] bool Reconfigure(const MicroDeltaConfig& config);

  [[nodiscard]] MicroDeltaDecision Evaluate(const std::optional<GrayscaleDelta>& delta,
                                            int width, int height,
                                            std::chrono::steady_clock::time_point now);

  [[nodiscard]] bool HasDeferredCandidate() const;
  [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> Deadline() const;
  // Called only when the caller submits its retained candidate at Deadline().
  // It begins the bounded equivalent-repeat window; it never claims panel
  // completion.
  void MarkExpirySubmitted(std::chrono::steady_clock::time_point now);
  void Reset();
  [[nodiscard]] Diagnostics diagnostics() const;

 private:
  struct Signature {
    GrayscaleDelta delta;
    int width = 0;
    int height = 0;
  };

  [[nodiscard]] bool IsMicro(const GrayscaleDelta& delta, int width, int height) const;
  [[nodiscard]] bool IsEquivalent(const Signature& previous, const GrayscaleDelta& current,
                                  int width, int height) const;
  void ClearExpiredRepeat(std::chrono::steady_clock::time_point now);

  MicroDeltaConfig config_;
  std::optional<Signature> signature_;
  std::optional<std::chrono::steady_clock::time_point> deadline_;
  bool deferred_ = false;
  bool repeat_guard_ = false;
  Diagnostics diagnostics_;
};

}  // namespace neo2::eink
