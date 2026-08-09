#pragma once

#include <cstdint>

namespace neo2::eink {

inline constexpr std::uint32_t kNeo2PanelWidth = 1448;
inline constexpr std::uint32_t kNeo2PanelHeight = 1072;

// The first same-version RenderSurface proof must establish that it is seeing
// the stock panel-native target. A mismatch is sticky for the session: later
// matching frames must not accidentally enable a pixel/backend path on a
// target whose transform was not understood.
enum class PanelGeometryStatus {
  kUnverified,
  kVerified,
  kRejected,
};

class Neo2PanelGeometryGate final {
 public:
  [[nodiscard]] PanelGeometryStatus Observe(std::uint32_t width, std::uint32_t height);

  [[nodiscard]] PanelGeometryStatus Status() const { return status_; }
  [[nodiscard]] bool IsVerified() const { return status_ == PanelGeometryStatus::kVerified; }
  [[nodiscard]] std::uint64_t SampleCount() const { return sample_count_; }
  [[nodiscard]] std::uint64_t MismatchCount() const { return mismatch_count_; }

 private:
  PanelGeometryStatus status_ = PanelGeometryStatus::kUnverified;
  std::uint64_t sample_count_ = 0;
  std::uint64_t mismatch_count_ = 0;
};

}  // namespace neo2::eink
