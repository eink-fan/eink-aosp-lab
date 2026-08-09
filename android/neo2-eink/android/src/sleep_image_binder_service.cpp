#include "neo2/eink/sleep_image_binder_service.h"

#include "neo2/eink/eink_sleep_image_catalog.h"
#include "neo2/eink/render_surface_capture_session.h"

#include <binder/Binder.h>
#include <binder/IServiceManager.h>
#include <binder/IPCThreadState.h>
#include <private/android_filesystem_config.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <mutex>
#include <vector>

namespace neo2::eink::android {
namespace {

constexpr char kServiceName[] = "neo2_sleep_image";
constexpr char kDescriptor[] = "android.neo2.INeo2SleepImage";
constexpr std::uint32_t kPublishCatalog = ::android::IBinder::FIRST_CALL_TRANSACTION;
constexpr std::uint32_t kScreenOff = kPublishCatalog + 1;
constexpr std::uint32_t kScreenOn = kPublishCatalog + 2;

std::mutex g_session_mutex;
RenderSurfaceCaptureSession* g_session = nullptr;
std::once_flag g_publish_once;
bool g_published = false;

bool IsSystemCaller() {
  return ::android::IPCThreadState::self()->getCallingUid() == AID_SYSTEM;
}

bool ReadEntry(const ::android::Parcel& data, EinkSleepImageCatalog::Entry* entry) {
  if (entry == nullptr) return false;
  entry->width = data.readInt32();
  entry->height = data.readInt32();
  const int fd = data.readFileDescriptor();
  if (data.errorCheck() != ::android::OK || fd < 0 ||
      entry->width != EinkSleepImageCatalog::kPanelWidth ||
      entry->height != EinkSleepImageCatalog::kPanelHeight) {
    return false;
  }
  struct stat st {};
  if (fstat(fd, &st) != 0 || st.st_size != static_cast<off_t>(EinkSleepImageCatalog::kPanelBytes)) {
    return false;
  }
  void* mapped = mmap(nullptr, EinkSleepImageCatalog::kPanelBytes, PROT_READ, MAP_PRIVATE, fd, 0);
  if (mapped == MAP_FAILED) return false;
  const auto* bytes = static_cast<const std::uint8_t*>(mapped);
  entry->panel_gray.assign(bytes, bytes + EinkSleepImageCatalog::kPanelBytes);
  munmap(mapped, EinkSleepImageCatalog::kPanelBytes);
  return true;
}

class SleepImageBinder final : public ::android::BBinder {
 public:
  const ::android::String16& getInterfaceDescriptor() const override {
    static const ::android::String16 kInterfaceDescriptor(kDescriptor);
    return kInterfaceDescriptor;
  }

  ::android::status_t onTransact(std::uint32_t code, const ::android::Parcel& data,
                                 ::android::Parcel* reply, std::uint32_t flags) override {
    static_cast<void>(flags);
    if (reply == nullptr || !IsSystemCaller() || !data.checkInterface(this)) {
      return ::android::PERMISSION_DENIED;
    }
    std::lock_guard lock(g_session_mutex);
    if (g_session == nullptr) return ::android::NAME_NOT_FOUND;
    if (code == kScreenOff || code == kScreenOn) {
      const auto epoch = static_cast<std::uint64_t>(data.readInt64());
      if (data.errorCheck() != ::android::OK || epoch == 0) return ::android::BAD_VALUE;
      if (code == kScreenOff) g_session->SetScreenOffEpoch(epoch);
      else g_session->SetScreenOnEpoch(epoch);
      reply->writeNoException();
      return ::android::OK;
    }
    if (code != kPublishCatalog) return ::android::UNKNOWN_TRANSACTION;
    const auto epoch = static_cast<std::uint64_t>(data.readInt64());
    const int count = data.readInt32();
    const int selected_index = data.readInt32();
    if (data.errorCheck() != ::android::OK || epoch == 0 || count <= 0 ||
        count > static_cast<int>(EinkSleepImageCatalog::kMaximumEntries) || selected_index < 0 ||
        selected_index >= count) {
      return ::android::BAD_VALUE;
    }
    std::vector<EinkSleepImageCatalog::Entry> entries;
    entries.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
      EinkSleepImageCatalog::Entry entry;
      if (!ReadEntry(data, &entry)) return ::android::BAD_VALUE;
      entries.push_back(std::move(entry));
    }
    const auto result = g_session->PublishSleepImageCatalog(
            epoch, EinkSleepImageCatalog::kPanelWidth, EinkSleepImageCatalog::kPanelHeight,
            std::move(entries), static_cast<std::size_t>(selected_index));
    reply->writeNoException();
    reply->writeInt32(static_cast<std::int32_t>(result));
    return ::android::OK;
  }
};

::android::sp<SleepImageBinder> g_binder;

}  // namespace

bool PublishSleepImageBinderService() {
  std::call_once(g_publish_once, [] {
    g_binder = ::android::sp<SleepImageBinder>::make();
    g_published = ::android::defaultServiceManager()->addService(
            ::android::String16(kServiceName), g_binder) == ::android::OK;
    if (!g_published) g_binder.clear();
  });
  return g_published;
}

void RegisterSleepImageCaptureSession(RenderSurfaceCaptureSession* session) {
  std::lock_guard lock(g_session_mutex);
  g_session = session;
}

void UnregisterSleepImageCaptureSession(RenderSurfaceCaptureSession* session) {
  std::lock_guard lock(g_session_mutex);
  if (g_session == session) g_session = nullptr;
}

}  // namespace neo2::eink::android
