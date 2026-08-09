#pragma once

#include <chrono>
#include <cstdint>

namespace einklab {

// The only active response to a proven full-pool condition. It owns no
// pixels, buffers, fences, or threads. The platform seam supplies one later
// RenderSurface composition request after a pool resource returns; only that
// normal queueBuffer call may attempt a copy.
struct FullPoolResampleConfig {
  std::chrono::milliseconds expiry{500};

  [[nodiscard]] static FullPoolResampleConfig Defaults();
  [[nodiscard]] bool IsValid() const;
};

struct FullPoolPipelineState {
  bool conversion_in_flight = false;
  bool pending_capture = false;
  bool adapter_retained = false;
};

enum class FullPoolObservationDecision : std::uint8_t {
  kIgnored,
  kCreated,
  kCoalesced,
};

enum class FullPoolReleaseDecision : std::uint8_t {
  kNone,
  kDispatchResample,
  kDispatchExpiryFallback,
};

enum class FullPoolCaptureDecision : std::uint8_t {
  kNormalCapture,
  kResampleCapture,
  kExpiryFallbackCapture,
};

class FullPoolResamplePolicy final {
 public:
  struct Diagnostics {
    std::uint64_t created = 0;
    std::uint64_t coalesced = 0;
    std::uint64_t dispatched = 0;
    std::uint64_t expiry_fallback = 0;
    std::uint64_t fulfilled = 0;
    std::uint64_t copy_failures = 0;
    std::uint64_t cancelled = 0;
  };

  explicit FullPoolResamplePolicy(FullPoolResampleConfig config = FullPoolResampleConfig::Defaults());

  [[nodiscard]] bool Reconfigure(const FullPoolResampleConfig& config);

  // Creates at most one obligation only for the exact R2-attributed state:
  // a full pool with conversion and pending work, and no adapter retention.
  [[nodiscard]] FullPoolObservationDecision ObserveFullPool(
          bool pool_full, const FullPoolPipelineState& state,
          std::chrono::steady_clock::time_point now);

  // A conversion-worker resource return consumes the one obligation and asks
  // the platform owner for one normal composition. Expiry changes only the
  // reason/counter; it never permits a second outstanding request.
  [[nodiscard]] FullPoolReleaseDecision OnResourceReleased(
          std::chrono::steady_clock::time_point now);

  // Called at the RenderSurface queueBuffer seam before TryCopy. A naturally
  // arriving composition may fulfill the already scheduled obligation; this
  // still leaves no second request outstanding.
  [[nodiscard]] FullPoolCaptureDecision TakeCaptureOpportunity();
  void RecordCaptureOutcome(FullPoolCaptureDecision decision, bool copied);
  void Cancel();

  [[nodiscard]] bool HasOutstandingObligation() const;
  [[nodiscard]] Diagnostics diagnostics() const;

 private:
  FullPoolResampleConfig config_;
  bool outstanding_ = false;
  bool awaiting_capture_ = false;
  bool awaiting_expiry_fallback_ = false;
  std::chrono::steady_clock::time_point deadline_{};
  Diagnostics diagnostics_;
};

}  // namespace einklab
