#pragma once

#include "eink_presentation_adapter.h"

#include <optional>

namespace neo2::eink {

// This mirrors the precedence demonstrated by the stock vendor path. Values
// remain raw EinkMode bitfields: callers may use named bases (e.g. 2 GC16)
// plus documented modifier bits (e.g. 32 full update).
struct ModePolicy {
  std::optional<int> system_default;
  std::optional<int> package_mode;
  std::optional<int> app_or_view_mode;
};

enum class ModeSource {
  kAutomaticFallback,
  kSystemDefault,
  kPackageMode,
  kAppOrViewMode,
};

struct ResolvedMode {
  int engine_mode = ToEngineMode(RefreshMode::kAutomatic);
  ModeSource source = ModeSource::kAutomaticFallback;
};

[[nodiscard]] ResolvedMode ResolveMode(const ModePolicy& policy);

}  // namespace neo2::eink
