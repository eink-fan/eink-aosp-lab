#include "eink_policy.h"

#include <cstdlib>
#include <iostream>

namespace {

using neo2::eink::ModePolicy;
using neo2::eink::ModeSource;
using neo2::eink::ResolveMode;

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

}  // namespace

int main() {
  ModePolicy automatic_policy{};
  const auto automatic = ResolveMode(automatic_policy);
  Require(automatic.engine_mode == 0 && automatic.source == ModeSource::kAutomaticFallback,
          "automatic fallback is wrong");

  ModePolicy system_policy{};
  system_policy.system_default = 3;
  const auto system = ResolveMode(system_policy);
  Require(system.engine_mode == 3 && system.source == ModeSource::kSystemDefault,
          "system default is wrong");

  ModePolicy package_policy{};
  package_policy.system_default = 3;
  package_policy.package_mode = 2;
  const auto package = ResolveMode(package_policy);
  Require(package.engine_mode == 2 && package.source == ModeSource::kPackageMode,
          "package mode did not override system mode");

  // 99 is the documented GL16 full-mode bitfield. Do not strip modifier bits.
  ModePolicy app_policy{};
  app_policy.system_default = 0;
  app_policy.package_mode = 2;
  app_policy.app_or_view_mode = 99;
  const auto app = ResolveMode(app_policy);
  Require(app.engine_mode == 99 && app.source == ModeSource::kAppOrViewMode,
          "app mode did not override package mode or preserve flags");

  std::cout << "PASS\n";
  return 0;
}
