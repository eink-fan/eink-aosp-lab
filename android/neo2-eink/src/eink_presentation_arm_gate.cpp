#include "eink_presentation_arm_gate.h"

namespace neo2::eink {

PresentationArmGate::Decision PresentationArmGate::Observe(bool enabled) {
  if (!enabled) return Decision::kDisarmed;
  if (started_) return Decision::kAlreadyStarted;
  started_ = true;
  return Decision::kStart;
}

}  // namespace neo2::eink
