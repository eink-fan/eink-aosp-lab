#include "neo2/eink/eink_sleep_image_catalog.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using neo2::eink::android::EinkSleepImageCatalog;
using neo2::eink::EinkDeviceProfile;
using neo2::eink::kNeo2DeviceProfile;
using neo2::eink::kOceanDeviceProfile;

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

EinkSleepImageCatalog::Entry Entry(std::vector<std::uint8_t> pixels) {
  return {.width = EinkSleepImageCatalog::kPanelWidth,
          .height = EinkSleepImageCatalog::kPanelHeight,
          .panel_gray = std::move(pixels),
          .alpha = std::vector<std::uint8_t>(EinkSleepImageCatalog::kPanelBytes, 255)};
}

EinkSleepImageCatalog::Entry EntryFor(const EinkDeviceProfile& profile,
                                      std::vector<std::uint8_t> pixels) {
  const auto bytes = EinkSleepImageCatalog::PanelBytesFor(profile);
  return {.width = static_cast<int>(profile.panel_width),
          .height = static_cast<int>(profile.panel_height),
          .panel_gray = std::move(pixels),
          .alpha = std::vector<std::uint8_t>(bytes, 255)};
}

std::vector<std::uint8_t> Panel(std::uint8_t gray) {
  return std::vector<std::uint8_t>(EinkSleepImageCatalog::kPanelBytes, gray);
}

}  // namespace

int main() {
  EinkSleepImageCatalog wrong_catalog(2, 2);
  Require(wrong_catalog.Publish({Entry(Panel(0x10))}) ==
                  EinkSleepImageCatalog::PublishResult::kRejectedGeometry,
          "non-panel catalog geometry was accepted");
  EinkSleepImageCatalog catalog(EinkSleepImageCatalog::kPanelWidth,
                                EinkSleepImageCatalog::kPanelHeight);
  Require(catalog.Publish({}) == EinkSleepImageCatalog::PublishResult::kRejectedEmpty,
          "empty catalog was accepted");
  auto short_panel = Panel(0x10);
  short_panel.pop_back();
  Require(catalog.Publish({Entry(std::move(short_panel))}) ==
                  EinkSleepImageCatalog::PublishResult::kRejectedBytes,
          "short entry was accepted");
  auto short_alpha = Entry(Panel(0x10));
  short_alpha.alpha.pop_back();
  Require(catalog.Publish({std::move(short_alpha)}) ==
                  EinkSleepImageCatalog::PublishResult::kRejectedAlphaBytes,
          "short alpha plane was accepted");
  auto invalid_gray = Panel(0x10);
  invalid_gray[0] = 0x21;
  Require(catalog.Publish({Entry(std::move(invalid_gray))}) ==
                  EinkSleepImageCatalog::PublishResult::kRejectedPanelGray,
          "non-panel-gray entry was accepted");
  auto wrong_geometry = Entry(Panel(0x10));
  wrong_geometry.width = 1;
  wrong_geometry.height = static_cast<int>(EinkSleepImageCatalog::kPanelBytes);
  Require(catalog.Publish({std::move(wrong_geometry)}) ==
                  EinkSleepImageCatalog::PublishResult::kRejectedGeometry,
          "wrong geometry was accepted");

  std::vector<EinkSleepImageCatalog::Entry> too_many;
  for (std::size_t index = 0; index <= EinkSleepImageCatalog::kMaximumEntries; ++index) {
    too_many.push_back(Entry(Panel(0x10)));
  }
  Require(catalog.Publish(std::move(too_many)) ==
                  EinkSleepImageCatalog::PublishResult::kRejectedCount,
          "oversized catalog was accepted");

  Require(catalog.Publish({Entry(Panel(0x10)), Entry(Panel(0x50))}) ==
                  EinkSleepImageCatalog::PublishResult::kAccepted,
          "valid catalog was rejected");
  Require(catalog.size() == 2, "published catalog did not retain both entries");
  const auto selected = catalog.CandidateAt(0);
  Require(selected.buffer && selected.width == EinkSleepImageCatalog::kPanelWidth &&
                  selected.height == EinkSleepImageCatalog::kPanelHeight,
          "valid candidate was not returned");
  Require(selected.buffer->ByteCount() == EinkSleepImageCatalog::kPanelBytes &&
                  selected.buffer->Data()[0] == 0x10 && selected.alpha &&
                  selected.alpha->size() == EinkSleepImageCatalog::kPanelBytes &&
                  (*selected.alpha)[0] == 255,
          "published catalog did not retain its owned bytes");
  Require(!catalog.CandidateAt(2).buffer, "out-of-range candidate did not fail closed");
  catalog.Clear();
  Require(catalog.size() == 0 && !catalog.CandidateAt(0).buffer,
          "cleared catalog retained a candidate");

  EinkSleepImageCatalog ocean(kOceanDeviceProfile);
  const auto ocean_bytes = EinkSleepImageCatalog::PanelBytesFor(kOceanDeviceProfile);
  Require(ocean_bytes == 2123520, "Ocean panel byte geometry drifted");
  Require(ocean.Publish({EntryFor(kOceanDeviceProfile,
                                  std::vector<std::uint8_t>(ocean_bytes, 0x20))}) ==
                  EinkSleepImageCatalog::PublishResult::kAccepted,
          "valid Ocean catalog was rejected");
  const auto ocean_selected = ocean.CandidateAt(0);
  Require(ocean_selected.buffer && ocean_selected.width == 1264 &&
                  ocean_selected.height == 1680 &&
                  ocean_selected.buffer->ByteCount() == ocean_bytes,
          "Ocean catalog did not retain exact profile geometry");

  EinkSleepImageCatalog neo_from_profile(kNeo2DeviceProfile);
  Require(neo_from_profile.Publish({Entry(Panel(0x10))}) ==
                  EinkSleepImageCatalog::PublishResult::kAccepted,
          "Neo profile constructor changed B038 catalog behavior");

  const auto& aura_profile = neo2::eink::kAuraCDeviceProfile;
  EinkSleepImageCatalog aura(aura_profile);
  const auto aura_bytes = EinkSleepImageCatalog::PanelBytesFor(aura_profile);
  auto color_entry = EntryFor(aura_profile, std::vector<std::uint8_t>(aura_bytes, 0x40));
  color_entry.rgba.resize(aura_bytes * 4);
  for (std::size_t i = 0; i < aura_bytes; ++i) {
    color_entry.rgba[4 * i] = 255;
    color_entry.rgba[4 * i + 3] = 255;
  }
  Require(aura.Publish({color_entry}) == EinkSleepImageCatalog::PublishResult::kAccepted,
          "valid Aura color catalog rejected");
  const auto owned_color = aura.CandidateAt(0).rgba;
  Require(owned_color && owned_color->at(0) == 255 && owned_color->at(1) == 0,
          "Aura RGB bytes not retained");
  color_entry.rgba[0] = 0;
  Require(owned_color->at(0) == 255, "Aura catalog borrowed caller color bytes");
  color_entry.rgba[3] = 0;
  Require(aura.Publish({color_entry}) == EinkSleepImageCatalog::PublishResult::kRejectedAlphaBytes,
          "inconsistent color alpha accepted");
  color_entry.rgba.pop_back();
  Require(aura.Publish({color_entry}) == EinkSleepImageCatalog::PublishResult::kRejectedBytes,
          "truncated Aura RGBA plane accepted");
  Require(aura.CandidateAt(0).rgba == owned_color, "invalid publication replaced accepted color");
  aura.Clear();
  Require(!aura.CandidateAt(0).rgba && owned_color->at(0) == 255,
          "clear broke independent owned candidate lifetime");

  std::cout << "PASS\n";
  return 0;
}
