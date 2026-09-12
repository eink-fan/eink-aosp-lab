#include "neo2/eink/eink_sleep_image_catalog.h"

#include "eink_software_grayscale.h"

#include <atomic>
#include <limits>
#include <utility>

namespace neo2::eink::android {

struct EinkSleepImageCatalog::Snapshot final {
  std::vector<std::shared_ptr<OwnedGrayscaleBuffer>> entries;
  std::vector<std::shared_ptr<const std::vector<std::uint8_t>>> alpha;
  std::vector<std::shared_ptr<const std::vector<std::uint8_t>>> rgba;
};

namespace {

bool ExpectedBytes(int width, int height, std::size_t* bytes) {
  if (bytes == nullptr || width <= 0 || height <= 0) return false;
  const auto checked_width = static_cast<std::size_t>(width);
  const auto checked_height = static_cast<std::size_t>(height);
  if (checked_width > std::numeric_limits<std::size_t>::max() / checked_height) return false;
  *bytes = checked_width * checked_height;
  return true;
}

bool IsPanelGray(const std::vector<std::uint8_t>& pixels) {
  for (const std::uint8_t pixel : pixels) {
    if ((pixel & 0x0fU) != 0) return false;
  }
  return true;
}

}  // namespace

EinkSleepImageCatalog::EinkSleepImageCatalog(const EinkDeviceProfile& profile)
    : expected_width_(static_cast<int>(profile.panel_width)),
      expected_height_(static_cast<int>(profile.panel_height)) {}

EinkSleepImageCatalog::EinkSleepImageCatalog(int expected_width, int expected_height)
    : expected_width_(expected_width), expected_height_(expected_height) {}

EinkSleepImageCatalog::PublishResult EinkSleepImageCatalog::Validate(
        const std::vector<Entry>& entries) const {
  if (entries.empty()) return PublishResult::kRejectedEmpty;
  if (entries.size() > kMaximumEntries) return PublishResult::kRejectedCount;
  const auto* profile = FindEinkDeviceProfile(static_cast<std::uint32_t>(expected_width_),
                                              static_cast<std::uint32_t>(expected_height_));
  if (profile == nullptr) {
    return PublishResult::kRejectedGeometry;
  }
  std::size_t expected_bytes = 0;
  if (!ExpectedBytes(expected_width_, expected_height_, &expected_bytes)) {
    return PublishResult::kRejectedGeometry;
  }
  if (expected_bytes != PanelBytesFor(*profile) ||
      expected_bytes > std::numeric_limits<std::size_t>::max() / 2) {
    return PublishResult::kRejectedBytes;
  }
  for (const Entry& entry : entries) {
    if (entry.width != expected_width_ || entry.height != expected_height_) {
      return PublishResult::kRejectedGeometry;
    }
    if (entry.panel_gray.size() != expected_bytes) return PublishResult::kRejectedBytes;
    if (!IsPanelGray(entry.panel_gray)) return PublishResult::kRejectedPanelGray;
    if (entry.alpha.size() != expected_bytes) return PublishResult::kRejectedAlphaBytes;
    if (!entry.rgba.empty()) {
      if (!profile->has_color_cfa || entry.rgba.size() != expected_bytes * 4) {
        return PublishResult::kRejectedBytes;
      }
      for (std::size_t index = 0; index < expected_bytes; ++index) {
        if (entry.rgba[4 * index + 3] != entry.alpha[index]) {
          return PublishResult::kRejectedAlphaBytes;
        }
      }
    }
  }
  return PublishResult::kAccepted;
}

EinkSleepImageCatalog::PublishResult EinkSleepImageCatalog::Publish(std::vector<Entry> entries) {
  const PublishResult validation = Validate(entries);
  if (validation != PublishResult::kAccepted) return validation;

  auto next = std::make_shared<Snapshot>();
  next->entries.reserve(entries.size());
  next->alpha.reserve(entries.size());
  next->rgba.reserve(entries.size());
  for (Entry& entry : entries) {
    next->entries.push_back(std::make_shared<OwnedGrayscaleBuffer>(
            entry.width, entry.height, std::move(entry.panel_gray)));
    next->alpha.push_back(
            std::make_shared<const std::vector<std::uint8_t>>(std::move(entry.alpha)));
    next->rgba.push_back(entry.rgba.empty() ? nullptr
            : std::make_shared<const std::vector<std::uint8_t>>(std::move(entry.rgba)));
  }
  std::atomic_store_explicit(&snapshot_, std::shared_ptr<const Snapshot>(std::move(next)),
                             std::memory_order_release);
  return PublishResult::kAccepted;
}

void EinkSleepImageCatalog::Clear() {
  std::atomic_store_explicit(&snapshot_, std::shared_ptr<const Snapshot>{}, std::memory_order_release);
}

std::size_t EinkSleepImageCatalog::size() const {
  const std::shared_ptr<const Snapshot> snapshot =
          std::atomic_load_explicit(&snapshot_, std::memory_order_acquire);
  return snapshot ? snapshot->entries.size() : 0;
}

SleepImageCandidate EinkSleepImageCatalog::CandidateAt(std::size_t index) const {
  const std::shared_ptr<const Snapshot> snapshot =
          std::atomic_load_explicit(&snapshot_, std::memory_order_acquire);
  if (!snapshot || index >= snapshot->entries.size()) return {};
  return {.buffer = snapshot->entries[index],
          .alpha = snapshot->alpha[index],
          .rgba = snapshot->rgba[index],
          .width = expected_width_,
          .height = expected_height_};
}

}  // namespace neo2::eink::android
