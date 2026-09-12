#pragma once

namespace neo2::eink {

// One-way gate for a process-lifetime private presenter. It keeps a guest
// capture-only until an operator deliberately arms the existing runtime
// control. Once it permits startup, the caller owns any initialization failure
// and must not silently reinitialize the lower engine.
class PresentationArmGate final {
 public:
  enum class Decision {
    kDisarmed,
    kStart,
    kAlreadyStarted,
  };

  [[nodiscard]] Decision Observe(bool enabled);
  [[nodiscard]] bool started() const { return started_; }

 private:
  bool started_ = false;
};

}  // namespace neo2::eink
