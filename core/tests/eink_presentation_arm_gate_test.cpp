#include "eink_presentation_arm_gate.h"

#include <cassert>

int main() {
  einklab::PresentationArmGate gate;
  assert(!gate.started());
  assert(gate.Observe(false) == einklab::PresentationArmGate::Decision::kDisarmed);
  assert(!gate.started());
  assert(gate.Observe(true) == einklab::PresentationArmGate::Decision::kStart);
  assert(gate.started());
  assert(gate.Observe(false) == einklab::PresentationArmGate::Decision::kDisarmed);
  assert(gate.Observe(true) == einklab::PresentationArmGate::Decision::kAlreadyStarted);
  return 0;
}
