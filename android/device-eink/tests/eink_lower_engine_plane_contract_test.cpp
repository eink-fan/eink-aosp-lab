#include "eink_lower_engine_plane_contract.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

class ColorPlane final : public neo2::eink::ComposeBuffer {};

class GrayPlane final : public neo2::eink::GrayscaleBuffer {
 public:
  explicit GrayPlane(std::size_t size) : bytes_(size) {}
  explicit GrayPlane(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {}

  const std::uint8_t* Data() const override { return bytes_.data(); }
  std::size_t ByteCount() const override { return bytes_.size(); }

 private:
  std::vector<std::uint8_t> bytes_;
};

}  // namespace

int main() {
  using neo2::eink::Frame;
  using neo2::eink::LowerEnginePlaneStatus;
  using neo2::eink::ValidateLowerEnginePlanes;
  using neo2::eink::kAuraCDeviceProfile;
  using neo2::eink::kNeo2DeviceProfile;

  const auto aura_gray = std::make_shared<GrayPlane>(
          static_cast<std::size_t>(kAuraCDeviceProfile.panel_width) *
          kAuraCDeviceProfile.panel_height);
  const auto color = std::make_shared<ColorPlane>();
  Frame aura{.compose_buffer = color, .grayscale_buffer = aura_gray};
  assert(ValidateLowerEnginePlanes(kAuraCDeviceProfile, aura) ==
         LowerEnginePlaneStatus::kReady);

  aura.compose_buffer.reset();
  assert(ValidateLowerEnginePlanes(kAuraCDeviceProfile, aura) ==
         LowerEnginePlaneStatus::kMissingColorPlane);
  aura.sleep_image_epoch = 7;
  assert(ValidateLowerEnginePlanes(kAuraCDeviceProfile, aura) == LowerEnginePlaneStatus::kReady);
  aura.grayscale_buffer = std::make_shared<GrayPlane>(aura_gray->ByteCount() - 1);
  assert(ValidateLowerEnginePlanes(kAuraCDeviceProfile, aura) ==
         LowerEnginePlaneStatus::kInvalidGrayscaleSize);
  aura.grayscale_buffer = aura_gray;
  aura.sleep_image_epoch = 0;
  aura.compose_buffer = color;
  aura.grayscale_buffer = std::make_shared<GrayPlane>(aura_gray->ByteCount() - 1);
  assert(ValidateLowerEnginePlanes(kAuraCDeviceProfile, aura) ==
         LowerEnginePlaneStatus::kInvalidGrayscaleSize);
  aura.grayscale_buffer = aura_gray;
  aura.source_buffer = color;
  assert(ValidateLowerEnginePlanes(kAuraCDeviceProfile, aura) ==
         LowerEnginePlaneStatus::kUnexpectedSourcePlane);

  const auto neo_gray = std::make_shared<GrayPlane>(
          static_cast<std::size_t>(kNeo2DeviceProfile.panel_width) *
          kNeo2DeviceProfile.panel_height);
  Frame neo{.grayscale_buffer = neo_gray};
  assert(ValidateLowerEnginePlanes(kNeo2DeviceProfile, neo) ==
         LowerEnginePlaneStatus::kReady);
  neo.compose_buffer = color;
  assert(ValidateLowerEnginePlanes(kNeo2DeviceProfile, neo) ==
         LowerEnginePlaneStatus::kUnexpectedColorPlane);

  Frame sleep{.sleep_image_epoch = 1,
              .grayscale_buffer = std::make_shared<GrayPlane>(
                      std::vector<std::uint8_t>{0x00, 0x80, 0xf0})};
  std::vector<std::uint8_t> rgba;
  assert(neo2::eink::ExpandSleepGrayscaleToRgba(sleep, &rgba));
  assert((rgba == std::vector<std::uint8_t>{0, 0, 0, 255, 136, 136, 136, 255,
                                           255, 255, 255, 255}));
  sleep.sleep_image_epoch = 0;
  assert(!neo2::eink::ExpandSleepGrayscaleToRgba(sleep, &rgba));
  sleep.sleep_image_epoch = 1;
  sleep.compose_buffer = color;
  assert(!neo2::eink::ExpandSleepGrayscaleToRgba(sleep, &rgba));
}
