#pragma once

#include <eink_presentation_adapter.h>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace neo2::eink::android {

// Android-14/same-vendor-build-only Engine backend for the recovered
// IdisplayEpdcDeviceSync boundary. This declaration deliberately does not
// promise a stable vendor ABI and the archive containing its implementation is
// not linked by the M1–M3 capture integration.
class SyncControllerBackend final : public Engine {
 public:
  enum class State : std::uint8_t {
    kStopped,
    kInitialized,
    kPoisoned,
  };

  struct Diagnostics {
    State state = State::kStopped;
    std::uint64_t rejected_frames = 0;
    std::uint64_t submitted_frames = 0;
  };

  SyncControllerBackend();
  ~SyncControllerBackend() override;

  SyncControllerBackend(const SyncControllerBackend&) = delete;
  SyncControllerBackend& operator=(const SyncControllerBackend&) = delete;

  // Calls the vendor wrapper's one-time controller initialization. This is a
  // process-lifetime transition and is intentionally not reachable from the
  // M1–M3 session/graft. A future M4 owner must first complete its exact
  // dependency and single-owner checks, then call this once on the serial
  // presentation worker before arming OneShotEngine.
  [[nodiscard]] bool InitializeForObservedPanel();

  // Requires an initialized backend and the observed direct-vendor frame
  // shape. Production M4 must place OneShotEngine in front of this Engine;
  // this local validation is a second defensive boundary, not the retry limit.
  [[nodiscard]] bool Submit(const Frame& frame) override;
  [[nodiscard]] Diagnostics diagnostics() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace neo2::eink::android