#include "neo2/eink/manual_frontlight_binder_service.h"

#include "neo2/eink/frontlight_direct_backend.h"
#include "neo2/eink/render_surface_capture_session.h"
#include "neo2/eink/pogo_button_long_press_configurator.h"

#include <eink_device_profile.h>
#include <eink_manual_frontlight_control.h>

#include <binder/Binder.h>
#include <binder/IServiceManager.h>
#include <binder/IPCThreadState.h>
#include <binder/PermissionCache.h>

#include <private/android_filesystem_config.h>

#include <cstdint>
#include <memory>
#include <mutex>

namespace neo2::eink::android {
namespace {

constexpr char kServiceName[] = "neo2_frontlight_controls";
constexpr char kDescriptor[] = "org.neo2.controls.IFrontLightControl";
constexpr char kControlPermission[] = "org.neo2.controls.permission.CONTROL_FRONTLIGHT";

class ProcessControl final {
 public:
  ProcessControl() : control(backend) {}

  FrontLightDirectBackend backend;
  neo2::eink::ManualFrontLightControl control;
  std::mutex mutex;
};

class ControlBinder final : public ::android::BBinder {
 public:
  explicit ControlBinder(std::shared_ptr<ProcessControl> control) : control_(std::move(control)) {}

  const ::android::String16& getInterfaceDescriptor() const override {
    static const ::android::String16 kInterfaceDescriptor(kDescriptor);
    return kInterfaceDescriptor;
  }

  ::android::status_t onTransact(std::uint32_t code, const ::android::Parcel& data,
                                 ::android::Parcel* reply, std::uint32_t flags) override {
    static_cast<void>(flags);
    auto* ipc = ::android::IPCThreadState::self();
    const auto calling_uid = ipc->getCallingUid();
    const auto calling_pid = ipc->getCallingPid();
    if ((code == ::android::IBinder::FIRST_CALL_TRANSACTION + 2 ||
         code == ::android::IBinder::FIRST_CALL_TRANSACTION + 3 ||
         code == ::android::IBinder::FIRST_CALL_TRANSACTION + 4 ||
         code == ::android::IBinder::FIRST_CALL_TRANSACTION + 6) && reply != nullptr) {
      if (!::android::PermissionCache::checkPermission(
                  ::android::String16("org.neo2.controls.permission.REFRESH_DISPLAY"),
                  calling_pid, calling_uid) || !data.checkInterface(this))
        return ::android::PERMISSION_DENIED;
      if (code == ::android::IBinder::FIRST_CALL_TRANSACTION + 2) {
        const int enabled = data.readInt32();
        if (data.errorCheck() != ::android::OK || data.dataAvail() != 0 ||
            enabled < -1 || enabled > 1) return ::android::BAD_VALUE;
        const int state = ConfigureNeo2EinkColorQuality(enabled);
        reply->writeNoException();
        reply->writeInt32(state);
      } else if (code == ::android::IBinder::FIRST_CALL_TRANSACTION + 4 ||
                 code == ::android::IBinder::FIRST_CALL_TRANSACTION + 6) {
        const bool vivid = code == ::android::IBinder::FIRST_CALL_TRANSACTION + 6;
        const int requested = data.readInt32();
        if (data.errorCheck() != ::android::OK || data.dataAvail() != 0 ||
            requested < -1 || requested > (vivid ? 1 : 2)) return ::android::BAD_VALUE;
        const auto result = vivid ? ConfigureNeo2EinkVividColor(requested)
                                  : ConfigureNeo2EinkGrayPreference(requested);
        reply->writeNoException();
        reply->writeInt32(result.mode);
        reply->writeInt32(result.redraw_queued ? 1 : 0);
      } else {
        if (data.dataAvail() != 0) return ::android::BAD_VALUE;
        const auto result = RequestNeo2EinkColorCleanup();
        reply->writeNoException();
        reply->writeString16(::android::String16(result.c_str()));
      }
      return ::android::OK;
    }
    if ((code == ::android::IBinder::FIRST_CALL_TRANSACTION + 1 ||
         code == ::android::IBinder::FIRST_CALL_TRANSACTION + 5) && reply != nullptr) {
      if (!::android::PermissionCache::checkPermission(
                  ::android::String16("org.neo2.controls.permission.REFRESH_DISPLAY"),
                  calling_pid, calling_uid) || !data.checkInterface(this) ||
          data.dataAvail() != 0) return ::android::PERMISSION_DENIED;
      // Preserve transaction +1 as through-white for existing clients.
      const auto result = code == ::android::IBinder::FIRST_CALL_TRANSACTION + 5
              ? RequestNeo2EinkFullWaveformRefresh() : RequestNeo2EinkFullRefresh();
      reply->writeNoException();
      reply->writeString16(::android::String16(result.c_str()));
      return ::android::OK;
    }
    if (code != ::android::IBinder::FIRST_CALL_TRANSACTION || reply == nullptr ||
        (calling_uid != AID_SYSTEM &&
         !::android::PermissionCache::checkPermission(::android::String16(kControlPermission),
                                                      calling_pid, calling_uid))) {
      return ::android::PERMISSION_DENIED;
    }
    if (!data.checkInterface(this)) return ::android::PERMISSION_DENIED;
    const int brightness = data.readInt32();
    const int warmth = data.readInt32();
    if (data.errorCheck() != ::android::OK) return ::android::BAD_VALUE;
    std::lock_guard lock(control_->mutex);
    const auto outcome = control_->control.Apply({.brightness_percent = brightness,
                                                   .warmth_percent = warmth});
    reply->writeNoException();
    reply->writeInt32(static_cast<std::int32_t>(outcome));
    return ::android::OK;
  }

 private:
  std::shared_ptr<ProcessControl> control_;
};

std::once_flag g_publish_once;
bool g_published = false;
std::shared_ptr<ProcessControl> g_control;
::android::sp<ControlBinder> g_binder;

}  // namespace

bool PublishManualFrontLightBinderService() {
  std::call_once(g_publish_once, [] {
    if constexpr (BuildEinkDeviceProfile().has_pogo_buttons) {
      static_cast<void>(ConfigurePogoButtonLongPressSelectors());
    }
    if constexpr (!BuildEinkDeviceProfile().has_frontlight) {
      // Button configuration is independent of the optional front light.
      // A no-front-light product succeeds without publishing a dead Binder
      // surface or constructing a backend that could probe vestigial nodes.
      g_published = true;
      return;
    }
    g_control = std::make_shared<ProcessControl>();
    g_binder = ::android::sp<ControlBinder>::make(g_control);
    g_published = ::android::defaultServiceManager()->addService(
            ::android::String16(kServiceName), g_binder) == ::android::OK;
    if (!g_published) {
      g_binder.clear();
      g_control.reset();
    }
  });
  return g_published;
}

}  // namespace neo2::eink::android
