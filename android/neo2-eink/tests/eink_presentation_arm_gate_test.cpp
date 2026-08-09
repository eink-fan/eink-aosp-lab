#include "eink_presentation_arm_gate.h"

#include <cassert>

int main() {
  neo2::eink::PresentationArmGate gate;
  assert(!gate.started());
  assert(gate.Observe(false) == neo2::eink::PresentationArmGate::Decision::kDisarmed);
  assert(!gate.started());
  assert(gate.Observe(true) == neo2::eink::PresentationArmGate::Decision::kStart);
  assert(gate.started());
  assert(gate.Observe(false) == neo2::eink::PresentationArmGate::Decision::kDisarmed);
  assert(gate.Observe(true) == neo2::eink::PresentationArmGate::Decision::kAlreadyStarted);
  return 0;
}
