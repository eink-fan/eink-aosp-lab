#include "neo2/eink/manual_frontlight_binder_service.h"

#include "neo2/eink/frontlight_direct_backend.h"

#include <eink_manual_frontlight_control.h>

#include <binder/Binder.h>
#include <binder/IServiceManager.h>
#include <binder/IPCThreadState.h>

#include <private/android_filesystem_config.h>

#include <cstdint>
#include <memory>
#include <mutex>

namespace neo2::eink::android {
namespace {

constexpr char kServiceName[] = "neo2_frontlight_controls";
constexpr char kDescriptor[] = "org.neo2.controls.IFrontLightControl";

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
    if (code != ::android::IBinder::FIRST_CALL_TRANSACTION || reply == nullptr ||
        ::android::IPCThreadState::self()->getCallingUid() != AID_SYSTEM) {
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
