#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "Neo2EinkVendorSync"

#include "neo2/eink/vendor_sync_controller_backend.h"

#include "neo2/eink/android_eink_pipeline.h"

#include <log/log.h>
#include <ui/Fence.h>
#include <ui/GraphicBuffer.h>
#include <ui/Rect.h>
#include <utils/RefBase.h>
#include <utils/Vector.h>

#include <array>
#include <cstddef>
#include <memory>
#include <mutex>
#include <utility>

// This is the measured same-build layout, not a replacement vendor header.
// `next` is producer-owned and must remain null for M4's one full-frame record.
struct hwc_epdc_llist {
  ::android::Rect rect;
  std::int32_t mode_bits;
  std::uint32_t unknown_zero;
  void* next;
};

static_assert(sizeof(::android::Rect) == 16);
static_assert(offsetof(hwc_epdc_llist, rect) == 0x00);
static_assert(offsetof(hwc_epdc_llist, mode_bits) == 0x10);
static_assert(offsetof(hwc_epdc_llist, unknown_zero) == 0x14);
static_assert(offsetof(hwc_epdc_llist, next) == 0x18);
static_assert(sizeof(hwc_epdc_llist) == 0x20);

namespace android {

// Exact virtual shape recovered from the stock same-build callback vtable:
// RefBase destructors/hooks, then onHandWrite at +0x30 and isNeedDelay at +0x38.
// There is no vendor header for this interface in AOSP; do not use it outside
// the verified RM06L Android-14 library family.
class IEPDCCallback : public RefBase {
 public:
  virtual void onHandWrite(long timestamp_ns) = 0;
  virtual bool isNeedDelay(Vector<hwc_epdc_llist>& records) = 0;

 protected:
  ~IEPDCCallback() override = default;
};

// The exact vendor constructor stores state through byte 0x77df. Its dynamic
// destructor is not exported/recovered, therefore M4 never destroys this
// object or invokes an engine deinitializer; the object is intentionally
// process-lifetime after construction.
class IdisplayEpdcDeviceSync {
 public:
  explicit IdisplayEpdcDeviceSync(sp<IEPDCCallback> callback);
  void init(unsigned int width, unsigned int height);
  void refresh(sp<GraphicBuffer> buffer, sp<Fence> fence, hwc_epdc_llist& record);

 private:
  alignas(8) std::array<std::byte, 0x77e0> vendor_storage_;
};

static_assert(sizeof(IdisplayEpdcDeviceSync) == 0x77e0);
static_assert(alignof(IdisplayEpdcDeviceSync) == 8);

}  // namespace android

namespace neo2::eink::android {

namespace {

constexpr int kPanelWidth = 1448;
constexpr int kPanelHeight = 1072;
constexpr int kObservedGc16FullMode =
        ToEngineMode(RefreshMode::kGc16Partial) + ToEngineMode(RefreshMode::kFullUpdate);

class NoDelayCallback final : public ::android::IEPDCCallback {
 public:
  void onHandWrite(long /*timestamp_ns*/) override {}

  bool isNeedDelay(::android::Vector<hwc_epdc_llist>& /*records*/) override {
    // Sync::refresh then uses its stock immediate engine path. In particular,
    // no caller-owned Vector is retained after this method returns.
    return false;
  }
};

bool IsObservedDirectVendorFrame(const Frame& frame) {
  return frame.compose_buffer != nullptr && frame.source_buffer == nullptr &&
          frame.grayscale_buffer == nullptr && frame.dirty_rect.left == 0 &&
          frame.dirty_rect.top == 0 && frame.dirty_rect.right == kPanelWidth &&
          frame.dirty_rect.bottom == kPanelHeight &&
          frame.engine_mode == kObservedGc16FullMode && frame.policy_flag == 0 &&
          !frame.handwriting;
}

}  // namespace

class SyncControllerBackend::Impl {
 public:
  std::mutex mutex;
  Diagnostics diagnostics;
  // Intentionally never deleted. The vendor destructor is not a recovered
  // public lifecycle and the engine itself is process-lifetime state.
  ::android::IdisplayEpdcDeviceSync* sync = nullptr;
};

SyncControllerBackend::SyncControllerBackend() : impl_(std::make_unique<Impl>()) {}

SyncControllerBackend::~SyncControllerBackend() = default;

bool SyncControllerBackend::InitializeForObservedPanel() {
  std::lock_guard lock(impl_->mutex);
  if (impl_->diagnostics.state != State::kStopped) {
    return false;
  }

  // The local sp is deliberately temporary: Sync's recovered constructor takes
  // its own strong reference at +0x77b0. The Sync allocation remains leaked
  // through process exit rather than being destructed by an unknown ABI path.
  const auto no_delay_callback = ::android::sp<NoDelayCallback>::make();
  ::android::sp<::android::IEPDCCallback> callback(no_delay_callback);
  auto* sync = new ::android::IdisplayEpdcDeviceSync(std::move(callback));
  ALOGI("M4 vendor Sync init begin: geometry=%dx%d", kPanelWidth, kPanelHeight);
  sync->init(kPanelWidth, kPanelHeight);
  impl_->sync = sync;
  impl_->diagnostics.state = State::kInitialized;
  ALOGI("M4 vendor Sync init returned");
  return true;
}

bool SyncControllerBackend::Submit(const Frame& frame) {
  std::lock_guard lock(impl_->mutex);
  if (impl_->diagnostics.state != State::kInitialized || !IsObservedDirectVendorFrame(frame)) {
    ++impl_->diagnostics.rejected_frames;
    return false;
  }

  if (frame.compose_buffer->TypeTag() != AndroidComposeBuffer::kTypeTag) {
    ++impl_->diagnostics.rejected_frames;
    return false;
  }
  // The preceding tag check is the no-RTTI equivalent of a checked downcast.
  // AndroidComposeBuffer is final and is constructed only inside this Android
  // capture path.
  const auto compose = std::static_pointer_cast<AndroidComposeBuffer>(frame.compose_buffer);
  if (!compose || !compose->buffer() || !compose->ready_fence()) {
    ++impl_->diagnostics.rejected_frames;
    return false;
  }

  hwc_epdc_llist record{
          .rect = ::android::Rect(0, 0, kPanelWidth, kPanelHeight),
          .mode_bits = kObservedGc16FullMode,
          .unknown_zero = 0,
          .next = nullptr,
  };
  ALOGI("M4 vendor Sync one-shot submit begin: sequence=%llu mode=0x%x",
        static_cast<unsigned long long>(frame.sequence), record.mode_bits);
  impl_->sync->refresh(compose->buffer(), compose->ready_fence(), record);
  ++impl_->diagnostics.submitted_frames;
  ALOGI("M4 vendor Sync one-shot submit returned: sequence=%llu",
        static_cast<unsigned long long>(frame.sequence));
  return true;
}

SyncControllerBackend::Diagnostics SyncControllerBackend::diagnostics() const {
  std::lock_guard lock(impl_->mutex);
  return impl_->diagnostics;
}

}  // namespace neo2::eink::android