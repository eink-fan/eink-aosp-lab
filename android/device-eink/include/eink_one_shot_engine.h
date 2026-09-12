#pragma once

#include "eink_presentation_adapter.h"

#include <cstdint>
#include <mutex>

namespace neo2::eink {

// A deliberately narrow M4 safety boundary around a same-build vendor backend.
// It contains no Android or vendor ABI: the actual private Sync wrapper is the
// delegated Engine and is not eligible to receive a frame until the owner calls
// Arm(). A completed (including failed) attempt is terminal for this process.
class OneShotEngine final : public Engine {
 public:
  enum class State : std::uint8_t { kDisarmed, kArmed, kAttempted };

  struct Diagnostics {
    State state = State::kDisarmed;
    std::uint64_t rejected = 0;
    std::uint64_t attempts = 0;
    bool delegate_accepted = false;
  };

  explicit OneShotEngine(Engine& delegate);

  OneShotEngine(const OneShotEngine&) = delete;
  OneShotEngine& operator=(const OneShotEngine&) = delete;

  // Arming is intentionally explicit and succeeds only once. Production M4
  // must call it only after the same-version wrapper has completed its single
  // process-lifetime initialization and after a separate output approval.
  [[nodiscard]] bool Arm();
  [[nodiscard]] bool Submit(const Frame& frame) override;
  [[nodiscard]] Diagnostics diagnostics() const;

 private:
  [[nodiscard]] static bool IsObservedOneShotFrame(const Frame& frame);

  Engine& delegate_;
  mutable std::mutex mutex_;
  Diagnostics diagnostics_;
};

}  // namespace neo2::eink