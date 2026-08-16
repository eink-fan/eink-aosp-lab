#include "neo2/eink/sleep_image_binder_service.h"

#include "neo2/eink/eink_sleep_image_catalog.h"
#include "neo2/eink/render_surface_capture_session.h"

#include <android/sharedmem.h>
#include <binder/Binder.h>
#include <binder/IServiceManager.h>
#include <binder/IPCThreadState.h>
#include <dlfcn.h>
#include <private/android_filesystem_config.h>

#include <sys/mman.h>
#include <unistd.h>

#include <cstddef>
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
constexpr std::int32_t kProtocolVersion = 2;

std::mutex g_session_mutex;
RenderSurfaceCaptureSession* g_session = nullptr;
std::once_flag g_publish_once;
bool g_published = false;

using SharedMemoryGetSize = std::size_t (*)(int);

// The lower-engine implementation is linked into SurfaceFlinger through a
// static archive. Its shared-library dependencies do not propagate through
// that archive, so resolve the platform API at the only call boundary. Keep
// the handle for the process lifetime and fail closed if this Android image
// does not supply the expected native shared-memory API.
SharedMemoryGetSize GetSharedMemorySize = [] {
  void* const handle = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
  if (handle == nullptr) return static_cast<SharedMemoryGetSize>(nullptr);
  return reinterpret_cast<SharedMemoryGetSize>(dlsym(handle, "ASharedMemory_getSize"));
}();

bool IsSystemCaller() {
  return ::android::IPCThreadState::self()->getCallingUid() == AID_SYSTEM;
}

bool ReadPlane(const ::android::Parcel& data, std::vector<std::uint8_t>* output) {
  if (output == nullptr) return false;
  const int fd = data.readFileDescriptor();
  if (data.errorCheck() != ::android::OK || fd < 0) return false;
  if (GetSharedMemorySize == nullptr) return false;
  const std::size_t shared_memory_size = GetSharedMemorySize(fd);
  if (!EinkSleepImageCatalog::IsPanelByteSize(shared_memory_size)) {
    return false;
  }
  // Android's legacy ashmem implementation does not reliably expose caller
  // writes through a private mapping after the producer seals the region
  // read-only. Map the shared object read-only, then immediately copy its
  // validated bytes into the catalog-owned vector below.
  void* mapped = mmap(nullptr, EinkSleepImageCatalog::kPanelBytes, PROT_READ, MAP_SHARED, fd, 0);
  if (mapped == MAP_FAILED) return false;
  const auto* bytes = static_cast<const std::uint8_t*>(mapped);
  output->assign(bytes, bytes + EinkSleepImageCatalog::kPanelBytes);
  munmap(mapped, EinkSleepImageCatalog::kPanelBytes);
  return true;
}

bool ReadEntry(const ::android::Parcel& data, EinkSleepImageCatalog::Entry* entry) {
  if (entry == nullptr) return false;
  entry->width = data.readInt32();
  entry->height = data.readInt32();
  return entry->width == EinkSleepImageCatalog::kPanelWidth &&
          entry->height == EinkSleepImageCatalog::kPanelHeight &&
          ReadPlane(data, &entry->panel_gray) && ReadPlane(data, &entry->alpha);
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
    const auto protocol_version = data.readInt32();
    if (data.errorCheck() != ::android::OK || protocol_version != kProtocolVersion) {
      return ::android::BAD_VALUE;
    }
    if (code == kScreenOff || code == kScreenOn) {
      const auto epoch = static_cast<std::uint64_t>(data.readInt64());
      if (data.errorCheck() != ::android::OK || epoch == 0) return ::android::BAD_VALUE;
      reply->writeNoException();
      reply->writeInt32(kProtocolVersion);
      if (code == kScreenOff) {
        // The early power intent transports only the already accepted epoch.
        // Native selects the retained copied candidate and queues it directly;
        // no provider or catalog mutation occurs here.
        reply->writeInt32(static_cast<std::int32_t>(g_session->PresentSleepImage(epoch)));
      } else {
        reply->writeInt32(static_cast<std::int32_t>(g_session->DisarmSleepCycle(epoch)));
      }
      return ::android::OK;
    }
    if (code != kPublishCatalog) return ::android::UNKNOWN_TRANSACTION;
    const auto epoch = static_cast<std::uint64_t>(data.readInt64());
    const int count = data.readInt32();
    const int selected_index = data.readInt32();
    const auto mode = static_cast<SleepImagePresentationMode>(data.readInt32());
    if (data.errorCheck() != ::android::OK || epoch == 0 || count <= 0 ||
        count > static_cast<int>(EinkSleepImageCatalog::kMaximumEntries) || selected_index < 0 ||
        selected_index >= count ||
        (mode != SleepImagePresentationMode::kOpaque &&
         mode != SleepImagePresentationMode::kOverlay)) {
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
            std::move(entries), static_cast<std::size_t>(selected_index), mode);
    reply->writeNoException();
    reply->writeInt32(kProtocolVersion);
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
