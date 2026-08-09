#include "neo2_panel_geometry_gate.h"

#include <cassert>

int main() {
  using neo2::eink::Neo2PanelGeometryGate;
  using neo2::eink::PanelGeometryStatus;

  Neo2PanelGeometryGate gate;
  assert(gate.Status() == PanelGeometryStatus::kUnverified);
  assert(!gate.IsVerified());
  assert(gate.SampleCount() == 0);

  assert(gate.Observe(1448, 1072) == PanelGeometryStatus::kVerified);
  assert(gate.IsVerified());
  assert(gate.SampleCount() == 1);
  assert(gate.MismatchCount() == 0);

  assert(gate.Observe(1072, 1448) == PanelGeometryStatus::kRejected);
  assert(!gate.IsVerified());
  assert(gate.MismatchCount() == 1);
  // Rejection remains latched for the entire session.
  assert(gate.Observe(1448, 1072) == PanelGeometryStatus::kRejected);
  assert(gate.SampleCount() == 3);
}
