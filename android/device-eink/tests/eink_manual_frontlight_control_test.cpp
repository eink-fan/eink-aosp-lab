#include "eink_manual_frontlight_control.h"

#include <cassert>
#include <vector>

namespace {

using neo2::eink::FrontLightControlBackend;
using neo2::eink::FrontLightControlOutcome;
using neo2::eink::ManualFrontLightControl;
using neo2::eink::ManualFrontLightRequest;

class FakeBackend final : public FrontLightControlBackend {
 public:
  FrontLightControlOutcome next = FrontLightControlOutcome::kApplied;
  std::vector<ManualFrontLightRequest> requests;

  FrontLightControlOutcome ApplyManualRequest(const ManualFrontLightRequest& request) override {
    requests.push_back(request);
    return next;
  }
};

}  // namespace

int main() {
  FakeBackend backend;
  ManualFrontLightControl control(backend);

  // Slider movement has no route to this explicit-request boundary. Until an
  // Apply or Turn off action reaches it, the backend must not see a write.
  assert(backend.requests.empty());

  assert(control.Apply({.brightness_percent = -1, .warmth_percent = 50}) ==
         FrontLightControlOutcome::kRejected);
  assert(control.Apply({.brightness_percent = 50, .warmth_percent = 101}) ==
         FrontLightControlOutcome::kRejected);
  assert(backend.requests.empty());

  backend.next = FrontLightControlOutcome::kUnavailable;
  assert(control.Apply({.brightness_percent = 50, .warmth_percent = 50}) ==
         FrontLightControlOutcome::kUnavailable);
  assert(backend.requests.size() == 1);

  backend.next = FrontLightControlOutcome::kCapped;
  assert(control.Apply({.brightness_percent = 100, .warmth_percent = 100}) ==
         FrontLightControlOutcome::kCapped);
  assert(backend.requests.size() == 2);

  backend.next = FrontLightControlOutcome::kOff;
  assert(control.Apply({.brightness_percent = 0, .warmth_percent = 75}) ==
         FrontLightControlOutcome::kOff);
  assert(backend.requests.size() == 3);

  backend.next = FrontLightControlOutcome::kApplied;
  assert(control.Apply({.brightness_percent = 0, .warmth_percent = 75}) ==
         FrontLightControlOutcome::kFailed);
  assert(!control.failure_latched());

  backend.next = FrontLightControlOutcome::kFailed;
  assert(control.Apply({.brightness_percent = 50, .warmth_percent = 50}) ==
         FrontLightControlOutcome::kFailed);
  assert(control.failure_latched());
  assert(control.Apply({.brightness_percent = 25, .warmth_percent = 25}) ==
         FrontLightControlOutcome::kFailureLatched);
  assert(backend.requests.size() == 5);
}
