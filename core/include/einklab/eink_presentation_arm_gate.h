#pragma once

namespace einklab {

// One-way gate for a process-lifetime presentation session. It keeps capture
// disabled until an operator deliberately arms the runtime control. Once it
// permits startup, the caller owns any initialization failure and must not
// silently reinitialize the lower engine.
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

}  // namespace einklab
