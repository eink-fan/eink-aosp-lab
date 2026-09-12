#pragma once

#include "eink_device_profile.h"
#include "eink_sleep_image_latch.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace neo2::eink::android {

// A bounded, immutable panel-gray catalog. It deliberately owns copied bytes:
// a Binder or app-provider boundary must never leave SurfaceFlinger with a
// borrowed mapping, a caller-reported size, or a decoder-owned buffer.
class EinkSleepImageCatalog final {
 public:
  struct Entry {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> panel_gray;
    std::vector<std::uint8_t> alpha;
    std::vector<std::uint8_t> rgba{};
  };

  enum class PublishResult : std::uint8_t {
    kAccepted,
    kRejectedEmpty,
    kRejectedCount,
    kRejectedGeometry,
    kRejectedBytes,
    kRejectedPanelGray,
    kRejectedAlphaBytes,
    kRejectedSelection,
  };

  // B038 source compatibility. Variant-aware callers use PanelBytesFor() and
  // construct the catalog from the build-selected device profile.
  static constexpr int kPanelWidth = static_cast<int>(kNeo2DeviceProfile.panel_width);
  static constexpr int kPanelHeight = static_cast<int>(kNeo2DeviceProfile.panel_height);
  static constexpr std::size_t kPanelBytes =
          static_cast<std::size_t>(kPanelWidth) * static_cast<std::size_t>(kPanelHeight);
  static constexpr std::size_t kMaximumEntries = 8;
  static constexpr std::size_t kMaximumCatalogBytes = kMaximumEntries * kPanelBytes * 2;

  [[nodiscard]] static constexpr std::size_t PanelBytesFor(const EinkDeviceProfile& profile) {
    return static_cast<std::size_t>(profile.panel_width) *
            static_cast<std::size_t>(profile.panel_height);
  }

  // The Binder ingress must accept exactly one native panel-gray allocation.
  // Keeping the predicate portable lets host tests cover the boundary decision
  // without pretending to compile Android's shared-memory API off-target.
  [[nodiscard]] static constexpr bool IsPanelByteSize(
          std::size_t byte_count, const EinkDeviceProfile& profile = BuildEinkDeviceProfile()) {
    return byte_count == PanelBytesFor(profile);
  }

  explicit EinkSleepImageCatalog(const EinkDeviceProfile& profile);
  EinkSleepImageCatalog(int expected_width, int expected_height);

  // Replaces the complete catalog atomically. All entries are validated and
  // moved into owned immutable storage before the prior snapshot is discarded.
  [[nodiscard]] PublishResult Publish(std::vector<Entry> entries);
  void Clear();

  [[nodiscard]] std::size_t size() const;
  // The caller (the framework mediator) chooses an index once per sleep
  // epoch. Invalid indexes and missing snapshots fail closed to no candidate.
  [[nodiscard]] SleepImageCandidate CandidateAt(std::size_t index) const;

 private:
  struct Snapshot;

  [[nodiscard]] PublishResult Validate(const std::vector<Entry>& entries) const;

  int expected_width_;
  int expected_height_;
  std::shared_ptr<const Snapshot> snapshot_;
};

}  // namespace neo2::eink::android
