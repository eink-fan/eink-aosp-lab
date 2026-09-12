#pragma once

namespace neo2::eink::android {

// Called once from the Android-17 SurfaceFlinger process-start integration.
// The published calibrated service accepts calls only from the system-server
// mediator; it owns its backend for the process lifetime and does not depend
// on a capture session or any session-owned object.
[[nodiscard]] bool PublishManualFrontLightBinderService();

}  // namespace neo2::eink::android
