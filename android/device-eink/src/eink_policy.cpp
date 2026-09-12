#include "eink_policy.h"

namespace neo2::eink {

ResolvedMode ResolveMode(const ModePolicy& policy) {
  if (policy.app_or_view_mode) {
    return {.engine_mode = *policy.app_or_view_mode, .source = ModeSource::kAppOrViewMode};
  }
  if (policy.package_mode) {
    return {.engine_mode = *policy.package_mode, .source = ModeSource::kPackageMode};
  }
  if (policy.system_default) {
    return {.engine_mode = *policy.system_default, .source = ModeSource::kSystemDefault};
  }
  return {};
}

}  // namespace neo2::eink
