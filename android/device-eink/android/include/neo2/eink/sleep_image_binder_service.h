#pragma once

namespace neo2::eink::android {

class RenderSurfaceCaptureSession;

// The SurfaceFlinger-private endpoint has one caller: system_server. It maps
// and copies bounded shared-memory candidates on Binder threads, then hands
// owned bytes to the capture session. It is never reachable by an app.
[[nodiscard]] bool PublishSleepImageBinderService();
void RegisterSleepImageCaptureSession(RenderSurfaceCaptureSession* session);
void UnregisterSleepImageCaptureSession(RenderSurfaceCaptureSession* session);

}  // namespace neo2::eink::android
