#include "neo2/eink/eink_sleep_image_catalog.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using neo2::eink::android::EinkSleepImageCatalog;

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

  std::cout << "PASS\n";
  return 0;
}
