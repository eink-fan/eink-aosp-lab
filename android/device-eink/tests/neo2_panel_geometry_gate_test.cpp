#include "neo2_panel_geometry_gate.h"

#include <cassert>

int main() {
  using neo2::eink::EinkPanelGeometryGate;
  using neo2::eink::Neo2PanelGeometryGate;
  using neo2::eink::PanelGeometryStatus;
  using neo2::eink::kOceanDeviceProfile;
  using neo2::eink::kAuraCDeviceProfile;

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

  EinkPanelGeometryGate ocean(kOceanDeviceProfile);
  assert(ocean.ExpectedWidth() == 1264);
  assert(ocean.ExpectedHeight() == 1680);
  assert(ocean.Observe(1264, 1680) == PanelGeometryStatus::kVerified);
  assert(ocean.Observe(1448, 1072) == PanelGeometryStatus::kRejected);

  EinkPanelGeometryGate aura(kAuraCDeviceProfile);
  assert(aura.ExpectedWidth() == 2480);
  assert(aura.ExpectedHeight() == 1860);
  assert(aura.Observe(2480, 1860) == PanelGeometryStatus::kVerified);
  assert(aura.Observe(1860, 2480) == PanelGeometryStatus::kRejected);

  EinkPanelGeometryGate unknown(1200, 1600);
  assert(!unknown.HasRecognizedProfile());
  assert(unknown.Observe(1200, 1600) == PanelGeometryStatus::kRejected);
}
