#include "einklab/neo2_android17_profile.h"

#include <cassert>

int main() {
  using namespace einklab::neo2_android17;

  assert(kPanelWidth == 1448);
  assert(kPanelHeight == 1072);
  assert(ToPresentationHint(RefreshMode::kAutomatic) == 0);
  assert(ToPresentationHint(RefreshMode::kDuPartial) == 1);
  assert(ToPresentationHint(RefreshMode::kGc16Partial) == 2);
  assert(ToPresentationHint(RefreshMode::kGl16Partial) == 3);
  assert(ToPresentationHint(RefreshMode::kGlr16Partial) == 4);
  assert(ToPresentationHint(RefreshMode::kAnimationPartial) == 6);
  assert(ToPresentationHint(RefreshMode::kAutoPartial) == 15);
  assert(ToPresentationHint(RefreshMode::kFullUpdate) == 32);
  assert(kFullGc16Hint == 34);
  assert(kNormalDifferentialHint == 4);
}
