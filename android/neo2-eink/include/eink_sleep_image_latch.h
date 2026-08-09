#pragma once

#include "eink_presentation_adapter.h"

#include <cstdint>
#include <functional>
#include <memory>

namespace neo2::eink {

struct SleepImageCandidate {
  std::shared_ptr<GrayscaleBuffer> buffer;
  int width = 0;
  int height = 0;
};

enum class SleepImageDecision : std::uint8_t {
  kPassThrough,
  kReplace,
  kSuppress,
};

class SleepImageLatch final {
 public:
  using Selector = std::function<SleepImageCandidate(int width, int height)>;

  // A valid full frame is represented by an origin-aligned dirty_rect whose
  // area describes the grayscale source. kReplace updates only
  // frame.grayscale_buffer; kSuppress leaves frame intact.
  [[nodiscard]] SleepImageDecision Evaluate(Frame& frame, bool screen_off_latched,
                                            const Selector& selector);

  [[nodiscard]] bool selection_latched() const { return state_ == State::kSelected; }

 private:
  enum class State : std::uint8_t {
    kIdle,
    kSelected,
    kFailed,
  };

  State state_ = State::kIdle;
};

}  // namespace neo2::eink
